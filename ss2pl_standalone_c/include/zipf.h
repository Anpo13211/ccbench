#pragma once

#include <stdint.h>

#include "rng.h"

typedef struct Zipf {
  double *cdf;
  uint64_t n;
  double theta;
} Zipf;

int zipf_init(Zipf *zipf, uint64_t n, double theta);
void zipf_free(Zipf *zipf);
uint64_t zipf_next(Zipf *zipf, XorShift64 *rng);
