#define _POSIX_C_SOURCE 199309L

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "include/config.h"
#include "include/lock.h"
#include "include/result.h"
#include "include/transaction.h"
#include "include/util.h"
#include "include/zipf.h"

typedef struct WorkerCtx {
  int thid;
  Result *result;
  atomic_bool *start;
  atomic_bool *quit;
  atomic_uint_fast64_t *ready_count;
  Zipf zipf;
  XorShift64 rng;
} WorkerCtx;

static double monotonic_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void *worker_main(void *arg) {
  WorkerCtx *ctx = (WorkerCtx *)arg;
  Transaction tx;
  tx_init(&tx, ctx->thid, ctx->result, ctx->quit, g_cfg.ycsb_max_ope);

  rng_init(&ctx->rng, (uint64_t)(ctx->thid + 1) * 0x9e3779b97f4a7c15ull);
  zipf_init(&ctx->zipf, g_cfg.ycsb_tuple_num, g_cfg.ycsb_zipf_skew);

  atomic_fetch_add_explicit(ctx->ready_count, 1, memory_order_release);
  while (!atomic_load_explicit(ctx->start, memory_order_acquire)) {
    cpu_relax();
  }

  char tmp_val[VAL_SIZE];
  while (!atomic_load_explicit(ctx->quit, memory_order_acquire)) {
    tx_reset(&tx);
    make_procedure(tx.pro_set, g_cfg.ycsb_max_ope, &g_cfg, &ctx->zipf, &ctx->rng);

    tx_build_locklist(&tx);
    if (!tx_locklist(&tx)) {
      tx_abort(&tx);
      continue;
    }

    for (size_t i = 0; i < g_cfg.ycsb_max_ope; ++i) {
      Op *op = &tx.pro_set[i];
      int rc = 0;
      if (op->type == OP_READ) {
        rc = tx_read(&tx, op->key, tmp_val);
      } else if (op->type == OP_WRITE) {
        memset(tmp_val, 'b', VAL_SIZE);
        rc = tx_write(&tx, op->key, tmp_val);
      } else {
        rc = tx_read(&tx, op->key, tmp_val);
        if (rc == 0) {
          rc = tx_write(&tx, op->key, tmp_val);
        }
      }
      if (rc != 0 || tx.status == TX_ABORTED) {
        tx_abort(&tx);
        goto retry;
      }
    }
    tx_commit(&tx);
    ctx->result->commit_count++;
    continue;

retry:
    continue;
  }

  zipf_free(&ctx->zipf);
  tx_destroy(&tx);
  return NULL;
}

int main(int argc, char **argv) {
  config_set_defaults(&g_cfg);
  parse_args(argc, argv, &g_cfg);
  config_sync_ycsb(&g_cfg);
  validate_config(&g_cfg);
  print_config(&g_cfg);

  make_db(&g_cfg);

  Result *results = (Result *)calloc(g_cfg.thread_num, sizeof(Result));
  pthread_t *threads = (pthread_t *)calloc(g_cfg.thread_num, sizeof(pthread_t));
  WorkerCtx *ctxs = (WorkerCtx *)calloc(g_cfg.thread_num, sizeof(WorkerCtx));

  atomic_bool start = ATOMIC_VAR_INIT(false);
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  atomic_uint_fast64_t ready_count = ATOMIC_VAR_INIT(0);

  for (uint64_t i = 0; i < g_cfg.thread_num; ++i) {
    result_init(&results[i]);
    ctxs[i].thid = (int)i;
    ctxs[i].result = &results[i];
    ctxs[i].start = &start;
    ctxs[i].quit = &quit;
    ctxs[i].ready_count = &ready_count;
    pthread_create(&threads[i], NULL, worker_main, &ctxs[i]);
  }

  while (atomic_load_explicit(&ready_count, memory_order_acquire) < g_cfg.thread_num) {
    cpu_relax();
  }
  double start_sec = monotonic_seconds();
  atomic_store_explicit(&start, true, memory_order_release);
  for (uint64_t i = 0; i < g_cfg.extime; ++i) {
    sleep_ms(1000);
  }
  atomic_store_explicit(&quit, true, memory_order_release);

  for (uint64_t i = 0; i < g_cfg.thread_num; ++i) {
    pthread_join(threads[i], NULL);
  }
  double elapsed_sec = monotonic_seconds() - start_sec;

  Result total;
  result_init(&total);
  for (uint64_t i = 0; i < g_cfg.thread_num; ++i) {
    result_add(&total, &results[i]);
  }
  result_print(&total, elapsed_sec, g_cfg.thread_num);

  free(results);
  free(threads);
  free(ctxs);
  free(Table);
  return 0;
}
