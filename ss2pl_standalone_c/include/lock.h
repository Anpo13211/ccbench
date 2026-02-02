#pragma once

#include <stdatomic.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
static inline void cpu_relax(void) { _mm_pause(); }
#elif defined(__aarch64__) || defined(__arm__)
static inline void cpu_relax(void) { __asm__ __volatile__("yield"); }
#else
#include <sched.h>
static inline void cpu_relax(void) { sched_yield(); }
#endif

typedef struct RWLock {
  atomic_int counter;
} RWLock;

static inline void rwlock_init(RWLock *lock) {
  atomic_store_explicit(&lock->counter, 0, memory_order_release);
}

static inline void rwlock_r_lock(RWLock *lock) {
  for (;;) {
    int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
    if (expected == -1) {
      cpu_relax();
      continue;
    }
    int desired = expected + 1;
    if (atomic_compare_exchange_weak_explicit(
            &lock->counter, &expected, desired,
            memory_order_acq_rel, memory_order_acquire)) {
      return;
    }
  }
}

static inline int rwlock_r_trylock(RWLock *lock) {
  int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
  for (;;) {
    if (expected == -1) return 0;
    int desired = expected + 1;
    if (atomic_compare_exchange_weak_explicit(
            &lock->counter, &expected, desired,
            memory_order_acq_rel, memory_order_acquire)) {
      return 1;
    }
  }
}

static inline void rwlock_r_unlock(RWLock *lock) {
  atomic_fetch_sub_explicit(&lock->counter, 1, memory_order_release);
}

static inline void rwlock_w_lock(RWLock *lock) {
  for (;;) {
    int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
    if (expected != 0) {
      cpu_relax();
      continue;
    }
    if (atomic_compare_exchange_weak_explicit(
            &lock->counter, &expected, -1,
            memory_order_acq_rel, memory_order_acquire)) {
      return;
    }
  }
}

static inline int rwlock_w_trylock(RWLock *lock) {
  int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
  if (expected != 0) return 0;
  return atomic_compare_exchange_strong_explicit(
      &lock->counter, &expected, -1,
      memory_order_acq_rel, memory_order_acquire);
}

static inline void rwlock_w_unlock(RWLock *lock) {
  atomic_store_explicit(&lock->counter, 0, memory_order_release);
}

static inline int rwlock_tryupgrade(RWLock *lock) {
  int expected = 1;
  return atomic_compare_exchange_strong_explicit(
      &lock->counter, &expected, -1,
      memory_order_acq_rel, memory_order_acquire);
}
