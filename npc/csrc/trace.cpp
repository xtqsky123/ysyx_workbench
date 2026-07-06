#include "trace.h"

#include <capstone/capstone.h>
#include <dlfcn.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace npc {

namespace {

void print_indent(int depth) {
  for (int i = 0; i < depth; i++) {
    std::printf("  ");
  }
}

}  // namespace

TraceManager::TraceManager(bool itrace, bool mtrace, bool ftrace, const char *img_file, const char *elf_file)
    : itrace_enabled_(itrace),
      mtrace_enabled_(mtrace),
      ftrace_enabled_(ftrace),
      img_file_(img_file != nullptr ? img_file : ""),
      elf_file_(elf_file != nullptr ? elf_file : "") {
  if (itrace_enabled_) {
    init_disasm();
  }
  if (ftrace_enabled_) {
    init_ftrace(img_file, elf_file);
  }
}

TraceManager::~TraceManager() {
  if (capstone_handle_ != nullptr) {
    dlclose(capstone_handle_);
  }
}

void TraceManager::set_itrace(bool enabled) {
  if (enabled && !itrace_enabled_) {
    init_disasm();
  }
  itrace_enabled_ = enabled;
}

void TraceManager::set_mtrace(bool enabled) {
  mtrace_enabled_ = enabled;
}

void TraceManager::set_ftrace(bool enabled) {
  if (enabled && !ftrace_enabled_) {
    init_ftrace(img_file_.c_str(), elf_file_.empty() ? nullptr : elf_file_.c_str());
  }
  ftrace_enabled_ = enabled;
}

bool TraceManager::itrace_enabled() const {
  return itrace_enabled_;
}

bool TraceManager::mtrace_enabled() const {
  return mtrace_enabled_;
}

bool TraceManager::ftrace_enabled() const {
  return ftrace_enabled_;
}

void TraceManager::init_disasm() {
  if (capstone_handle_ != nullptr) {
    return;
  }

  capstone_handle_ = dlopen(NPC_CAPSTONE_LIB, RTLD_LAZY);
  if (capstone_handle_ == nullptr) {
    std::fprintf(stderr, "Failed to open capstone library %s: %s\n", NPC_CAPSTONE_LIB, dlerror());
    std::exit(1);
  }

  auto cs_open_dl = reinterpret_cast<cs_err (*)(cs_arch, cs_mode, csh *)>(dlsym(capstone_handle_, "cs_open"));
  cs_disasm_dl_ = reinterpret_cast<size_t (*)(uintptr_t, const uint8_t *, size_t, uint64_t, size_t, void **)>(
      dlsym(capstone_handle_, "cs_disasm"));
  cs_free_dl_ = reinterpret_cast<void (*)(void *, size_t)>(dlsym(capstone_handle_, "cs_free"));

  if (cs_open_dl == nullptr || cs_disasm_dl_ == nullptr || cs_free_dl_ == nullptr) {
    std::fprintf(stderr, "Incomplete capstone symbols in %s\n", NPC_CAPSTONE_LIB);
    std::exit(1);
  }

  csh handle = 0;
  if (cs_open_dl(CS_ARCH_RISCV, static_cast<cs_mode>(CS_MODE_RISCV32 | CS_MODE_RISCVC), &handle) != CS_ERR_OK) {
    std::fprintf(stderr, "Failed to initialize capstone\n");
    std::exit(1);
  }
  cs_handle_ = handle;
}

std::string TraceManager::infer_elf_path(const char *img_file) {
  if (img_file == nullptr) {
    return {};
  }

  const char *suffix = std::strrchr(img_file, '.');
  if (suffix == nullptr || std::strcmp(suffix, ".bin") != 0) {
    return {};
  }

  return std::string(img_file, suffix - img_file) + ".elf";
}

void TraceManager::load_func_symbols_32(FILE *fp, const Elf32_Ehdr &ehdr) {
  std::vector<Elf32_Shdr> shdrs(ehdr.e_shnum);
  std::fseek(fp, ehdr.e_shoff, SEEK_SET);
  if (std::fread(shdrs.data(), ehdr.e_shentsize, ehdr.e_shnum, fp) != ehdr.e_shnum) {
    std::fprintf(stderr, "Failed to read ELF section headers\n");
    std::exit(1);
  }

  for (int i = 0; i < ehdr.e_shnum; i++) {
    if (shdrs[i].sh_type != SHT_SYMTAB) {
      continue;
    }

    const Elf32_Shdr &symtab = shdrs[i];
    const Elf32_Shdr &strtab = shdrs[symtab.sh_link];
    std::vector<char> strbuf(strtab.sh_size);
    std::fseek(fp, strtab.sh_offset, SEEK_SET);
    if (std::fread(strbuf.data(), 1, strtab.sh_size, fp) != strtab.sh_size) {
      std::fprintf(stderr, "Failed to read ELF string table\n");
      std::exit(1);
    }

    size_t nr_syms = symtab.sh_size / sizeof(Elf32_Sym);
    std::vector<Elf32_Sym> syms(nr_syms);
    std::fseek(fp, symtab.sh_offset, SEEK_SET);
    if (std::fread(syms.data(), sizeof(Elf32_Sym), nr_syms, fp) != nr_syms) {
      std::fprintf(stderr, "Failed to read ELF symbol table\n");
      std::exit(1);
    }

    for (const auto &sym : syms) {
      if (ELF32_ST_TYPE(sym.st_info) != STT_FUNC || sym.st_name == 0) {
        continue;
      }
      FuncSymbol item;
      item.start = sym.st_value;
      item.end = sym.st_size == 0 ? sym.st_value : sym.st_value + sym.st_size;
      item.name = strbuf.data() + sym.st_name;
      func_symbols_.push_back(std::move(item));
    }
    return;
  }
}

void TraceManager::init_ftrace(const char *img_file, const char *elf_file) {
  if (!func_symbols_.empty()) {
    return;
  }

  std::string path = elf_file != nullptr ? elf_file : infer_elf_path(img_file);
  if (path.empty()) {
    std::fprintf(stderr, "FTrace enabled, but no ELF file is available\n");
    ftrace_enabled_ = false;
    return;
  }

  FILE *fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    std::perror("fopen");
    ftrace_enabled_ = false;
    return;
  }

  unsigned char ident[EI_NIDENT];
  if (std::fread(ident, 1, EI_NIDENT, fp) != EI_NIDENT || std::memcmp(ident, ELFMAG, SELFMAG) != 0) {
    std::fprintf(stderr, "Invalid ELF file: %s\n", path.c_str());
    std::fclose(fp);
    ftrace_enabled_ = false;
    return;
  }
  std::rewind(fp);

  if (ident[EI_CLASS] == ELFCLASS32) {
    Elf32_Ehdr ehdr = {};
    if (std::fread(&ehdr, 1, sizeof(ehdr), fp) != sizeof(ehdr)) {
      std::fprintf(stderr, "Failed to read ELF header: %s\n", path.c_str());
      std::fclose(fp);
      ftrace_enabled_ = false;
      return;
    }
    load_func_symbols_32(fp, ehdr);
  } else {
    std::fprintf(stderr, "Unsupported ELF class in %s\n", path.c_str());
    ftrace_enabled_ = false;
  }

  std::fclose(fp);
}

const char *TraceManager::find_func_name(uint32_t addr) const {
  for (const auto &sym : func_symbols_) {
    if ((addr >= sym.start && addr < sym.end) || (sym.start == addr && sym.end == addr)) {
      return sym.name.c_str();
    }
  }
  return "???";
}

void TraceManager::trace_inst(uint32_t pc, uint32_t inst) const {
  if (!itrace_enabled_) {
    return;
  }

  cs_insn *insn = nullptr;
  size_t count = cs_disasm_dl_(cs_handle_, reinterpret_cast<const uint8_t *>(&inst), 4, pc, 0,
      reinterpret_cast<void **>(&insn));
  if (count != 1 || insn == nullptr) {
    std::printf("ITRACE: 0x%08x: %02x %02x %02x %02x  unknown\n",
        pc,
        inst & 0xff,
        (inst >> 8) & 0xff,
        (inst >> 16) & 0xff,
        (inst >> 24) & 0xff);
    return;
  }

  std::printf("ITRACE: 0x%08x: %02x %02x %02x %02x  %s",
      pc,
      inst & 0xff,
      (inst >> 8) & 0xff,
      (inst >> 16) & 0xff,
      (inst >> 24) & 0xff,
      insn->mnemonic);
  if (insn->op_str[0] != '\0') {
    std::printf("\t%s", insn->op_str);
  }
  std::printf("\n");
  cs_free_dl_(insn, count);
}

void TraceManager::trace_mem_read(uint32_t pc, uint32_t addr, int len, uint32_t data) const {
  if (!mtrace_enabled_) {
    return;
  }
  std::printf("MTRACE: read  pc=0x%08x addr=0x%08x len=%d data=0x%08x\n", pc, addr, len, data);
}

void TraceManager::trace_mem_write(uint32_t pc, uint32_t addr, int len, uint32_t data) const {
  if (!mtrace_enabled_) {
    return;
  }
  std::printf("MTRACE: write pc=0x%08x addr=0x%08x len=%d data=0x%08x\n", pc, addr, len, data);
}

void TraceManager::trace_call(uint32_t pc, uint32_t target) {
  if (!ftrace_enabled_) {
    return;
  }
  print_indent(ftrace_depth_);
  std::printf("FTRACE: call 0x%08x -> 0x%08x <%s>\n", pc, target, find_func_name(target));
  ftrace_depth_++;
}

void TraceManager::trace_ret(uint32_t pc, uint32_t target) {
  if (!ftrace_enabled_) {
    return;
  }
  if (ftrace_depth_ > 0) {
    ftrace_depth_--;
  }
  print_indent(ftrace_depth_);
  std::printf("FTRACE: ret  0x%08x -> 0x%08x <%s>\n", pc, target, find_func_name(target));
}

}  // namespace npc
