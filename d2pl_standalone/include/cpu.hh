#pragma once

#include <sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.hh"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>

#define CPUID(INFO, LEAF, SUBLEAF) \
  __cpuid_count(LEAF, SUBLEAF, INFO[0], INFO[1], INFO[2], INFO[3])

#define GETCPU(CPU)                                                 \
  {                                                                 \
    uint32_t CPUInfo[4];                                            \
    CPUID(CPUInfo, 1, 0);                                           \
    /* CPUInfo[1] is EBX, bits 24-31 are APIC ID */                 \
    if ((CPUInfo[3] & (1 << 9)) == 0) {                             \
      CPU = -1; /* no APIC on chip */                               \
    } else {                                                        \
      CPU = (unsigned)CPUInfo[1] >> 24;                             \
    }                                                               \
    if (CPU < 0) CPU = 0;                                           \
  }
#else
#define GETCPU(CPU) \
  {                \
    CPU = 0;       \
  }
#endif

#ifdef Linux
static void setThreadAffinity(const int myid) {
  pid_t pid = syscall(SYS_gettid);
  cpu_set_t cpu_set;

  CPU_ZERO(&cpu_set);
  CPU_SET(myid % sysconf(_SC_NPROCESSORS_CONF), &cpu_set);

  if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpu_set) != 0) ERR;

  return;
}
#endif  // Linux

inline int cached_sched_getcpu() {
#ifdef Linux
    thread_local int value = ::sched_getcpu();
    return value;
#else
    return 0;
#endif
}
