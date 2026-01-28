#include <stdio.h>
#include <string.h>

#include <atomic>

#include "include/backoff.hh"
#include "include/debug.hh"
#include "include/procedure.hh"
#include "include/result.hh"
#include "include/util.hh"
#include "include/common.hh"
#include "include/transaction.hh"

using namespace std;

extern void display_procedure_vector(std::vector<Procedure> &pro);

namespace {
inline Tuple* resolve_tuple(std::string_view key) {
  if (key.size() != sizeof(uint64_t)) return nullptr;
  uint64_t idx = 0;
  parse_bigendian(key.data(), idx);
  if (idx >= FLAGS_tuple_num) return nullptr;
  return &Table[idx];
}

inline Tuple* lookup_tuple([[maybe_unused]] Storage s, std::string_view key) {
  Tuple* tuple = resolve_tuple(key);
  if (tuple == nullptr) return nullptr;
  if (!tuple->present_.load(std::memory_order_acquire)) return nullptr;
  return tuple;
}
}  // namespace

SetElement<Tuple> *TxExecutor::searchReadSet(Storage s, std::string_view key) {
  for (auto &re : read_set_) {
    if (re.storage_ != s) continue;
    if (re.key_ == key) return &re;
  }

  return nullptr;
}

SetElement<Tuple> *TxExecutor::searchWriteSet(Storage s, std::string_view key) {
  for (auto &we : write_set_) {
    if (we.storage_ != s) continue;
    if (we.key_ == key) return &we;
  }

  return nullptr;
}

/**
 * @brief function about abort.
 * Clean-up local read/write set.
 * Release locks.
 * @return void
 */
void TxExecutor::abort() {
  /**
   * Release locks
   */
  unlockList();

  /**
   * Clean-up local read/write set.
   */
  read_set_.clear();
  write_set_.clear();

  ++result_->local_abort_counts_;

#if BACK_OFF
#if ADD_ANALYSIS
  uint64_t start(rdtscp());
#endif

  Backoff::backoff(FLAGS_clocks_per_us);

#if ADD_ANALYSIS
  result_->local_backoff_latency_ += rdtscp() - start;
#endif

#endif
}

/**
 * @brief success termination of transaction.
 * @return void
 */
bool TxExecutor::commit() {
  for (auto itr = write_set_.begin(); itr != write_set_.end(); ++itr) {
    switch ((*itr).op_) {
      case OpType::UPDATE: {
        memcpy((*itr).rcdptr_->body_.get_val_ptr(),
               (*itr).body_.get_val_ptr(), (*itr).body_.get_val_size());
        break;
      }
      case OpType::INSERT: {
        break;
      }
      case OpType::DELETE: {
        (*itr).rcdptr_->present_.store(false, std::memory_order_release);
        (*itr).rcdptr_->body_ = TupleBody();
        break;
      }
      default:
        ERR;
    }
  }

  /**
   * Release locks.
   */
  unlockList();

  /**
   * Clean-up local read/write set.
   */
  read_set_.clear();
  write_set_.clear();

  return true;
}

/**
 * @brief Initialize function of transaction.
 * Allocate timestamp.
 * @return void
 */
void TxExecutor::begin() { this->status_ = TransactionStatus::inflight; }

/**
 * @brief Transaction read function.
 * @param [in] key The key of key-value
 */
Status TxExecutor::read(Storage s, std::string_view key, TupleBody** body) {
#if ADD_ANALYSIS
  uint64_t start = rdtscp();
#endif  // ADD_ANALYSIS
  TupleBody b;
  SetElement<Tuple>* e;

  /**
   * read-own-writes or re-read from local read set.
   */
  e = searchReadSet(s, key);
  if (e) {
    *body = &(e->body_);
    goto FINISH_READ;
  }
  e = searchWriteSet(s, key);
  if (e) {
    *body = &(e->body_);
    goto FINISH_READ;
  }

  /**
   * Search tuple from data structure.
   */
  Tuple *tuple;
  tuple = lookup_tuple(s, key);
#if ADD_ANALYSIS
  ++result_->local_tree_traversal_;
#endif
  if (tuple == nullptr) return Status::WARN_NOT_FOUND;

  read_internal(s, key, tuple);
  *body = &(read_set_.back().body_);

FINISH_READ:

#if ADD_ANALYSIS
  result_->local_read_latency_ += rdtscp() - start;
#endif
  return Status::OK;
}

void TxExecutor::read_internal(Storage s, std::string_view key, Tuple* tuple) {
  TupleBody body;

  /**
   * read payload.
   */
  body = TupleBody(tuple->body_.get_key(), tuple->body_.get_val(), tuple->body_.get_val_align());
  read_set_.emplace_back(s, key, tuple, std::move(body));
  return;
}

Status TxExecutor::scan(const Storage s,
                      std::string_view left_key, bool l_exclusive,
                      std::string_view right_key, bool r_exclusive,
                      std::vector<TupleBody*>& result) {
  result.clear();
  auto rset_init_size = read_set_.size();

  uint64_t left = 0;
  uint64_t right = FLAGS_tuple_num - 1;

  if (!left_key.empty()) {
    parse_bigendian(left_key.data(), left);
  }
  if (!right_key.empty()) {
    parse_bigendian(right_key.data(), right);
  }

  if (l_exclusive && left < FLAGS_tuple_num) {
    ++left;
  }
  if (r_exclusive) {
    if (right == 0) return Status::OK;
    --right;
  }

  if (left >= FLAGS_tuple_num || left > right) {
    return Status::OK;
  }
  if (right >= FLAGS_tuple_num) {
    right = FLAGS_tuple_num - 1;
  }

  for (uint64_t i = left; i <= right; ++i) {
    Tuple* tuple = &Table[i];
    if (!tuple->present_.load(std::memory_order_acquire)) continue;
    SetElement<Tuple>* e = searchReadSet(s, tuple->body_.get_key());
    if (e) {
      result.emplace_back(&(e->body_));
      continue;
    }

    e = searchWriteSet(s, tuple->body_.get_key());
    if (e) {
      result.emplace_back(&(e->body_));
      continue;
    }

    read_internal(s, tuple->body_.get_key(), tuple);
  }

  if (rset_init_size != read_set_.size()) {
    for (auto itr = read_set_.begin() + rset_init_size;
         itr != read_set_.end(); ++itr) {
      result.emplace_back(&((*itr).body_));
    }
  }

  return Status::OK;
}

/**
 * @brief transaction write operation
 * @param [in] key The key of key-value
 * @return void
 */
Status TxExecutor::write(Storage s, std::string_view key, TupleBody&& body) {
#if ADD_ANALYSIS
  uint64_t start = rdtscp();
#endif

  // if it already wrote the key object once.
  if (searchWriteSet(s, key)) goto FINISH_WRITE;

  /**
   * Search tuple from data structure.
   */
  Tuple *tuple;
  tuple = lookup_tuple(s, key);
#if ADD_ANALYSIS
    ++result_->local_tree_traversal_;
#endif
  if (tuple == nullptr) return Status::WARN_NOT_FOUND;

  write_set_.emplace_back(s, key, tuple, std::move(body), OpType::UPDATE);

FINISH_WRITE:
#if ADD_ANALYSIS
  result_->local_write_latency_ += rdtscp() - start;
#endif  // ADD_ANALYSIS
  return Status::OK;
}

Status TxExecutor::insert(Storage s, std::string_view key, TupleBody&& body) {
#if ADD_ANALYSIS
  std::uint64_t start = rdtscp();
#endif

  if (searchWriteSet(s, key)) return Status::WARN_ALREADY_EXISTS;

  Tuple* tuple = resolve_tuple(key);
#if ADD_ANALYSIS
  ++result_->local_tree_traversal_;
#endif
  if (tuple == nullptr) {
    return Status::WARN_NOT_FOUND;
  }
  if (tuple->present_.load(std::memory_order_acquire)) {
    return Status::WARN_ALREADY_EXISTS;
  }

  tuple->init(std::move(body));
  write_set_.emplace_back(s, key, tuple, OpType::INSERT);
  w_lock_list_.emplace_back(&tuple->lock_);
#if ADD_ANALYSIS
  result_->local_write_latency_ += rdtscp() - start;
#endif
  return Status::OK;
}

Status TxExecutor::delete_record(Storage s, std::string_view key) {
#if ADD_ANALYSIS
  std::uint64_t start = rdtscp();
#endif

  // cancel previous write
  for (auto itr = write_set_.begin(); itr != write_set_.end(); ++itr) {
    if ((*itr).storage_ != s) continue;
    if ((*itr).key_ == key) {
      write_set_.erase(itr);
      break;
    }
  }

  Tuple* tuple = lookup_tuple(s, key);
#if ADD_ANALYSIS
  ++result_->local_tree_traversal_;
#endif
  if (tuple == nullptr) {
    return Status::WARN_NOT_FOUND;
  }

  write_set_.emplace_back(s, key, tuple, OpType::DELETE);
#if ADD_ANALYSIS
  result_->local_write_latency_ += rdtscp() - start;
#endif
  return Status::OK;
}

/**
 * @brief lock all target records.
 * @return void
 */
bool TxExecutor::lockList() {
  std::sort(lock_entries_.begin(), lock_entries_.end());
  for (auto& le : lock_entries_) {
    Tuple *tuple;
    tuple = lookup_tuple(le.storage_, le.key_);
    if (tuple == nullptr) {
      std::stringstream ss;
      ss << "WARN: key not found " << (uint32_t)le.storage_ << " " << str_view_hex(le.key_);
      dump(thid_, ss.str());
      return false;
    }
    if (le.is_exclusive_) {
      tuple->lock_.w_lock();
      w_lock_list_.emplace_back(&tuple->lock_);
    } else {
      tuple->lock_.r_lock();
      r_lock_list_.emplace_back(&tuple->lock_);
    }
  }
  return true;
}

/**
 * @brief unlock and clean-up local lock set.
 * @return void
 */
void TxExecutor::unlockList() {
  for (auto itr = r_lock_list_.begin(); itr != r_lock_list_.end(); ++itr)
    (*itr)->r_unlock();

  for (auto itr = w_lock_list_.begin(); itr != w_lock_list_.end(); ++itr) {
    (*itr)->w_unlock();
  }

  /**
   * Clean-up local lock set.
   */
  r_lock_list_.clear();
  w_lock_list_.clear();

  lock_entries_.clear();
}

void TxExecutor::reconnoiter_begin() {
    reconnoitering_ = true;
}

void TxExecutor::reconnoiter_end() {
    read_set_.clear();
    reconnoitering_ = false;
    begin();
}

bool TxExecutor::isLeader() {
  return this->thid_ == 0;
}

void TxExecutor::leaderWork() {
#if BACK_OFF
  leaderBackoffWork(backoff_, D2PLResult);
#endif
}
