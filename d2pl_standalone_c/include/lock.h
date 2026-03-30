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
  /* Count only writers that will actually wait. */
  atomic_int waiting_writers;
} RWLock;

static inline void rwlock_init(RWLock *lock) {
  atomic_store_explicit(&lock->counter, 0, memory_order_release);
  atomic_store_explicit(&lock->waiting_writers, 0, memory_order_release);
}

static inline int rwlock_reader_should_yield(const RWLock *lock) {
#if defined(DLR1)
  (void)lock;
  return 0;
#else
  return atomic_load_explicit(&lock->waiting_writers, memory_order_acquire) > 0;
#endif
}

static inline void rwlock_r_lock(RWLock *lock) {
  for (;;) {
    int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
    if (expected == -1 || rwlock_reader_should_yield(lock)) {
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
  for (;;) {
    int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
    if (expected == -1 || rwlock_reader_should_yield(lock)) return 0;
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
#if !defined(DLR1)
  atomic_fetch_add_explicit(&lock->waiting_writers, 1, memory_order_acq_rel);
#endif
  for (;;) {
    int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
    if (expected != 0) {
      cpu_relax();
      continue;
    }
    if (atomic_compare_exchange_weak_explicit(
            &lock->counter, &expected, -1,
            memory_order_acq_rel, memory_order_acquire)) {
#if !defined(DLR1)
      atomic_fetch_sub_explicit(&lock->waiting_writers, 1, memory_order_acq_rel);
#endif
      return;
    }
  }
}

static inline int rwlock_w_trylock(RWLock *lock) {
  int expected = atomic_load_explicit(&lock->counter, memory_order_acquire);
  if (expected != 0) return 0;
  int ok = atomic_compare_exchange_strong_explicit(
      &lock->counter, &expected, -1,
      memory_order_acq_rel, memory_order_acquire);
  return ok;
}

static inline void rwlock_w_unlock(RWLock *lock) {
  atomic_store_explicit(&lock->counter, 0, memory_order_release);
}

static inline int rwlock_tryupgrade(RWLock *lock) {
  int expected = 1;
  int ok = atomic_compare_exchange_strong_explicit(
      &lock->counter, &expected, -1,
      memory_order_acq_rel, memory_order_acquire);
  return ok;
}
