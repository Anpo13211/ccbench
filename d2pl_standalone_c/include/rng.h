#pragma once

#include <stdint.h>

typedef struct XorShift64 {
  uint64_t state;
} XorShift64;

static inline void rng_init(XorShift64 *rng, uint64_t seed) {
  rng->state = seed ? seed : 88172645463325252ull;
}

static inline uint64_t rng_next(XorShift64 *rng) {
  uint64_t x = rng->state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  rng->state = x;
  return x;
}

static inline double rng_next_double(XorShift64 *rng) {
  return (rng_next(rng) >> 11) * (1.0 / 9007199254740992.0);
}
