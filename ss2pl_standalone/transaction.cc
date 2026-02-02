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

inline SetElement<Tuple> *TxExecutor::searchReadSet(Storage s, std::string_view key) {
  for (auto &re : read_set_) {
    if (re.storage_ != s) continue;
    if (re.key_ == key) return &re;
  }

  return nullptr;
}

inline SetElement<Tuple> *TxExecutor::searchWriteSet(Storage s, std::string_view key) {
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

  if (reconnoitering_) goto FINISH_READ_LOCK;

#ifdef DLR0
  /**
   * Acquire lock with wait.
   */
  tuple->lock_.r_lock();
  r_lock_list_.emplace_back(&tuple->lock_);
#elif defined(DLR1)
  if (tuple->lock_.r_trylock()) {
    r_lock_list_.emplace_back(&tuple->lock_);
  } else {
    /**
     * No-wait and abort.
     */
    this->status_ = TransactionStatus::aborted;
    goto FINISH_READ;
  }
#endif

FINISH_READ_LOCK:
  body = TupleBody(tuple->body_.get_key(), tuple->body_.get_val(), tuple->body_.get_val_align());
  read_set_.emplace_back(s, key, tuple, std::move(body));

FINISH_READ:
  return;
}

Status TxExecutor::scan(const Storage s,
                        std::string_view left_key, bool l_exclusive,
                        std::string_view right_key, bool r_exclusive,
                        std::vector<TupleBody*>& result) {
  return scan(s, left_key, l_exclusive, right_key, r_exclusive, result, -1);
}

Status TxExecutor::scan(const Storage s,
                        std::string_view left_key, bool l_exclusive,
                        std::string_view right_key, bool r_exclusive,
                        std::vector<TupleBody*>& result, int64_t limit) {
  result.clear();

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

  auto can_add = [&]() {
    return (limit < 0) || (static_cast<int64_t>(result.size()) < limit);
  };

  for (uint64_t i = left; i <= right; ++i) {
    if (!can_add()) break;
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

    if (!can_add()) break;
    read_internal(s, tuple->body_.get_key(), tuple);
    if (status_ == TransactionStatus::aborted) {
      return Status::ERROR_LOCK_FAILED;
    }
    result.emplace_back(&(read_set_.back().body_));
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

  for (auto rItr = read_set_.begin(); rItr != read_set_.end(); ++rItr) {
    if ((*rItr).storage_ != s) continue;
    if ((*rItr).key_ == key) {  // hit
#if DLR0
      // Workaround for handling static BoMB properly
      if (!(*rItr).rcdptr_->lock_.tryupgrade()) {
        this->status_ = TransactionStatus::aborted;
        goto FINISH_WRITE;
      }
#elif defined(DLR1)
      if (!(*rItr).rcdptr_->lock_.tryupgrade()) {
        this->status_ = TransactionStatus::aborted;
        goto FINISH_WRITE;
      }
#endif

      // upgrade success
      // remove old element of read lock list.
      for (auto lItr = r_lock_list_.begin(); lItr != r_lock_list_.end();
           ++lItr) {
        if (*lItr == &((*rItr).rcdptr_->lock_)) {
          write_set_.emplace_back(s, key, (*rItr).rcdptr_, std::move(body), OpType::UPDATE);
          w_lock_list_.emplace_back(&(*rItr).rcdptr_->lock_);
          r_lock_list_.erase(lItr);
          break;
        }
      }

      read_set_.erase(rItr);
      goto FINISH_WRITE;
    }
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

#if DLR0
  /**
   * Lock with wait.
   */
  tuple->lock_.w_lock();
#elif defined(DLR1)
  if (!tuple->lock_.w_trylock()) {
    /**
     * No-wait and abort.
     */
    this->status_ = TransactionStatus::aborted;
    goto FINISH_WRITE;
  }
#endif

  /**
   * Register the contents to write lock list and write set.
   */
  w_lock_list_.emplace_back(&tuple->lock_);
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

Status TxExecutor::read_lock(Storage s, std::string_view key) {
  Tuple *tuple;
  tuple = lookup_tuple(s, key);
#if ADD_ANALYSIS
  ++result_->local_tree_traversal_;
#endif
  if (reconnoitering_) return Status::OK;

  if (tuple == nullptr) {
    return Status::WARN_NOT_FOUND;
  }

  for (auto& r_lock : r_lock_list_) {
    if (r_lock == &tuple->lock_) {
      return Status::OK;
    }
  }

  for (auto& w_lock : w_lock_list_) {
    if (w_lock == &tuple->lock_) {
      return Status::OK;
    }
  }

#ifdef DLR0
  tuple->lock_.r_lock();
#elif defined(DLR1)
  if (!tuple->lock_.r_trylock()) {
    this->status_ = TransactionStatus::aborted;
    return Status::ERROR_LOCK_FAILED;
  }
#endif
  r_lock_list_.emplace_back(&tuple->lock_);

  return Status::OK;
}

Status TxExecutor::write_lock(Storage s, std::string_view key) {
  Tuple *tuple;
  tuple = lookup_tuple(s, key);
#if ADD_ANALYSIS
  ++result_->local_tree_traversal_;
#endif
  if (tuple == nullptr) return Status::WARN_NOT_FOUND;

  for (auto& w_lock : w_lock_list_) {
    if (w_lock == &tuple->lock_) {
      return Status::OK;
    }
  }

#if DLR0
  tuple->lock_.w_lock();
#elif defined(DLR1)
  if (!tuple->lock_.w_trylock()) {
    this->status_ = TransactionStatus::aborted;
    return Status::ERROR_LOCK_FAILED;
  }
#endif
  w_lock_list_.emplace_back(&tuple->lock_);

  return Status::OK;
}


/**
 * @brief unlock and clean-up local lock set.
 * @return void
 */
void TxExecutor::unlockList() {
  for (auto itr = r_lock_list_.begin(); itr != r_lock_list_.end(); ++itr)
    (*itr)->r_unlock();

  for (auto itr = w_lock_list_.begin(); itr != w_lock_list_.end(); ++itr)
    (*itr)->w_unlock();

  /**
   * Clean-up local lock set.
   */
  r_lock_list_.clear();
  w_lock_list_.clear();
}

void TxExecutor::reconnoiter_begin() {
  reconnoitering_ = true;
}

void TxExecutor::reconnoiter_end() {
  unlockList();
  read_set_.clear();
  reconnoitering_ = false;
  begin();
}

bool TxExecutor::isLeader() {
  return this->thid_ == 0;
}

void TxExecutor::leaderWork() {
#if BACK_OFF
  leaderBackoffWork(backoff_, SS2PLResult);
#endif
}
