#pragma once

#include <cstddef>
#include <cstdint>

namespace npc {

struct CPUState {
  uint32_t gpr[32] = {};
  uint32_t pc = 0;
};

class DiffTest {
public:
  explicit DiffTest(const char *ref_so);
  ~DiffTest();

  void init(void *mem, unsigned img_size, const CPUState &boot_state);
  void step(const CPUState &dut);

private:
  void *handle_ = nullptr;
  const char *ref_so_ = nullptr;

  using memcpy_func_t = void (*)(uint32_t, void *, size_t, bool);
  using regcpy_func_t = void (*)(void *, bool);
  using exec_func_t = void (*)(uint64_t);
  using init_func_t = void (*)(int);

  memcpy_func_t memcpy_ = nullptr;
  regcpy_func_t regcpy_ = nullptr;
  exec_func_t exec_ = nullptr;
  init_func_t init_ = nullptr;
};

}  // namespace npc
