#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Config {
  uint64_t clocks_per_us;
  uint64_t extime;
  uint64_t max_ope;
  bool rmw;
  uint64_t rratio;
  uint64_t thread_num;
  uint64_t tuple_num;
  bool ycsb;
  double zipf_skew;

  bool ycsb_rmw;
  uint64_t ycsb_max_ope;
  uint64_t ycsb_rratio;
  uint64_t ycsb_tuple_num;
  double ycsb_zipf_skew;
} Config;

extern Config g_cfg;

void config_set_defaults(Config *cfg);
void config_sync_ycsb(Config *cfg);
