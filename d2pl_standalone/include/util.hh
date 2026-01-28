#pragma once

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "atomic_wrapper.hh"
#include "debug.hh"
#include "procedure.hh"
#include "tsc.hh"

// function declarations
extern void chkArg();

extern void displayParameter();

extern void makeDB();

extern void partTableInit([[maybe_unused]] size_t thid, uint64_t start, uint64_t end);

extern void ShowOptParameters();

[[maybe_unused]] extern bool chkSpan(struct timeval &start, struct timeval &stop, long threshold);

extern size_t decideParallelBuildNumber(size_t tuple_num);

extern void displayRusageRUMaxrss();

extern bool isReady(const std::vector<char> &readys);

extern void waitForReady(const std::vector<char> &readys);

extern void sleepMs(size_t ms);

extern void sleepMicroSec(size_t ms);

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
  _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield");
#else
  std::this_thread::yield();
#endif
}

// inline utilities
[[maybe_unused]] inline static bool chkClkSpan(const uint64_t start,
                                               const uint64_t stop,
                                               const uint64_t threshold) {
  uint64_t diff = 0;
  diff = stop - start;
  if (diff > threshold)
    return true;
  else
    return false;
}

[[maybe_unused]] inline static bool chkClkSpanSec(
        const uint64_t start, const uint64_t stop, const unsigned int clocks_per_us,
        const uint64_t sec) {
  uint64_t diff = 0;
  diff = stop - start;
  diff = diff / clocks_per_us / 1000 / 1000;
  if (diff > sec)
    return true;
  else
    return false;
}

template<typename Int>
Int byteswap(Int in) {
  switch (sizeof(Int)) {
    case 1:
      return in;
    case 2:
      return __builtin_bswap16(in);
    case 4:
      return __builtin_bswap32(in);
    case 8:
      return __builtin_bswap64(in);
    default:
      assert(false);
  }
}

template<typename Int>
void assign_as_bigendian(Int value, char *out) {
  Int tmp = byteswap(value);
  ::memcpy(out, &tmp, sizeof(tmp));
}

template<typename Int>
void parse_bigendian(const char *in, Int &out) {
  Int tmp;
  ::memcpy(&tmp, in, sizeof(tmp));
  out = byteswap(tmp);
}

template<typename T>
std::string_view struct_str_view(const T &t) {
  return std::string_view(reinterpret_cast<const char *>(&t), sizeof(t));
}

inline std::string str_view_hex(std::string_view sv) {
  std::stringstream ss;
  char buf[3];
  for (uint8_t i : sv) {
    ::snprintf(buf, sizeof(buf), "%02x", i);
    ss << buf;
  }
  return ss.str();
}
