#include <ctype.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <new>

#define GLOBAL_VALUE_DEFINE

#include "include/common.hh"
#include "include/result.hh"
#include "include/transaction.hh"
#include "include/util.hh"
#include "include/ycsb_workload.hh"
#include "include/heap_object.hh"

#include "include/atomic_wrapper.hh"
#include "include/backoff.hh"
#include "include/cpu.hh"
#include "include/debug.hh"
#include "include/result.hh"
#include "include/tsc.hh"
#include "include/util.hh"

using namespace std;

static void run_ycsb(TxExecutor &tx, YcsbWorkload &workload) {
#if ADD_ANALYSIS
  uint64_t start = rdtscp();
#endif
  makeProcedure(tx.pro_set_, workload.rnd_, workload.zipf_);
#if ADD_ANALYSIS
  tx.result_->local_make_procedure_latency_ += rdtscp() - start;
#endif

RETRY:
  if (tx.isLeader()) {
    tx.leaderWork();
  }

  if (loadAcquire(tx.quit_)) return;

  tx.begin();
  std::vector<SimpleKey<8>> keys(tx.pro_set_.size());
  std::vector<HeapObject> objs(tx.pro_set_.size());
  size_t i = 0;
  for (auto &pro : tx.pro_set_) {
    YCSB::CreateKey(pro.key_, keys[i].ptr());
    if (pro.ope_ == Ope::READ) {
      TupleBody *body;
      Status stat = tx.read(Storage::YCSB, keys[i].view(), &body);
      if (stat != Status::OK) {
        tx.abort();
        goto RETRY;
      }
      if (tx.status_ != TransactionStatus::aborted) {
        YCSB &t = body->get_value().cast_to<YCSB>();
        (void)t;
      }
    } else if (pro.ope_ == Ope::WRITE) {
      objs[i].template allocate<YCSB>();
      YCSB &t = objs[i].ref();
      (void)t;
      Status stat = tx.write(Storage::YCSB, keys[i].view(),
                             TupleBody(keys[i].view(), std::move(objs[i])));
      if (stat != Status::OK) {
        tx.abort();
        goto RETRY;
      }
    } else if (pro.ope_ == Ope::READ_MODIFY_WRITE) {
      TupleBody *body;
      Status stat = tx.read(Storage::YCSB, keys[i].view(), &body);
      if (stat != Status::OK) {
        tx.abort();
        goto RETRY;
      }
      if (tx.status_ != TransactionStatus::aborted) {
        YCSB &old_tuple = body->get_value().cast_to<YCSB>();
        objs[i].template allocate<YCSB>();
        YCSB &new_tuple = objs[i].ref();
        memcpy(new_tuple.val_, old_tuple.val_, VAL_SIZE);
        stat = tx.write(Storage::YCSB, keys[i].view(),
                        TupleBody(keys[i].view(), std::move(objs[i])));
        if (stat != Status::OK) {
          tx.abort();
          goto RETRY;
        }
      }
    } else {
      ERR;
    }

    if (tx.status_ == TransactionStatus::aborted) {
      tx.abort();
#if ADD_ANALYSIS
      ++tx.result_->local_early_aborts_;
#endif
      goto RETRY;
    }

    ++i;
  }

  if (!tx.commit()) {
    tx.abort();
    goto RETRY;
  }

  storeRelease(tx.result_->local_commit_counts_,
               loadAcquire(tx.result_->local_commit_counts_) + 1);
}

void worker(size_t thid, char &ready, const bool &start, const bool &quit) {
  Result &myres = std::ref(SS2PLResult[thid]);
  TxExecutor trans(thid, (Result *) &myres, quit);
  YcsbWorkload workload;

#ifdef Linux
  setThreadAffinity(thid);
#endif  // Linux

  storeRelease(ready, 1);
  while (!loadAcquire(start)) cpu_relax();
  while (!loadAcquire(quit)) {
    run_ycsb(trans, workload);
  }

  return;
}

int main(int argc, char *argv[]) try {
  gflags::SetUsageMessage("YCSB SS2PL standalone benchmark (no Masstree).");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  chkArg();
  YcsbWorkload::displayWorkloadParameter();
  makeDB();

  alignas(CACHE_LINE_SIZE) bool start = false;
  alignas(CACHE_LINE_SIZE) bool quit = false;
  initResult();
  std::vector<char> readys(TotalThreadNum);
  std::vector<std::thread> thv;
  for (size_t i = 0; i < TotalThreadNum; ++i)
    thv.emplace_back(worker, i, std::ref(readys[i]), std::ref(start),
                     std::ref(quit));
  waitForReady(readys);
  storeRelease(start, true);
  for (size_t i = 0; i < FLAGS_extime; ++i) {
    sleepMs(1000);
  }
  storeRelease(quit, true);
  for (auto &th : thv) th.join();

  for (unsigned int i = 0; i < TotalThreadNum; ++i) {
    SS2PLResult[0].addLocalAllResult(SS2PLResult[i]);
  }
  ShowOptParameters();
  SS2PLResult[0].displayAllResult(FLAGS_clocks_per_us, FLAGS_extime, TotalThreadNum);

  return 0;
} catch (const std::bad_alloc&) {
  ERR;
}
