#define _POSIX_C_SOURCE 199309L

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "include/config.h"
#include "include/storage.h"
#include "include/transaction.h"

Config g_cfg;
Tuple *Table = NULL;

static void init_table(uint64_t tuple_num) {
  g_cfg.tuple_num = tuple_num;
  g_cfg.ycsb_tuple_num = tuple_num;
  Table = (Tuple *)calloc(tuple_num, sizeof(Tuple));
  if (!Table) {
    fprintf(stderr, "failed to allocate table\n");
    exit(1);
  }
  for (uint64_t i = 0; i < tuple_num; ++i) {
    rwlock_init(&Table[i].lock);
    atomic_store_explicit(&Table[i].present, true, memory_order_release);
    Table[i].id = i;
    memset(Table[i].val, 'a', VAL_SIZE);
  }
}

static void reset_table(void) {
  free(Table);
  Table = NULL;
}

static void set_tuple_present(uint64_t key, char fill) {
  atomic_store_explicit(&Table[key].present, true, memory_order_release);
  memset(Table[key].val, fill, VAL_SIZE);
}

static void set_tuple_absent(uint64_t key) {
  atomic_store_explicit(&Table[key].present, false, memory_order_release);
  memset(Table[key].val, 0, VAL_SIZE);
}

static void fill_val(char out[VAL_SIZE], char fill) {
  memset(out, fill, VAL_SIZE);
}

#ifdef DLR0
static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
  return (uint64_t)now.tv_sec * 1000ull + (uint64_t)now.tv_nsec / 1000000ull;
}

static int wait_until_bool(const atomic_bool *flag, uint64_t timeout_ms) {
  uint64_t start_ms = monotonic_ms();
  if (start_ms == 0) return atomic_load_explicit(flag, memory_order_acquire);

  for (;;) {
    if (atomic_load_explicit(flag, memory_order_acquire)) return 1;
    if (monotonic_ms() - start_ms >= timeout_ms) return 0;
    struct timespec nap = {.tv_sec = 0, .tv_nsec = 1000000};
    nanosleep(&nap, NULL);
  }
}
static int wait_until_waiting_writers(const RWLock *lock, int expected, uint64_t timeout_ms) {
  uint64_t start_ms = monotonic_ms();
  if (start_ms == 0) {
    return atomic_load_explicit(&lock->waiting_writers, memory_order_acquire) >= expected;
  }

  for (;;) {
    if (atomic_load_explicit(&lock->waiting_writers, memory_order_acquire) >= expected) {
      return 1;
    }
    if (monotonic_ms() - start_ms >= timeout_ms) return 0;
    struct timespec nap = {.tv_sec = 0, .tv_nsec = 1000000};
    nanosleep(&nap, NULL);
  }
}
#endif

static int test_delete_then_write(void) {
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val[VAL_SIZE];
  char zero[VAL_SIZE] = {0};
  int rc = 0;

  init_table(1);
  tx_init(&tx, 0, &res, &quit, 1);
  memset(val, 'b', VAL_SIZE);

  if (tx_delete(&tx, 0) != 0) {
    fprintf(stderr, "tx_delete should succeed\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != -1) {
    fprintf(stderr, "tx_read should treat a pending delete as not found\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_write(&tx, 0, val) != -1) {
    fprintf(stderr, "tx_write should not resurrect a pending delete\n");
    rc = 1;
    goto cleanup;
  }
  tx_commit(&tx);
  if (atomic_load_explicit(&Table[0].present, memory_order_acquire)) {
    fprintf(stderr, "tuple should remain deleted after commit\n");
    rc = 1;
    goto cleanup;
  }
  if (memcmp(Table[0].val, zero, VAL_SIZE) != 0) {
    fprintf(stderr, "deleted tuple payload should be cleared\n");
    rc = 1;
    goto cleanup;
  }

cleanup:
  tx_destroy(&tx);
  reset_table();
  return rc;
}

static int test_insert_write_delete_reinsert(void) {
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val_b[VAL_SIZE];
  char val_c[VAL_SIZE];
  char val_d[VAL_SIZE];
  int rc = 0;

  init_table(1);
  set_tuple_absent(0);
  tx_init(&tx, 0, &res, &quit, 4);
  fill_val(val_b, 'b');
  fill_val(val_c, 'c');
  fill_val(val_d, 'd');

  if (tx_insert(&tx, 0, val_b) != 0) {
    fprintf(stderr, "tx_insert should succeed on an absent tuple\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, val_b, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should see a pending insert\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_write(&tx, 0, val_c) != 0) {
    fprintf(stderr, "tx_write should update a pending insert\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, val_c, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should see the updated pending insert\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_delete(&tx, 0) != 0) {
    fprintf(stderr, "tx_delete should cancel a pending insert\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != -1) {
    fprintf(stderr, "tx_read should not find a canceled insert\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_insert(&tx, 0, val_d) != 0) {
    fprintf(stderr, "tx_insert should succeed again after canceling the insert\n");
    rc = 1;
    goto cleanup;
  }
  tx_commit(&tx);
  if (!atomic_load_explicit(&Table[0].present, memory_order_acquire) ||
      memcmp(Table[0].val, val_d, VAL_SIZE) != 0) {
    fprintf(stderr, "reinserted tuple should commit with the latest value\n");
    rc = 1;
    goto cleanup;
  }

cleanup:
  tx_destroy(&tx);
  reset_table();
  return rc;
}

static int test_read_write_upgrade(void) {
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val_b[VAL_SIZE];
  int rc = 0;

  init_table(1);
  set_tuple_present(0, 'a');
  tx_init(&tx, 0, &res, &quit, 2);
  fill_val(val_b, 'b');

  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, Table[0].val, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should succeed on a present tuple\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_write(&tx, 0, val_b) != 0) {
    fprintf(stderr, "tx_write should upgrade a prior read\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, val_b, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should see its own upgraded write\n");
    rc = 1;
    goto cleanup;
  }
  tx_commit(&tx);
  if (!atomic_load_explicit(&Table[0].present, memory_order_acquire) ||
      memcmp(Table[0].val, val_b, VAL_SIZE) != 0) {
    fprintf(stderr, "upgraded write should commit the new value\n");
    rc = 1;
    goto cleanup;
  }

cleanup:
  tx_destroy(&tx);
  reset_table();
  return rc;
}

static int test_delete_then_insert(void) {
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val_b[VAL_SIZE];
  int rc = 0;

  init_table(1);
  set_tuple_present(0, 'a');
  tx_init(&tx, 0, &res, &quit, 2);
  fill_val(val_b, 'b');

  if (tx_delete(&tx, 0) != 0) {
    fprintf(stderr, "tx_delete should succeed on a present tuple\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_insert(&tx, 0, val_b) != 0) {
    fprintf(stderr, "tx_insert should recreate a tuple deleted in the same transaction\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, val_b, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should see the recreated tuple\n");
    rc = 1;
    goto cleanup;
  }
  tx_commit(&tx);
  if (!atomic_load_explicit(&Table[0].present, memory_order_acquire) ||
      memcmp(Table[0].val, val_b, VAL_SIZE) != 0) {
    fprintf(stderr, "recreated tuple should commit with the inserted value\n");
    rc = 1;
    goto cleanup;
  }

cleanup:
  tx_destroy(&tx);
  reset_table();
  return rc;
}

static int test_delete_insert_delete_reinsert(void) {
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val_b[VAL_SIZE];
  char val_c[VAL_SIZE];
  int rc = 0;

  init_table(1);
  set_tuple_present(0, 'a');
  tx_init(&tx, 0, &res, &quit, 4);
  fill_val(val_b, 'b');
  fill_val(val_c, 'c');

  if (tx_delete(&tx, 0) != 0) {
    fprintf(stderr, "tx_delete should succeed on a present tuple\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_insert(&tx, 0, val_b) != 0) {
    fprintf(stderr, "tx_insert should recreate a tuple deleted in the same transaction\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_delete(&tx, 0) != 0) {
    fprintf(stderr, "tx_delete should restore the pending delete after a recreate\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != -1) {
    fprintf(stderr, "tx_read should not find a tuple after delete->insert->delete\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_insert(&tx, 0, val_c) != 0) {
    fprintf(stderr, "tx_insert should succeed again after delete->insert->delete\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, val_c, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should see the recreated tuple after the second insert\n");
    rc = 1;
    goto cleanup;
  }
  tx_commit(&tx);
  if (!atomic_load_explicit(&Table[0].present, memory_order_acquire) ||
      memcmp(Table[0].val, val_c, VAL_SIZE) != 0) {
    fprintf(stderr, "final insert should commit after delete->insert->delete\n");
    rc = 1;
    goto cleanup;
  }

cleanup:
  tx_destroy(&tx);
  reset_table();
  return rc;
}

#ifdef DLR0
typedef struct Dlr0WriterArgs {
  RWLock *lock;
  atomic_bool *acquired;
  atomic_bool *release;
} Dlr0WriterArgs;

static void *dlr0_writer_worker(void *arg) {
  Dlr0WriterArgs *args = (Dlr0WriterArgs *)arg;
  rwlock_w_lock(args->lock);
  atomic_store_explicit(args->acquired, true, memory_order_release);
  while (!atomic_load_explicit(args->release, memory_order_acquire)) {
    cpu_relax();
  }
  rwlock_w_unlock(args->lock);
  return NULL;
}

static int test_dlr0_queued_writer_blocks_new_readers(void) {
  RWLock lock;
  pthread_t writer_thread;
  atomic_bool writer_acquired = ATOMIC_VAR_INIT(false);
  atomic_bool writer_release = ATOMIC_VAR_INIT(false);
  Dlr0WriterArgs args = {
      .lock = &lock,
      .acquired = &writer_acquired,
      .release = &writer_release,
  };
  int rc = 0;

  rwlock_init(&lock);
  rwlock_r_lock(&lock);

  if (pthread_create(&writer_thread, NULL, dlr0_writer_worker, &args) != 0) {
    fprintf(stderr, "failed to start DLR0 queued-writer worker\n");
    rwlock_r_unlock(&lock);
    return 1;
  }

  if (!wait_until_waiting_writers(&lock, 1, 200)) {
    fprintf(stderr, "writer did not register as waiting in DLR0\n");
    rc = 1;
    goto cleanup;
  }

  if (rwlock_r_trylock(&lock)) {
    fprintf(stderr, "new readers should not bypass a queued writer in DLR0\n");
    rwlock_r_unlock(&lock);
    rc = 1;
    goto cleanup;
  }

  rwlock_r_unlock(&lock);
  if (!wait_until_bool(&writer_acquired, 200)) {
    fprintf(stderr, "queued writer did not acquire after readers drained in DLR0\n");
    rc = 1;
    goto cleanup;
  }

  if (rwlock_r_trylock(&lock)) {
    fprintf(stderr, "reader should not acquire while writer holds the DLR0 lock\n");
    rwlock_r_unlock(&lock);
    rc = 1;
    goto cleanup;
  }

cleanup:
  atomic_store_explicit(&writer_release, true, memory_order_release);
  pthread_join(writer_thread, NULL);
  if (rc == 0 && !rwlock_r_trylock(&lock)) {
    fprintf(stderr, "reader should acquire after queued writer releases in DLR0\n");
    return 1;
  }
  if (rc == 0) {
    rwlock_r_unlock(&lock);
  }
  return rc;
}
#endif

#ifdef DLR1
typedef struct StressArgs {
  atomic_bool *start;
  atomic_uint *ready;
  atomic_uint_fast64_t *read_failures;
  atomic_uint_fast64_t *contender_errors;
  size_t iterations;
} StressArgs;

static void wait_for_start(const atomic_bool *start) {
  while (!atomic_load_explicit(start, memory_order_acquire)) {
    cpu_relax();
  }
}

static void *reader_worker(void *arg) {
  StressArgs *args = (StressArgs *)arg;
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];

  tx_init(&tx, 1, &res, &quit, 1);
  atomic_fetch_add_explicit(args->ready, 1, memory_order_release);
  wait_for_start(args->start);

  for (size_t i = 0; i < args->iterations; ++i) {
    int rc = tx_read(&tx, 0, buf);
    if (rc != 0) {
      atomic_fetch_add_explicit(args->read_failures, 1, memory_order_relaxed);
      if (tx.status == TX_ABORTED) {
        tx_abort(&tx);
      } else {
        tx_reset(&tx);
      }
      continue;
    }
    tx_commit(&tx);
  }

  tx_destroy(&tx);
  return NULL;
}

static void *writer_worker(void *arg) {
  StressArgs *args = (StressArgs *)arg;
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char val[VAL_SIZE];

  memset(val, 'b', VAL_SIZE);
  tx_init(&tx, 2, &res, &quit, 1);
  atomic_fetch_add_explicit(args->ready, 1, memory_order_release);
  wait_for_start(args->start);

  for (size_t i = 0; i < args->iterations; ++i) {
    int rc = tx_write(&tx, 0, val);
    if (rc != -2 || tx.status != TX_ABORTED) {
      atomic_fetch_add_explicit(args->contender_errors, 1, memory_order_relaxed);
      if (rc == 0) {
        tx_commit(&tx);
      } else if (tx.status == TX_ABORTED) {
        tx_abort(&tx);
      } else {
        tx_reset(&tx);
      }
      continue;
    }
    tx_abort(&tx);
  }

  tx_destroy(&tx);
  return NULL;
}

static void *upgrade_worker(void *arg) {
  StressArgs *args = (StressArgs *)arg;
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val[VAL_SIZE];

  memset(val, 'c', VAL_SIZE);
  tx_init(&tx, 3, &res, &quit, 1);
  atomic_fetch_add_explicit(args->ready, 1, memory_order_release);
  wait_for_start(args->start);

  for (size_t i = 0; i < args->iterations; ++i) {
    int rc = tx_read(&tx, 0, buf);
    if (rc != 0) {
      atomic_fetch_add_explicit(args->contender_errors, 1, memory_order_relaxed);
      if (tx.status == TX_ABORTED) {
        tx_abort(&tx);
      } else {
        tx_reset(&tx);
      }
      continue;
    }

    rc = tx_write(&tx, 0, val);
    if (rc != -2 || tx.status != TX_ABORTED) {
      atomic_fetch_add_explicit(args->contender_errors, 1, memory_order_relaxed);
      if (rc == 0) {
        tx_commit(&tx);
      } else if (tx.status == TX_ABORTED) {
        tx_abort(&tx);
      } else {
        tx_reset(&tx);
      }
      continue;
    }
    tx_abort(&tx);
  }

  tx_destroy(&tx);
  return NULL;
}

static int run_reader_stress(const char *name, void *(*contender_worker)(void *)) {
  const size_t iterations = 200000;
  atomic_bool start = ATOMIC_VAR_INIT(false);
  atomic_uint ready = ATOMIC_VAR_INIT(0);
  atomic_uint_fast64_t read_failures = ATOMIC_VAR_INIT(0);
  atomic_uint_fast64_t contender_errors = ATOMIC_VAR_INIT(0);
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result holder_res = {0};
  Transaction holder_tx;
  StressArgs args = {
      .start = &start,
      .ready = &ready,
      .read_failures = &read_failures,
      .contender_errors = &contender_errors,
      .iterations = iterations,
  };
  pthread_t reader_thread;
  pthread_t contender_thread;
  char buf[VAL_SIZE];
  int reader_started = 0;
  int contender_started = 0;
  int rc = 0;

  init_table(1);
  tx_init(&holder_tx, 0, &holder_res, &quit, 1);
  if (tx_read(&holder_tx, 0, buf) != 0) {
    fprintf(stderr, "%s: failed to create the shared reader\n", name);
    rc = 1;
    goto cleanup_holder;
  }

  if (pthread_create(&reader_thread, NULL, reader_worker, &args) != 0) {
    fprintf(stderr, "%s: failed to start reader thread\n", name);
    rc = 1;
    goto cleanup_holder;
  }
  reader_started = 1;
  if (pthread_create(&contender_thread, NULL, contender_worker, &args) != 0) {
    fprintf(stderr, "%s: failed to start contender thread\n", name);
    rc = 1;
    goto cleanup_holder;
  }
  contender_started = 1;

  while (atomic_load_explicit(&ready, memory_order_acquire) < 2) {
    cpu_relax();
  }
  atomic_store_explicit(&start, true, memory_order_release);

  pthread_join(reader_thread, NULL);
  pthread_join(contender_thread, NULL);
  reader_started = 0;
  contender_started = 0;

  if (atomic_load_explicit(&read_failures, memory_order_relaxed) != 0) {
    fprintf(stderr, "%s: readers observed aborts while only no-wait contenders failed\n", name);
    rc = 1;
  }
  if (atomic_load_explicit(&contender_errors, memory_order_relaxed) != 0) {
    fprintf(stderr, "%s: contender thread saw an unexpected outcome\n", name);
    rc = 1;
  }

cleanup_holder:
  if (reader_started || contender_started) {
    atomic_store_explicit(&start, true, memory_order_release);
  }
  if (reader_started) {
    pthread_join(reader_thread, NULL);
  }
  if (contender_started) {
    pthread_join(contender_thread, NULL);
  }
  tx_abort(&holder_tx);
  tx_destroy(&holder_tx);
  reset_table();
  return rc;
}
#endif

int main(void) {
  if (test_delete_then_write() != 0) return 1;
  if (test_delete_then_insert() != 0) return 1;
  if (test_delete_insert_delete_reinsert() != 0) return 1;
  if (test_insert_write_delete_reinsert() != 0) return 1;
  if (test_read_write_upgrade() != 0) return 1;
#ifdef DLR0
  if (test_dlr0_queued_writer_blocks_new_readers() != 0) return 1;
#endif
#ifdef DLR1
  if (run_reader_stress("writer no-wait", writer_worker) != 0) return 1;
  if (run_reader_stress("upgrade no-wait", upgrade_worker) != 0) return 1;
#endif
  return 0;
}
