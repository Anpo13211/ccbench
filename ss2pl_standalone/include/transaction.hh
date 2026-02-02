#pragma once

#include <cstdint>
#include <deque>
#include <queue>
#include <string_view>
#include <vector>

#include "backoff.hh"
#include "procedure.hh"
#include "result.hh"
#include "status.hh"
#include "lock.hh"
#include "util.hh"
#include "ss2pl_op_element.hh"
#include "tuple.hh"

extern std::vector<Result> SS2PLResult;

enum class TransactionStatus : uint8_t {
  invalid,
  inflight,
  committed,
  aborted,
};

extern void writeValGenerator(char *writeVal, size_t val_size, size_t thid);

class TxExecutor {
public:
  alignas(CACHE_LINE_SIZE) int thid_;
  std::vector<RWLock *> r_lock_list_;
  std::vector<RWLock *> w_lock_list_;
  TransactionStatus status_ = TransactionStatus::inflight;
  Result *result_;
  Backoff backoff_;
  std::vector<SetElement<Tuple>> read_set_;
  std::vector<SetElement<Tuple>> write_set_;
  std::vector<Procedure> pro_set_;
  std::deque<Tuple*> gc_records_;
  const bool& quit_; // for thread termination control
  bool reconnoitering_ = false;
  bool is_ronly_ = false;
  bool is_batch_ = false;

  TxExecutor(int thid, Result *res,  const bool &quit)
      : thid_(thid), result_(res), backoff_(FLAGS_clocks_per_us), quit_(quit) {
    // read_set_.reserve(FLAGS_max_ope);
    // write_set_.reserve(FLAGS_max_ope);
    // pro_set_.reserve(FLAGS_max_ope);
    // r_lock_list_.reserve(FLAGS_max_ope);
    // w_lock_list_.reserve(FLAGS_max_ope);
  }

  SetElement<Tuple> *searchReadSet(Storage s, std::string_view key);

  SetElement<Tuple> *searchWriteSet(Storage s, std::string_view key);

  void begin();

  Status read(Storage s, std::string_view key, TupleBody** body);
  void read_internal(Storage s, std::string_view key, Tuple* tuple);

  Status scan(Storage s,
              std::string_view left_key, bool l_exclusive,
              std::string_view right_key, bool r_exclusive,
              std::vector<TupleBody *>&result);
  Status scan(Storage s,
              std::string_view left_key, bool l_exclusive,
              std::string_view right_key, bool r_exclusive,
              std::vector<TupleBody *>&result, int64_t limit);

  Status write(Storage s, std::string_view key, TupleBody&& body);

  Status insert(Storage s, std::string_view key, TupleBody&& body);

  Status delete_record(Storage s, std::string_view key);

  bool commit();

  void abort();

  Status read_lock(Storage s, std::string_view key);
  Status write_lock(Storage s, std::string_view key);

  void unlockList();

  void reconnoiter_begin();

  void reconnoiter_end();

  bool isLeader();

  void leaderWork();

  // inline
  Tuple *get_tuple(Tuple *table, uint64_t key) { return &table[key]; }
};
