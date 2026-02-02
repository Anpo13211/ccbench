#pragma once

#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__)

[[maybe_unused]] static uint64_t
rdtsc() {
  uint64_t rax;
  uint64_t rdx;

  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return (rdx << 32) | rax;
}

[[maybe_unused]] static uint64_t
rdtsc_serial() {
  uint64_t rax;
  uint64_t rdx;

  asm volatile("cpuid":: : "rax", "rbx", "rcx", "rdx");
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));

  return (rdx << 32) | rax;
}

[[maybe_unused]] static uint64_t
rdtscp() {
  uint64_t rax;
  uint64_t rdx;
  uint32_t aux;
  asm volatile("rdtscp" : "=a"(rax), "=d"(rdx), "=c"(aux)::);
  return (rdx << 32) | rax;
}

#else

#include <chrono>

[[maybe_unused]] static uint64_t
rdtsc() {
  return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

[[maybe_unused]] static uint64_t
rdtsc_serial() {
  return rdtsc();
}

[[maybe_unused]] static uint64_t
rdtscp() {
  return rdtsc();
}

#endif
