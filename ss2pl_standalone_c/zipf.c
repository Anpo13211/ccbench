#include "include/zipf.h"

#include <math.h>
#include <stdlib.h>

static int zipf_build_cdf(Zipf *zipf) {
  if (zipf->n == 0) return -1;
  zipf->cdf = (double *)calloc(zipf->n, sizeof(double));
  if (!zipf->cdf) return -1;

  double sum = 0.0;
  for (uint64_t i = 0; i < zipf->n; ++i) {
    double w = 1.0;
    if (zipf->theta > 0.0) {
      w = 1.0 / pow((double)(i + 1), zipf->theta);
    }
    sum += w;
    zipf->cdf[i] = sum;
  }
  if (sum == 0.0) return -1;
  for (uint64_t i = 0; i < zipf->n; ++i) {
    zipf->cdf[i] /= sum;
  }
  return 0;
}

int zipf_init(Zipf *zipf, uint64_t n, double theta) {
  zipf->cdf = NULL;
  zipf->n = n;
  zipf->theta = theta;
  return zipf_build_cdf(zipf);
}

void zipf_free(Zipf *zipf) {
  if (zipf->cdf) {
    free(zipf->cdf);
    zipf->cdf = NULL;
  }
}

uint64_t zipf_next(Zipf *zipf, XorShift64 *rng) {
  if (!zipf->cdf || zipf->n == 0) return 0;
  double u = rng_next_double(rng);
  uint64_t lo = 0;
  uint64_t hi = zipf->n - 1;
  while (lo < hi) {
    uint64_t mid = lo + (hi - lo) / 2;
    if (u <= zipf->cdf[mid]) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}
