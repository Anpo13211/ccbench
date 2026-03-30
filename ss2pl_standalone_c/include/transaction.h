#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "config.h"
#include "result.h"
#include "storage.h"
#include "workload.h"

typedef enum TxStatus {
  TX_INFLIGHT = 0,
  TX_ABORTED = 1
} TxStatus;

typedef enum WriteOp {
  WOP_UPDATE = 0,
  WOP_INSERT = 1,
  WOP_DELETE = 2
} WriteOp;

typedef struct ReadEntry {
  uint64_t key;
  char val[VAL_SIZE];
} ReadEntry;

typedef struct WriteEntry {
  uint64_t key;
  WriteOp op;
  int existed_before_tx;
  char val[VAL_SIZE];
} WriteEntry;

typedef struct Transaction {
  int thid;
  Result *result;
  const atomic_bool *quit;
  TxStatus status;

  ReadEntry *read_set;
  size_t read_count;
  WriteEntry *write_set;
  size_t write_count;

  RWLock **r_locks;
  size_t r_lock_count;
  RWLock **w_locks;
  size_t w_lock_count;

  Op *pro_set;
  size_t pro_count;
} Transaction;

void tx_init(Transaction *tx, int thid, Result *res, const atomic_bool *quit, size_t max_ope);
void tx_reset(Transaction *tx);
void tx_destroy(Transaction *tx);

int tx_read(Transaction *tx, uint64_t key, char out_val[VAL_SIZE]);
int tx_write(Transaction *tx, uint64_t key, const char val[VAL_SIZE]);
int tx_insert(Transaction *tx, uint64_t key, const char val[VAL_SIZE]);
int tx_delete(Transaction *tx, uint64_t key);

int tx_commit(Transaction *tx);
void tx_abort(Transaction *tx);
