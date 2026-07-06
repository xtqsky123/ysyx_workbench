#include <am.h>

static uint64_t read_time() {
  uint32_t lo = 0, hi = 0;
  asm volatile("csrr %0, mcycle"  : "=r"(lo));
  asm volatile("csrr %0, mcycleh" : "=r"(hi));
  return ((uint64_t)hi << 32) | lo;
}

static uint64_t boot_time = 0;

void __am_timer_init() {
  boot_time = read_time();
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  uptime->us = read_time() - boot_time;
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 1;
  rtc->month  = 1;
  rtc->year   = 1900;
}
