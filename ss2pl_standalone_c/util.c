#include "include/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "include/lock.h"

Config g_cfg;
Tuple *Table = NULL;

void config_set_defaults(Config *cfg) {
  cfg->clocks_per_us = 2100;
  cfg->extime = 3;
  cfg->max_ope = 10;
  cfg->rmw = false;
  cfg->rratio = 50;
  cfg->thread_num = 10;
  cfg->tuple_num = 1000000;
  cfg->ycsb = true;
  cfg->zipf_skew = 0.0;

  cfg->ycsb_rmw = false;
  cfg->ycsb_max_ope = 10;
  cfg->ycsb_rratio = 50;
  cfg->ycsb_tuple_num = 1000000;
  cfg->ycsb_zipf_skew = 0.0;
}

void config_sync_ycsb(Config *cfg) {
  cfg->tuple_num = cfg->ycsb_tuple_num;
  cfg->rratio = cfg->ycsb_rratio;
  cfg->max_ope = cfg->ycsb_max_ope;
  cfg->rmw = cfg->ycsb_rmw;
  cfg->zipf_skew = cfg->ycsb_zipf_skew;
}

static int parse_bool(const char *val, int *out) {
  if (!val) {
    *out = 1;
    return 1;
  }
  if (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
      strcasecmp(val, "t") == 0 || strcasecmp(val, "yes") == 0 ||
      strcasecmp(val, "y") == 0) {
    *out = 1;
    return 1;
  }
  if (strcmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "f") == 0 || strcasecmp(val, "no") == 0 ||
      strcasecmp(val, "n") == 0) {
    *out = 0;
    return 1;
  }
  return 0;
}

static int parse_u64(const char *val, uint64_t *out) {
  if (!val) return 0;
  errno = 0;
  char *end = NULL;
  unsigned long long v = strtoull(val, &end, 10);
  if (errno != 0 || end == val || *end != '\0') return 0;
  *out = (uint64_t)v;
  return 1;
}

static int parse_double(const char *val, double *out) {
  if (!val) return 0;
  errno = 0;
  char *end = NULL;
  double v = strtod(val, &end);
  if (errno != 0 || end == val || *end != '\0') return 0;
  *out = v;
  return 1;
}

static void set_flag(const char *name, const char *value, int has_value) {
  if (strcmp(name, "clocks_per_us") == 0) {
    parse_u64(value, &g_cfg.clocks_per_us);
  } else if (strcmp(name, "extime") == 0) {
    parse_u64(value, &g_cfg.extime);
  } else if (strcmp(name, "max_ope") == 0) {
    parse_u64(value, &g_cfg.max_ope);
  } else if (strcmp(name, "rmw") == 0) {
    int b = 0;
    if (parse_bool(value, &b)) g_cfg.rmw = b;
  } else if (strcmp(name, "rratio") == 0) {
    parse_u64(value, &g_cfg.rratio);
  } else if (strcmp(name, "thread_num") == 0) {
    parse_u64(value, &g_cfg.thread_num);
  } else if (strcmp(name, "tuple_num") == 0) {
    parse_u64(value, &g_cfg.tuple_num);
  } else if (strcmp(name, "ycsb") == 0) {
    int b = 0;
    if (parse_bool(value, &b)) g_cfg.ycsb = b;
  } else if (strcmp(name, "zipf_skew") == 0) {
    parse_double(value, &g_cfg.zipf_skew);
  } else if (strcmp(name, "ycsb_rmw") == 0) {
    int b = 0;
    if (parse_bool(value, &b)) g_cfg.ycsb_rmw = b;
  } else if (strcmp(name, "ycsb_max_ope") == 0) {
    parse_u64(value, &g_cfg.ycsb_max_ope);
  } else if (strcmp(name, "ycsb_rratio") == 0) {
    parse_u64(value, &g_cfg.ycsb_rratio);
  } else if (strcmp(name, "ycsb_tuple_num") == 0) {
    parse_u64(value, &g_cfg.ycsb_tuple_num);
  } else if (strcmp(name, "ycsb_zipf_skew") == 0) {
    parse_double(value, &g_cfg.ycsb_zipf_skew);
  } else if (strcmp(name, "help") == 0) {
    printf("SS2PL standalone C options:\n");
    printf("  --thread_num=N\n");
    printf("  --extime=N\n");
    printf("  --ycsb_tuple_num=N\n");
    printf("  --ycsb_max_ope=N\n");
    printf("  --ycsb_rratio=N\n");
    printf("  --ycsb_zipf_skew=F\n");
    exit(0);
  } else {
    (void)has_value;
  }
}

void parse_args(int argc, char **argv, Config *cfg) {
  (void)cfg;
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (strncmp(arg, "--", 2) != 0) continue;
    arg += 2;
    const char *eq = strchr(arg, '=');
    const char *name = arg;
    const char *value = NULL;
    char name_buf[128];
    if (eq) {
      size_t len = (size_t)(eq - arg);
      if (len >= sizeof(name_buf)) len = sizeof(name_buf) - 1;
      memcpy(name_buf, arg, len);
      name_buf[len] = '\0';
      name = name_buf;
      value = eq + 1;
      set_flag(name, value, 1);
      continue;
    }

    if (strncmp(name, "no", 2) == 0) {
      set_flag(name + 2, "0", 1);
      continue;
    }

    if (i + 1 < argc && argv[i + 1][0] != '-') {
      value = argv[++i];
      set_flag(name, value, 1);
    } else {
      set_flag(name, "1", 1);
    }
  }
}

void print_config(const Config *cfg) {
  printf("#FLAGS_clocks_per_us:\t%llu\n", (unsigned long long)cfg->clocks_per_us);
  printf("#FLAGS_extime:\t\t%llu\n", (unsigned long long)cfg->extime);
  printf("#FLAGS_max_ope:\t\t%llu\n", (unsigned long long)cfg->max_ope);
  printf("#FLAGS_rmw:\t\t%d\n", cfg->rmw ? 1 : 0);
  printf("#FLAGS_rratio:\t\t%llu\n", (unsigned long long)cfg->rratio);
  printf("#FLAGS_thread_num:\t%llu\n", (unsigned long long)cfg->thread_num);
  printf("#FLAGS_tuple_num:\t%llu\n", (unsigned long long)cfg->tuple_num);
  printf("#FLAGS_ycsb:\t\t%d\n", cfg->ycsb ? 1 : 0);
  printf("#FLAGS_zipf_skew:\t%.3f\n", cfg->zipf_skew);

  printf("#FLAGS_ycsb_max_ope:\t%llu\n", (unsigned long long)cfg->ycsb_max_ope);
  printf("#FLAGS_ycsb_rmw:\t%d\n", cfg->ycsb_rmw ? 1 : 0);
  printf("#FLAGS_ycsb_rratio:\t%llu\n", (unsigned long long)cfg->ycsb_rratio);
  printf("#FLAGS_ycsb_tuple_num:\t%llu\n", (unsigned long long)cfg->ycsb_tuple_num);
  printf("#FLAGS_ycsb_zipf_skew:\t%.3f\n", cfg->ycsb_zipf_skew);
}

void validate_config(const Config *cfg) {
  if (cfg->ycsb_rratio > 100) {
    fprintf(stderr, "ycsb_rratio must be <= 100\n");
    exit(1);
  }
  if (cfg->ycsb_zipf_skew >= 1.0) {
    fprintf(stderr, "ycsb_zipf_skew must be < 1.0\n");
    exit(1);
  }
  if (cfg->ycsb_tuple_num == 0) {
    fprintf(stderr, "ycsb_tuple_num must be >= 1\n");
    exit(1);
  }
}

void make_db(const Config *cfg) {
  Table = (Tuple *)calloc(cfg->ycsb_tuple_num, sizeof(Tuple));
  if (!Table) {
    fprintf(stderr, "failed to allocate table\n");
    exit(1);
  }
  for (uint64_t i = 0; i < cfg->ycsb_tuple_num; ++i) {
    rwlock_init(&Table[i].lock);
    atomic_store_explicit(&Table[i].present, true, memory_order_release);
    Table[i].id = i;
    memset(Table[i].val, 'a', VAL_SIZE);
  }
}

void make_procedure(Op *ops, size_t max_ope, const Config *cfg, Zipf *zipf, XorShift64 *rng) {
  for (size_t i = 0; i < max_ope; ++i) {
    uint64_t key = zipf_next(zipf, rng) % cfg->ycsb_tuple_num;
    uint64_t coin = rng_next(rng) % 100;
    if (coin < cfg->ycsb_rratio) {
      ops[i].type = OP_READ;
    } else {
      ops[i].type = cfg->ycsb_rmw ? OP_RMW : OP_WRITE;
    }
    ops[i].key = key;
  }
}

void sleep_ms(uint64_t ms) {
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000);
  ts.tv_nsec = (long)((ms % 1000) * 1000000ull);
  nanosleep(&ts, NULL);
}
