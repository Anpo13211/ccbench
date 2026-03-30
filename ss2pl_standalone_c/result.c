#include "include/result.h"

#include <math.h>
#include <stdio.h>

void result_init(Result *res) {
  res->commit_count = 0;
  res->abort_count = 0;
}

void result_add(Result *dst, const Result *src) {
  dst->commit_count += src->commit_count;
  dst->abort_count += src->abort_count;
}

void result_print(const Result *total, double elapsed_sec, uint64_t thread_num) {
  uint64_t commits = total->commit_count;
  uint64_t aborts = total->abort_count;
  double abort_rate = 0.0;
  if (commits + aborts > 0) {
    abort_rate = (double)aborts / (double)(commits + aborts);
  }
  double tps = elapsed_sec > 0.0 ? (double)commits / elapsed_sec : 0.0;
  double latency =
      commits ? (elapsed_sec * 1e9 * (double)thread_num) / (double)commits : 0.0;

  printf("abort_counts_:\t%llu\n", (unsigned long long)aborts);
  printf("commit_counts_:\t%llu\n", (unsigned long long)commits);
  printf("abort_rate:\t%.4f\n", abort_rate);
  printf("latency[ns]:\t%.4f\n", latency);
  printf("throughput[tps]:\t%.4f\n", tps);
}
