#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  Zipf zipf;
  XorShift64 rng;
} WorkerCtx;

static void *worker_main(void *arg) {
  WorkerCtx *ctx = (WorkerCtx *)arg;
  Transaction tx;
  tx_init(&tx, ctx->thid, ctx->result, ctx->quit, g_cfg.ycsb_max_ope);

  rng_init(&ctx->rng, (uint64_t)(ctx->thid + 1) * 0x9e3779b97f4a7c15ull);
  zipf_init(&ctx->zipf, g_cfg.ycsb_tuple_num, g_cfg.ycsb_zipf_skew);

  while (!atomic_load_explicit(ctx->start, memory_order_acquire)) {
    cpu_relax();
  }

  char tmp_val[VAL_SIZE];
  while (!atomic_load_explicit(ctx->quit, memory_order_acquire)) {
    tx_reset(&tx);
    make_procedure(tx.pro_set, g_cfg.ycsb_max_ope, &g_cfg, &ctx->zipf, &ctx->rng);

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

  for (uint64_t i = 0; i < g_cfg.thread_num; ++i) {
    result_init(&results[i]);
    ctxs[i].thid = (int)i;
    ctxs[i].result = &results[i];
    ctxs[i].start = &start;
    ctxs[i].quit = &quit;
    pthread_create(&threads[i], NULL, worker_main, &ctxs[i]);
  }

  atomic_store_explicit(&start, true, memory_order_release);
  for (uint64_t i = 0; i < g_cfg.extime; ++i) {
    sleep_ms(1000);
  }
  atomic_store_explicit(&quit, true, memory_order_release);

  for (uint64_t i = 0; i < g_cfg.thread_num; ++i) {
    pthread_join(threads[i], NULL);
  }

  Result total;
  result_init(&total);
  for (uint64_t i = 0; i < g_cfg.thread_num; ++i) {
    result_add(&total, &results[i]);
  }
  result_print(&total, g_cfg.extime, g_cfg.thread_num);

  free(results);
  free(threads);
  free(ctxs);
  free(Table);
  return 0;
}
