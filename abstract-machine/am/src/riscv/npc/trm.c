#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

extern char _heap_start;
int main(const char *args);

extern char _pmem_start;
#define SERIAL_PORT 0x10000000u
#define PMEM_SIZE (128 * 1024 * 1024)
#define PMEM_END  ((uintptr_t)&_pmem_start + PMEM_SIZE)

Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

static void print_hex32(uint32_t x) {
  static const char lut[] = "0123456789abcdef";
  for (int i = 7; i >= 0; i--) {
    putch(lut[(x >> (i * 4)) & 0xf]);
  }
}

static void print_str(const char *s) {
  while (*s != '\0') {
    putch(*s++);
  }
}

static void print_u32(uint32_t x) {
  char buf[16];
  int len = 0;
  if (x == 0) {
    putch('0');
    return;
  }
  while (x > 0) {
    buf[len++] = '0' + (x % 10);
    x /= 10;
  }
  while (len > 0) {
    putch(buf[--len]);
  }
}

void putch(char ch) {
  *(volatile uint8_t *)SERIAL_PORT = (uint8_t)ch;
}

void halt(int code) {
  asm volatile(
    "mv a0, %0\n\t"
    "ebreak\n\t"
    :
    : "r"(code)
    : "a0"
  );
  while (1) { }
}

void _trm_init() {
  uint32_t mvendorid = 0, marchid = 0;
  asm volatile("csrr %0, mvendorid" : "=r"(mvendorid));
  asm volatile("csrr %0, marchid"   : "=r"(marchid));
  print_str("mvendorid = 0x");
  print_hex32(mvendorid);
  print_str(", marchid = ");
  print_u32(marchid);
  putch('\n');
  int ret = main(mainargs);
  halt(ret);
}
