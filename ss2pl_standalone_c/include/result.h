#pragma once

#include <stdint.h>

typedef struct Result {
  uint64_t commit_count;
  uint64_t abort_count;
} Result;

void result_init(Result *res);
void result_add(Result *dst, const Result *src);
void result_print(const Result *total, double elapsed_sec, uint64_t thread_num);
