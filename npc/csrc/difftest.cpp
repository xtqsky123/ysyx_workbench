#include "difftest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace npc {

DiffTest::DiffTest(const char *ref_so) : ref_so_(ref_so) {}

DiffTest::~DiffTest() {
  if (handle_ != nullptr) {
    dlclose(handle_);
  }
}

void DiffTest::init(void *mem, unsigned img_size, const CPUState &boot_state) {
  if (ref_so_ == nullptr || ref_so_[0] == '\0') {
    return;
  }

  handle_ = dlopen(ref_so_, RTLD_LAZY);
  if (handle_ == nullptr) {
    std::fprintf(stderr, "Failed to open DiffTest ref %s: %s\n", ref_so_, dlerror());
    std::exit(1);
  }

  memcpy_ = reinterpret_cast<memcpy_func_t>(dlsym(handle_, "difftest_memcpy"));
  regcpy_ = reinterpret_cast<regcpy_func_t>(dlsym(handle_, "difftest_regcpy"));
  exec_ = reinterpret_cast<exec_func_t>(dlsym(handle_, "difftest_exec"));
  init_ = reinterpret_cast<init_func_t>(dlsym(handle_, "difftest_init"));

  if (memcpy_ == nullptr || regcpy_ == nullptr || exec_ == nullptr || init_ == nullptr) {
    std::fprintf(stderr, "Incomplete DiffTest symbols in %s\n", ref_so_);
    std::exit(1);
  }

  init_(0);
  memcpy_(0x80000000u, mem, img_size, true);
  regcpy_(const_cast<CPUState *>(&boot_state), true);
}

void DiffTest::step(const CPUState &dut) {
  if (handle_ == nullptr) {
    return;
  }

  CPUState ref = {};
  exec_(1);
  regcpy_(&ref, false);

  for (int i = 0; i < 32; i++) {
    if (ref.gpr[i] != dut.gpr[i]) {
      std::fprintf(stderr,
          "DiffTest mismatch at x%d: ref=0x%08x dut=0x%08x pc=0x%08x\n",
          i, ref.gpr[i], dut.gpr[i], dut.pc);
      std::exit(1);
    }
  }

  if (ref.pc != dut.pc) {
    std::fprintf(stderr, "DiffTest mismatch at pc: ref=0x%08x dut=0x%08x\n", ref.pc, dut.pc);
    std::exit(1);
  }
}

}  // namespace npc
