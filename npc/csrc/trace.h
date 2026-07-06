#pragma once

#include <elf.h>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace npc {

class TraceManager {
public:
  TraceManager(bool itrace, bool mtrace, bool ftrace, const char *img_file, const char *elf_file);
  ~TraceManager();

  void set_itrace(bool enabled);
  void set_mtrace(bool enabled);
  void set_ftrace(bool enabled);

  bool itrace_enabled() const;
  bool mtrace_enabled() const;
  bool ftrace_enabled() const;

  void trace_inst(uint32_t pc, uint32_t inst) const;
  void trace_mem_read(uint32_t pc, uint32_t addr, int len, uint32_t data) const;
  void trace_mem_write(uint32_t pc, uint32_t addr, int len, uint32_t data) const;
  void trace_call(uint32_t pc, uint32_t target);
  void trace_ret(uint32_t pc, uint32_t target);

private:
  struct FuncSymbol {
    uint32_t start = 0;
    uint32_t end = 0;
    std::string name;
  };

  void init_disasm();
  void init_ftrace(const char *img_file, const char *elf_file);
  void load_func_symbols_32(FILE *fp, const Elf32_Ehdr &ehdr);
  const char *find_func_name(uint32_t addr) const;
  static std::string infer_elf_path(const char *img_file);

  bool itrace_enabled_ = false;
  bool mtrace_enabled_ = false;
  bool ftrace_enabled_ = false;
  int ftrace_depth_ = 0;
  std::string img_file_;
  std::string elf_file_;

  void *capstone_handle_ = nullptr;
  size_t (*cs_disasm_dl_)(uintptr_t, const uint8_t *, size_t, uint64_t, size_t, void **) = nullptr;
  void (*cs_free_dl_)(void *, size_t) = nullptr;
  uintptr_t cs_handle_ = 0;
  std::vector<FuncSymbol> func_symbols_;
};

}  // namespace npc
