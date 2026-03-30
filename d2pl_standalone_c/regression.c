#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int lock_tx(Transaction *tx) {
  tx_build_locklist(tx);
  if (!tx_locklist(tx)) {
    fprintf(stderr, "tx_locklist should succeed\n");
    return 1;
  }
  return 0;
}

static int test_delete_then_write(void) {
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val[VAL_SIZE];
  char zero[VAL_SIZE] = {0};
  int rc = 0;

  init_table(1);
  tx_init(&tx, 0, &res, &quit, 2);
  tx.pro_set[0].type = OP_WRITE;
  tx.pro_set[0].key = 0;
  tx.pro_set[1].type = OP_WRITE;
  tx.pro_set[1].key = 0;
  if (lock_tx(&tx) != 0) {
    rc = 1;
    goto cleanup;
  }

  fill_val(val, 'b');
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
  for (size_t i = 0; i < 4; ++i) {
    tx.pro_set[i].type = OP_WRITE;
    tx.pro_set[i].key = 0;
  }
  if (lock_tx(&tx) != 0) {
    rc = 1;
    goto cleanup;
  }

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
  tx.pro_set[0].type = OP_WRITE;
  tx.pro_set[0].key = 0;
  tx.pro_set[1].type = OP_WRITE;
  tx.pro_set[1].key = 0;
  if (lock_tx(&tx) != 0) {
    rc = 1;
    goto cleanup;
  }

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
  for (size_t i = 0; i < 4; ++i) {
    tx.pro_set[i].type = OP_WRITE;
    tx.pro_set[i].key = 0;
  }
  if (lock_tx(&tx) != 0) {
    rc = 1;
    goto cleanup;
  }

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

static int test_read_write_sequence(void) {
  atomic_bool quit = ATOMIC_VAR_INIT(false);
  Result res = {0};
  Transaction tx;
  char buf[VAL_SIZE];
  char val_b[VAL_SIZE];
  int rc = 0;

  init_table(1);
  set_tuple_present(0, 'a');
  tx_init(&tx, 0, &res, &quit, 2);
  tx.pro_set[0].type = OP_READ;
  tx.pro_set[0].key = 0;
  tx.pro_set[1].type = OP_WRITE;
  tx.pro_set[1].key = 0;
  if (lock_tx(&tx) != 0) {
    rc = 1;
    goto cleanup;
  }

  fill_val(val_b, 'b');
  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, Table[0].val, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should succeed on a present tuple\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_write(&tx, 0, val_b) != 0) {
    fprintf(stderr, "tx_write should succeed while holding the exclusive lock\n");
    rc = 1;
    goto cleanup;
  }
  if (tx_read(&tx, 0, buf) != 0 || memcmp(buf, val_b, VAL_SIZE) != 0) {
    fprintf(stderr, "tx_read should see its own write\n");
    rc = 1;
    goto cleanup;
  }
  tx_commit(&tx);
  if (!atomic_load_explicit(&Table[0].present, memory_order_acquire) ||
      memcmp(Table[0].val, val_b, VAL_SIZE) != 0) {
    fprintf(stderr, "write should commit the new value\n");
    rc = 1;
    goto cleanup;
  }

cleanup:
  tx_destroy(&tx);
  reset_table();
  return rc;
}

int main(void) {
  if (test_delete_then_write() != 0) return 1;
  if (test_delete_then_insert() != 0) return 1;
  if (test_delete_insert_delete_reinsert() != 0) return 1;
  if (test_insert_write_delete_reinsert() != 0) return 1;
  if (test_read_write_sequence() != 0) return 1;
  return 0;
}
