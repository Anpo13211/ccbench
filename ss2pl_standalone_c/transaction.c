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

static void remove_r_lock(Transaction *tx, RWLock *lock) {
  for (size_t i = 0; i < tx->r_lock_count; ++i) {
    if (tx->r_locks[i] == lock) {
      tx->r_locks[i] = tx->r_locks[tx->r_lock_count - 1];
      tx->r_lock_count--;
      return;
    }
  }
}

static void remove_read_entry(Transaction *tx, uint64_t key) {
  for (size_t i = 0; i < tx->read_count; ++i) {
    if (tx->read_set[i].key == key) {
      tx->read_set[i] = tx->read_set[tx->read_count - 1];
      tx->read_count--;
      return;
    }
  }
}

static void remove_write_entry(Transaction *tx, uint64_t key) {
  for (size_t i = 0; i < tx->write_count; ++i) {
    if (tx->write_set[i].key == key) {
      tx->write_set[i] = tx->write_set[tx->write_count - 1];
      tx->write_count--;
      return;
    }
  }
}

static int has_r_lock(Transaction *tx, RWLock *lock) {
  for (size_t i = 0; i < tx->r_lock_count; ++i) {
    if (tx->r_locks[i] == lock) return 1;
  }
  return 0;
}

static int has_w_lock(Transaction *tx, RWLock *lock) {
  for (size_t i = 0; i < tx->w_lock_count; ++i) {
    if (tx->w_locks[i] == lock) return 1;
  }
  return 0;
}

void tx_init(Transaction *tx, int thid, Result *res, const atomic_bool *quit, size_t max_ope) {
  tx->thid = thid;
  tx->result = res;
  tx->quit = quit;
  tx->status = TX_INFLIGHT;
  tx->read_set = (ReadEntry *)calloc(max_ope, sizeof(ReadEntry));
  tx->write_set = (WriteEntry *)calloc(max_ope, sizeof(WriteEntry));
  tx->r_locks = (RWLock **)calloc(max_ope, sizeof(RWLock *));
  tx->w_locks = (RWLock **)calloc(max_ope, sizeof(RWLock *));
  tx->pro_set = (Op *)calloc(max_ope, sizeof(Op));
  tx->pro_count = max_ope;
  tx->read_count = 0;
  tx->write_count = 0;
  tx->r_lock_count = 0;
  tx->w_lock_count = 0;
}

void tx_reset(Transaction *tx) {
  tx->status = TX_INFLIGHT;
  tx->read_count = 0;
  tx->write_count = 0;
  tx->r_lock_count = 0;
  tx->w_lock_count = 0;
}

void tx_destroy(Transaction *tx) {
  free(tx->read_set);
  free(tx->write_set);
  free(tx->r_locks);
  free(tx->w_locks);
  free(tx->pro_set);
}

int tx_read(Transaction *tx, uint64_t key, char out_val[VAL_SIZE]) {
  int widx = find_write(tx, key);
  if (widx >= 0) {
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

#ifdef DLR0
  rwlock_r_lock(&tuple->lock);
#elif defined(DLR1)
  if (!rwlock_r_trylock(&tuple->lock)) {
    tx->status = TX_ABORTED;
    return -2;
  }
#endif
  if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) {
    rwlock_r_unlock(&tuple->lock);
    return -1;
  }
  tx->r_locks[tx->r_lock_count++] = &tuple->lock;

  memcpy(tx->read_set[tx->read_count].val, tuple->val, VAL_SIZE);
  tx->read_set[tx->read_count].key = key;
  memcpy(out_val, tx->read_set[tx->read_count].val, VAL_SIZE);
  tx->read_count++;
  return 0;
}

int tx_write(Transaction *tx, uint64_t key, const char val[VAL_SIZE]) {
  int widx = find_write(tx, key);
  if (widx >= 0) {
    memcpy(tx->write_set[widx].val, val, VAL_SIZE);
    return 0;
  }

  int ridx = find_read(tx, key);
  if (ridx >= 0) {
    Tuple *tuple = &Table[key];
    if (!rwlock_tryupgrade(&tuple->lock)) {
      tx->status = TX_ABORTED;
      return -2;
    }
    remove_r_lock(tx, &tuple->lock);
    remove_read_entry(tx, key);
    tx->w_locks[tx->w_lock_count++] = &tuple->lock;
    if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) {
      rwlock_w_unlock(&tuple->lock);
      return -1;
    }

    tx->write_set[tx->write_count].key = key;
    tx->write_set[tx->write_count].op = WOP_UPDATE;
    memcpy(tx->write_set[tx->write_count].val, val, VAL_SIZE);
    tx->write_count++;
    return 0;
  }

  if (key >= g_cfg.tuple_num) return -1;
  Tuple *tuple = &Table[key];

#ifdef DLR0
  rwlock_w_lock(&tuple->lock);
#elif defined(DLR1)
  if (!rwlock_w_trylock(&tuple->lock)) {
    tx->status = TX_ABORTED;
    return -2;
  }
#endif
  if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) {
    rwlock_w_unlock(&tuple->lock);
    return -1;
  }
  tx->w_locks[tx->w_lock_count++] = &tuple->lock;

  tx->write_set[tx->write_count].key = key;
  tx->write_set[tx->write_count].op = WOP_UPDATE;
  memcpy(tx->write_set[tx->write_count].val, val, VAL_SIZE);
  tx->write_count++;
  return 0;
}

int tx_insert(Transaction *tx, uint64_t key, const char val[VAL_SIZE]) {
  if (find_write(tx, key) >= 0) return -3;
  if (key >= g_cfg.tuple_num) return -1;
  Tuple *tuple = &Table[key];

#ifdef DLR0
  rwlock_w_lock(&tuple->lock);
#elif defined(DLR1)
  if (!rwlock_w_trylock(&tuple->lock)) {
    tx->status = TX_ABORTED;
    return -2;
  }
#endif
  if (atomic_load_explicit(&tuple->present, memory_order_acquire)) {
    rwlock_w_unlock(&tuple->lock);
    return -3;
  }
  tx->w_locks[tx->w_lock_count++] = &tuple->lock;

  tx->write_set[tx->write_count].key = key;
  tx->write_set[tx->write_count].op = WOP_INSERT;
  memcpy(tx->write_set[tx->write_count].val, val, VAL_SIZE);
  tx->write_count++;
  return 0;
}

int tx_delete(Transaction *tx, uint64_t key) {
  remove_write_entry(tx, key);
  if (key >= g_cfg.tuple_num) return -1;
  Tuple *tuple = &Table[key];

  if (has_w_lock(tx, &tuple->lock)) {
    if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) return -1;
    tx->write_set[tx->write_count].key = key;
    tx->write_set[tx->write_count].op = WOP_DELETE;
    memset(tx->write_set[tx->write_count].val, 0, VAL_SIZE);
    tx->write_count++;
    return 0;
  }

  if (has_r_lock(tx, &tuple->lock)) {
    if (!rwlock_tryupgrade(&tuple->lock)) {
      tx->status = TX_ABORTED;
      return -2;
    }
    remove_r_lock(tx, &tuple->lock);
    remove_read_entry(tx, key);
    tx->w_locks[tx->w_lock_count++] = &tuple->lock;
    if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) {
      rwlock_w_unlock(&tuple->lock);
      return -1;
    }
    tx->write_set[tx->write_count].key = key;
    tx->write_set[tx->write_count].op = WOP_DELETE;
    memset(tx->write_set[tx->write_count].val, 0, VAL_SIZE);
    tx->write_count++;
    return 0;
  }

#ifdef DLR0
  rwlock_w_lock(&tuple->lock);
#elif defined(DLR1)
  if (!rwlock_w_trylock(&tuple->lock)) {
    tx->status = TX_ABORTED;
    return -2;
  }
#endif
  if (!atomic_load_explicit(&tuple->present, memory_order_acquire)) {
    rwlock_w_unlock(&tuple->lock);
    return -1;
  }
  tx->w_locks[tx->w_lock_count++] = &tuple->lock;

  tx->write_set[tx->write_count].key = key;
  tx->write_set[tx->write_count].op = WOP_DELETE;
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
