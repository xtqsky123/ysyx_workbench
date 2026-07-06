#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "difftest.h"

class Vexample;
class VerilatedVcdC;

namespace npc {

class TraceManager;

struct TraceConfig {
  bool itrace = false;
  bool mtrace = false;
  bool ftrace = false;
  const char *elf_file = nullptr;
};

class Simulator {
public:
  Simulator(const char *img_file, const char *diff_so, const TraceConfig &trace_config);
  ~Simulator();

  void load_image(const char *img_file);
  void reset();
  void eval_once();
  void tick();
  void step(bool trace);
  void run(unsigned long n);
  void halt(int code, uint32_t pc);

  uint32_t guest_to_host(uint32_t addr);
  uint32_t pmem_read(uint32_t addr) const;
  void pmem_write(uint32_t addr, uint32_t data, uint8_t mask);

  uint32_t pc() const;
  uint32_t inst() const;
  CPUState cpu_state() const;
  void dump_registers() const;
  void dump_memory(uint32_t addr, int n) const;
  void print_state() const;
  void print_trap() const;
  void print_trace_status() const;
  void clear_stop();
  void request_stop();
  bool stop_requested() const;
  bool halted() const;
  uint64_t get_uptime_us() const;
  void set_itrace(bool enabled);
  void set_mtrace(bool enabled);
  void set_ftrace(bool enabled);
  bool itrace_enabled() const;
  bool mtrace_enabled() const;
  bool ftrace_enabled() const;

private:
  Vexample *top_ = nullptr;
  VerilatedVcdC *tfp_ = nullptr;
  std::unique_ptr<DiffTest> difftest_;
  std::unique_ptr<class TraceManager> trace_;
  size_t img_size_ = 0;
  uint64_t sim_time_ = 0;
  bool halted_ = false;
  bool stop_requested_ = false;
  int halt_code_ = 0;
  uint32_t halt_pc_ = 0;
};

Simulator &get_simulator();

}  // namespace npc

extern "C" uint32_t pmem_read(uint32_t addr);
extern "C" void pmem_write(uint32_t addr, uint32_t data, uint8_t mask);
extern "C" void npc_halt(int code, uint32_t pc);
double sc_time_stamp();
