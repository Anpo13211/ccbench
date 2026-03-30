#include "include/transaction.h"

#include <stdlib.h>
#include <string.h>

static int find_write(Transaction *tx, uint64_t key) {
  for (size_t i = 0; i < tx->write_count; ++i) {
    if (tx->write_set[i].key == key) return (int)i;
  }
  return -1;
}

static int find_read(Transaction *tx, uint64_t key) {
  for (size_t i = 0; i < tx->read_count; ++i) {
    if (tx->read_set[i].key == key) return (int)i;
  }
  return -1;
}

static int find_lock(Transaction *tx, uint64_t key) {
  for (size_t i = 0; i < tx->lock_count; ++i) {
    if (tx->lock_entries[i].key == key) return (int)i;
  }
  return -1;
}

static int lock_entry_cmp(const void *a, const void *b) {
  const LockEntry *la = (const LockEntry *)a;
  const LockEntry *lb = (const LockEntry *)b;
  if (la->key < lb->key) return -1;
  if (la->key > lb->key) return 1;
  return 0;
}

void tx_init(Transaction *tx, int thid, Result *res, const atomic_bool *quit, size_t max_ope) {
  tx->thid = thid;
  tx->result = res;
  tx->quit = quit;
  tx->status = TX_INFLIGHT;
  tx->read_set = (ReadEntry *)calloc(max_ope, sizeof(ReadEntry));
  tx->write_set = (WriteEntry *)calloc(max_ope, sizeof(WriteEntry));
  tx->lock_entries = (LockEntry *)calloc(max_ope, sizeof(LockEntry));
  tx->r_locks = (RWLock **)calloc(max_ope, sizeof(RWLock *));
  tx->w_locks = (RWLock **)calloc(max_ope, sizeof(RWLock *));
  tx->pro_set = (Op *)calloc(max_ope, sizeof(Op));
  tx->pro_count = max_ope;
  tx->read_count = 0;
  tx->write_count = 0;
  tx->lock_count = 0;
  tx->r_lock_count = 0;
  tx->w_lock_count = 0;
}

void tx_reset(Transaction *tx) {
  tx->status = TX_INFLIGHT;
  tx->read_count = 0;
  tx->write_count = 0;
  tx->lock_count = 0;
  tx->r_lock_count = 0;
  tx->w_lock_count = 0;
}

void tx_destroy(Transaction *tx) {
  free(tx->read_set);
  free(tx->write_set);
  free(tx->lock_entries);
  free(tx->r_locks);
  free(tx->w_locks);
  free(tx->pro_set);
}

void tx_build_locklist(Transaction *tx) {
  tx->lock_count = 0;
  for (size_t i = 0; i < tx->pro_count; ++i) {
    uint64_t key = tx->pro_set[i].key;
    int exclusive = (tx->pro_set[i].type != OP_READ);
    int idx = find_lock(tx, key);
    if (idx >= 0) {
      if (exclusive) tx->lock_entries[idx].exclusive = 1;
      continue;
    }
    tx->lock_entries[tx->lock_count].key = key;
    tx->lock_entries[tx->lock_count].exclusive = exclusive ? 1 : 0;
    tx->lock_count++;
  }
  qsort(tx->lock_entries, tx->lock_count, sizeof(LockEntry), lock_entry_cmp);
}

int tx_locklist(Transaction *tx) {
  for (size_t i = 0; i < tx->lock_count; ++i) {
    uint64_t key = tx->lock_entries[i].key;
    if (key >= g_cfg.tuple_num) {
      return 0;
    }
    Tuple *tuple = &Table[key];
    if (tx->lock_entries[i].exclusive) {
      rwlock_w_lock(&tuple->lock);
      tx->w_locks[tx->w_lock_count++] = &tuple->lock;
    } else {
      rwlock_r_lock(&tuple->lock);
      if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) {
        rwlock_r_unlock(&tuple->lock);
        return 0;
      }
      tx->r_locks[tx->r_lock_count++] = &tuple->lock;
    }
  }
  return 1;
}

int tx_read(Transaction *tx, uint64_t key, char out_val[VAL_SIZE]) {
  int widx = find_write(tx, key);
  if (widx >= 0) {
    if (tx->write_set[widx].op == WOP_DELETE) return -1;
    memcpy(out_val, tx->write_set[widx].val, VAL_SIZE);
    return 0;
  }
  int ridx = find_read(tx, key);
  if (ridx >= 0) {
    memcpy(out_val, tx->read_set[ridx].val, VAL_SIZE);
    return 0;
  }

  if (key >= g_cfg.tuple_num) return -1;
  Tuple *tuple = &Table[key];
  if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) return -1;

  memcpy(tx->read_set[tx->read_count].val, tuple->val, VAL_SIZE);
  tx->read_set[tx->read_count].key = key;
  memcpy(out_val, tx->read_set[tx->read_count].val, VAL_SIZE);
  tx->read_count++;
  return 0;
}

int tx_write(Transaction *tx, uint64_t key, const char val[VAL_SIZE]) {
  int widx = find_write(tx, key);
  if (widx >= 0) {
    WriteEntry *we = &tx->write_set[widx];
    if (we->op == WOP_DELETE) return -1;
    memcpy(we->val, val, VAL_SIZE);
    return 0;
  }

  if (key >= g_cfg.tuple_num) return -1;
  Tuple *tuple = &Table[key];
  if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) return -1;

  tx->write_set[tx->write_count].key = key;
  tx->write_set[tx->write_count].op = WOP_UPDATE;
  tx->write_set[tx->write_count].existed_before_tx = 1;
  memcpy(tx->write_set[tx->write_count].val, val, VAL_SIZE);
  tx->write_count++;
  return 0;
}

int tx_insert(Transaction *tx, uint64_t key, const char val[VAL_SIZE]) {
  int widx = find_write(tx, key);
  if (widx >= 0) {
    WriteEntry *we = &tx->write_set[widx];
    if (we->op != WOP_DELETE) return -3;
    we->op = WOP_INSERT;
    memcpy(we->val, val, VAL_SIZE);
    return 0;
  }
  if (key >= g_cfg.tuple_num) return -1;
  Tuple *tuple = &Table[key];
  if (atomic_load_explicit(&tuple->present, memory_order_acquire)) return -3;

  tx->write_set[tx->write_count].key = key;
  tx->write_set[tx->write_count].op = WOP_INSERT;
  tx->write_set[tx->write_count].existed_before_tx = 0;
  memcpy(tx->write_set[tx->write_count].val, val, VAL_SIZE);
  tx->write_count++;
  return 0;
}

int tx_delete(Transaction *tx, uint64_t key) {
  int widx = find_write(tx, key);
  if (widx >= 0) {
    WriteEntry *we = &tx->write_set[widx];
    if (we->op == WOP_INSERT) {
      if (!we->existed_before_tx) {
        tx->write_set[widx] = tx->write_set[tx->write_count - 1];
        tx->write_count--;
        return 0;
      }
      we->op = WOP_DELETE;
      memset(we->val, 0, VAL_SIZE);
      return 0;
    }
    if (we->op == WOP_UPDATE) {
      we->op = WOP_DELETE;
      memset(we->val, 0, VAL_SIZE);
      return 0;
    }
    return 0;
  }

  if (key >= g_cfg.tuple_num) return -1;
  Tuple *tuple = &Table[key];
  if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) return -1;

  tx->write_set[tx->write_count].key = key;
  tx->write_set[tx->write_count].op = WOP_DELETE;
  tx->write_set[tx->write_count].existed_before_tx = 1;
  memset(tx->write_set[tx->write_count].val, 0, VAL_SIZE);
  tx->write_count++;
  return 0;
}

int tx_commit(Transaction *tx) {
  for (size_t i = 0; i < tx->write_count; ++i) {
    WriteEntry *we = &tx->write_set[i];
    Tuple *tuple = &Table[we->key];
    if (we->op == WOP_UPDATE) {
      memcpy(tuple->val, we->val, VAL_SIZE);
    } else if (we->op == WOP_INSERT) {
      memcpy(tuple->val, we->val, VAL_SIZE);
      atomic_store_explicit(&tuple->present, true, memory_order_release);
    } else if (we->op == WOP_DELETE) {
      atomic_store_explicit(&tuple->present, false, memory_order_release);
      memset(tuple->val, 0, VAL_SIZE);
    }
  }

  for (size_t i = 0; i < tx->r_lock_count; ++i) {
    rwlock_r_unlock(tx->r_locks[i]);
  }
  for (size_t i = 0; i < tx->w_lock_count; ++i) {
    rwlock_w_unlock(tx->w_locks[i]);
  }

  tx_reset(tx);
  return 1;
}

void tx_abort(Transaction *tx) {
  for (size_t i = 0; i < tx->r_lock_count; ++i) {
    rwlock_r_unlock(tx->r_locks[i]);
  }
  for (size_t i = 0; i < tx->w_lock_count; ++i) {
    rwlock_w_unlock(tx->w_locks[i]);
  }
  tx_reset(tx);
  tx->result->abort_count++;
}
