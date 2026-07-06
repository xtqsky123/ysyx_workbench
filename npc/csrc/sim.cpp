#include "sim.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#include "Vexample.h"
#include "Vexample___024root.h"
#include "sdb.h"
#include "trace.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

namespace {

constexpr uint32_t kResetVector = 0x80000000u;
constexpr size_t kPmemSize = 128 * 1024 * 1024;
constexpr uint32_t kSerialPort = 0x10000000u;
constexpr uint32_t kRtcAddr    = 0x10000048u;
constexpr uint32_t kKbdAddr    = 0x10000060u;

npc::Simulator *g_sim = nullptr;
uint8_t pmem[kPmemSize] = {};
uint64_t boot_time = 0;
bool serial_silent = false;
bool trm_debug = false;

const char *kGprNames[16] = {
    "$0", "$ra", "$sp", "$gp", "$tp", "$t0", "$t1", "$t2",
    "$s0", "$s1", "$a0", "$a1", "$a2", "$a3", "$a4", "$a5",
};

const char *kGprAbiNames[16] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0/fp", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
};

uint64_t now_us() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

uint32_t sign_extend(uint32_t value, int bits) {
  const uint32_t mask = 1u << (bits - 1);
  return (value ^ mask) - mask;
}

uint32_t decode_i_imm(uint32_t inst) {
  return sign_extend(inst >> 20, 12);
}

bool is_load_inst(uint32_t inst) {
  return (inst & 0x7f) == 0x03;
}

bool is_jal_call(uint32_t inst) {
  return (inst & 0x7f) == 0x6f && ((inst >> 7) & 0x1f) == 1;
}

bool is_jalr_call(uint32_t inst) {
  return (inst & 0x707f) == 0x67 && ((inst >> 7) & 0x1f) == 1;
}

bool is_jalr_ret(uint32_t inst) {
  return (inst & 0x707f) == 0x67 &&
      ((inst >> 7) & 0x1f) == 0 &&
      ((inst >> 15) & 0x1f) == 1 &&
      decode_i_imm(inst) == 0;
}

int decode_mem_len(uint32_t inst) {
  switch ((inst >> 12) & 0x7) {
    case 0x0: return 1;
    case 0x1: return 2;
    case 0x2: return 4;
    case 0x4: return 1;
    case 0x5: return 2;
    default: return 0;
  }
}

}  // namespace

extern "C" uint32_t pmem_read(uint32_t addr) {
  return npc::get_simulator().pmem_read(addr);
}

extern "C" void pmem_write(uint32_t addr, uint32_t data, uint8_t mask) {
  npc::get_simulator().pmem_write(addr, data, mask);
}

extern "C" void npc_halt(int code, uint32_t pc) {
  npc::get_simulator().halt(code, pc);
}

double sc_time_stamp() {
  return static_cast<double>(npc::get_simulator().get_uptime_us());
}

namespace npc {

Simulator &get_simulator() {
  return *g_sim;
}

Simulator::Simulator(const char *img_file, const char *diff_so, const TraceConfig &trace_config) {
  g_sim = this;
  boot_time = now_us();
  top_ = new Vexample("TOP");

  const char *wave_env = std::getenv("NPC_WAVE");
  const char *silent_env = std::getenv("NPC_SILENT");
  const char *trm_debug_env = std::getenv("NPC_TRMDBG");
  serial_silent = (silent_env != nullptr && std::strcmp(silent_env, "1") == 0);
  trm_debug = (trm_debug_env != nullptr && std::strcmp(trm_debug_env, "1") == 0);
  if (wave_env != nullptr && std::strcmp(wave_env, "1") == 0) {
    tfp_ = new VerilatedVcdC;
    Verilated::traceEverOn(true);
    top_->trace(tfp_, 0, 0);
    tfp_->open("build/wave.vcd");
  }

  load_image(img_file);
  reset();
  trace_ = std::make_unique<TraceManager>(
      trace_config.itrace, trace_config.mtrace, trace_config.ftrace, img_file, trace_config.elf_file);
  difftest_ = std::make_unique<DiffTest>(diff_so);
  difftest_->init(pmem, img_size_, cpu_state());
}

Simulator::~Simulator() {
  if (top_ != nullptr) {
    top_->final();
    delete top_;
  }
  if (tfp_ != nullptr) {
    tfp_->close();
    delete tfp_;
  }
}

uint32_t Simulator::guest_to_host(uint32_t addr) {
  if (addr < kResetVector || addr - kResetVector >= kPmemSize) {
    std::fprintf(stderr, "address out of bound: 0x%08x\n", addr);
    std::exit(1);
  }
  return addr - kResetVector;
}

void Simulator::load_image(const char *img_file) {
  if (img_file == nullptr) return;
  FILE *fp = std::fopen(img_file, "rb");
  if (fp == nullptr) {
    std::perror("fopen");
    std::exit(1);
  }
  std::fseek(fp, 0, SEEK_END);
  size_t size = static_cast<size_t>(std::ftell(fp));
  std::fseek(fp, 0, SEEK_SET);
  size_t ret = std::fread(pmem, 1, size, fp);
  std::fclose(fp);
  if (ret != size) {
    std::fprintf(stderr, "failed to read image: %s\n", img_file);
    std::exit(1);
  }
  img_size_ = size;
  std::printf("load image: %s, size = %zu\n", img_file, size);
}

uint32_t Simulator::pmem_read(uint32_t addr) const {
  if (addr == kRtcAddr) {
    return static_cast<uint32_t>(get_uptime_us());
  }
  if (addr == kRtcAddr + 4) {
    return static_cast<uint32_t>(get_uptime_us() >> 32);
  }
  if (addr == kKbdAddr) {
    return 0;
  }

  if (addr < kResetVector || addr - kResetVector >= kPmemSize) {
    std::fprintf(stderr, "pmem_read out of bound: 0x%08x\n", addr);
    std::exit(1);
  }
  uint32_t offset = addr - kResetVector;
  return *reinterpret_cast<const uint32_t *>(pmem + offset);
}

void Simulator::pmem_write(uint32_t addr, uint32_t data, uint8_t mask) {
  if (addr >= kSerialPort && addr < kSerialPort + 4) {
    uint32_t offset = addr - kSerialPort;
    if (mask & (1u << offset)) {
      if (!serial_silent) {
        std::putchar((data >> (offset * 8)) & 0xffu);
        std::fflush(stdout);
      }
    }
    return;
  }

  if (addr < kResetVector || addr - kResetVector >= kPmemSize) {
    std::fprintf(stderr, "pmem_write out of bound: 0x%08x\n", addr);
    std::exit(1);
  }
  uint32_t offset = (addr & ~0x3u) - kResetVector;
  for (int i = 0; i < 4; i++) {
    if (mask & (1u << i)) {
      pmem[offset + i] = (data >> (i * 8)) & 0xffu;
    }
  }
}

void Simulator::eval_once() {
  top_->eval_step();
}

void Simulator::tick() {
  top_->clk = 0;
  eval_once();
  if (tfp_ != nullptr) {
    tfp_->dump(sim_time_++);
  }
  top_->clk = 1;
  eval_once();
  if (tfp_ != nullptr) {
    tfp_->dump(sim_time_++);
  }
}

void Simulator::reset() {
  top_->reset = 1;
  for (int i = 0; i < 5; i++) tick();
  top_->reset = 0;
}

uint32_t Simulator::pc() const { return top_->pc; }
uint32_t Simulator::inst() const { return top_->inst; }

CPUState Simulator::cpu_state() const {
  CPUState state = {};
  for (int i = 0; i < 16; i++) {
    state.gpr[i] = top_->rootp->example__DOT__u_rf__DOT__rf[i];
  }
  state.pc = top_->pc;
  return state;
}

void Simulator::dump_registers() const {
  CPUState s = cpu_state();
  for (int i = 0; i < 16; i++) {
    std::printf("%-4s (%-5s) = 0x%08x  %10u  %11d\n",
        kGprNames[i], kGprAbiNames[i], s.gpr[i], s.gpr[i], static_cast<int32_t>(s.gpr[i]));
  }
  std::printf("%-4s (%-5s) = 0x%08x  %10u  %11d\n",
      "$pc", "pc", s.pc, s.pc, static_cast<int32_t>(s.pc));
}

void Simulator::dump_memory(uint32_t addr, int n) const {
  for (int i = 0; i < n; i++) {
    uint32_t cur = addr + i * 4;
    uint32_t data = pmem_read(cur);
    std::printf("0x%08x: 0x%08x\n", cur, data);
  }
}

void Simulator::print_state() const {
  std::printf("pc = 0x%08x, inst = 0x%08x\n", pc(), inst());
}

void Simulator::print_trap() const {
  if (!halted_) {
    return;
  }
  const char *msg = (halt_code_ == 0) ? "HIT GOOD TRAP" : "HIT BAD TRAP";
  std::printf("npc: %s at pc = 0x%08x\n", msg, halt_pc_);
}

bool Simulator::halted() const {
  return halted_;
}

void Simulator::clear_stop() {
  stop_requested_ = false;
}

void Simulator::request_stop() {
  stop_requested_ = true;
}

bool Simulator::stop_requested() const {
  return stop_requested_;
}

uint64_t Simulator::get_uptime_us() const {
  return now_us() - boot_time;
}

void Simulator::print_trace_status() const {
  std::printf("itrace: %s\n", itrace_enabled() ? "on" : "off");
  std::printf("mtrace: %s\n", mtrace_enabled() ? "on" : "off");
  std::printf("ftrace: %s\n", ftrace_enabled() ? "on" : "off");
}

void Simulator::set_itrace(bool enabled) {
  trace_->set_itrace(enabled);
}

void Simulator::set_mtrace(bool enabled) {
  trace_->set_mtrace(enabled);
}

void Simulator::set_ftrace(bool enabled) {
  trace_->set_ftrace(enabled);
}

bool Simulator::itrace_enabled() const {
  return trace_->itrace_enabled();
}

bool Simulator::mtrace_enabled() const {
  return trace_->mtrace_enabled();
}

bool Simulator::ftrace_enabled() const {
  return trace_->ftrace_enabled();
}

void Simulator::halt(int code, uint32_t pc_val) {
  halted_ = true;
  halt_code_ = code;
  halt_pc_ = pc_val;
}

void Simulator::step(bool trace) {
  if (halted_) {
    return;
  }
  const uint32_t pc_before = pc();
  const uint32_t inst_before = inst();
  const uint32_t load_addr_before = top_->rootp->example__DOT__load_addr;
  const uint32_t load_data_before = top_->rootp->example__DOT__wb_data;
  const bool store_en_before = top_->rootp->example__DOT__store_en;
  const uint32_t store_addr_before = top_->rootp->example__DOT__store_addr;
  const uint32_t store_data_before = top_->rootp->example__DOT__rs2_data;

  tick();

  if (trace || itrace_enabled()) {
    trace_->trace_inst(pc_before, inst_before);
  }
  if (mtrace_enabled()) {
    if (is_load_inst(inst_before)) {
      int len = decode_mem_len(inst_before);
      if (len > 0) {
        trace_->trace_mem_read(pc_before, load_addr_before, len, load_data_before);
      }
    }
    if (store_en_before) {
      int len = decode_mem_len(inst_before);
      if (len > 0) {
        trace_->trace_mem_write(pc_before, store_addr_before, len, store_data_before);
      }
    }
  }
  if (ftrace_enabled()) {
    if (is_jal_call(inst_before) || is_jalr_call(inst_before)) {
      trace_->trace_call(pc_before, pc());
    } else if (is_jalr_ret(inst_before)) {
      trace_->trace_ret(pc_before, pc());
    }
  }
  if (trm_debug && pc() >= 0x80000140u && pc() <= 0x80000190u) {
    uint32_t sp = top_->rootp->example__DOT__u_rf__DOT__rf[2];
    std::printf("[trmdbg] pc=0x%08x inst=0x%08x sp=0x%08x x8=0x%08x x9=0x%08x x10=0x%08x x11=0x%08x x12=0x%08x x13=0x%08x x14=0x%08x x15=0x%08x mem[sp]=0x%08x mem[sp+4]=0x%08x mem[sp+8]=0x%08x\n",
        pc(), inst(),
        sp,
        top_->rootp->example__DOT__u_rf__DOT__rf[8],
        top_->rootp->example__DOT__u_rf__DOT__rf[9],
        top_->rootp->example__DOT__u_rf__DOT__rf[10],
        top_->rootp->example__DOT__u_rf__DOT__rf[11],
        top_->rootp->example__DOT__u_rf__DOT__rf[12],
        top_->rootp->example__DOT__u_rf__DOT__rf[13],
        top_->rootp->example__DOT__u_rf__DOT__rf[14],
        top_->rootp->example__DOT__u_rf__DOT__rf[15],
        pmem_read(sp),
        pmem_read(sp + 4),
        pmem_read(sp + 8));
  }
  difftest_->step(cpu_state());
  if (wp_check()) {
    request_stop();
  }
}

void Simulator::run(unsigned long n) {
  clear_stop();
  while (!halted_ && !stop_requested_ && n-- > 0) {
    step(false);
  }
  if (halted_) {
    print_trap();
  } else if (!stop_requested_) {
    std::puts("npc: simulation stopped");
  }
}

}  // namespace npc
