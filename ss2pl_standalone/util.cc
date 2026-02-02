#include <stdlib.h>
#include <sys/syscall.h>  // syscall(SYS_gettid),
#include <sys/types.h>    // syscall(SSY_gettid),
#include <unistd.h>       // syscall(SSY_gettid),

#include <atomic>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

#include "include/debug.hh"
#include "include/procedure.hh"
#include "include/random.hh"
#include "include/result.hh"
#include "include/util.hh"
#include "include/zipf.hh"
#include "include/heap_object.hh"
#include "include/common.hh"
#include "include/tuple.hh"
#include "include/util.hh"
#include "include/workload.hh"
#include "include/ycsb_workload.hh"

void chkArg() {
  TotalThreadNum = FLAGS_thread_num;

  // Keep generic flags aligned with YCSB flags for logging compatibility.
  FLAGS_tuple_num = FLAGS_ycsb_tuple_num;
  FLAGS_rratio = FLAGS_ycsb_rratio;
  FLAGS_max_ope = FLAGS_ycsb_max_ope;
  FLAGS_rmw = FLAGS_ycsb_rmw;
  FLAGS_zipf_skew = FLAGS_ycsb_zipf_skew;

  displayParameter();

  if (FLAGS_ycsb_rratio > 100) {
    ERR;
  }

  if (FLAGS_ycsb_zipf_skew >= 1) {
    cout << "FLAGS_ycsb_zipf_skew must be 0 ~ 0.999..." << endl;
    ERR;
  }

  if (FLAGS_clocks_per_us < 100) {
    cout << "CPU_MHZ is less than 100. are your really?" << endl;
    ERR;
  }

  if (FLAGS_ycsb_tuple_num == 0) {
    cout << "FLAGS_ycsb_tuple_num must be >= 1" << endl;
    ERR;
  }
}

void displayParameter() {
  cout << "#FLAGS_clocks_per_us:\t" << FLAGS_clocks_per_us << endl;
  cout << "#FLAGS_extime:\t\t" << FLAGS_extime << endl;
  cout << "#FLAGS_max_ope:\t\t" << FLAGS_max_ope << endl;
  cout << "#FLAGS_rmw:\t\t" << FLAGS_rmw << endl;
  cout << "#FLAGS_rratio:\t\t" << FLAGS_rratio << endl;
  cout << "#FLAGS_thread_num:\t" << FLAGS_thread_num << endl;
  cout << "#FLAGS_tuple_num:\t" << FLAGS_tuple_num << endl;
  cout << "#FLAGS_ycsb:\t\t" << FLAGS_ycsb << endl;
  cout << "#FLAGS_zipf_skew:\t" << FLAGS_zipf_skew << endl;
}

void partTableInit([[maybe_unused]] size_t thid, uint64_t start, uint64_t end) {
  for (auto i = start; i <= end; ++i) {
    SimpleKey<8> key;
    YCSB::CreateKey(i, key.ptr());

    HeapObject obj;
    obj.allocate<YCSB>();
    YCSB& ycsb_tuple = obj.ref();
    ycsb_tuple.id_ = i;
    std::memset(ycsb_tuple.val_, 'a', VAL_SIZE);

    Tuple* tmp = &Table[i];
    tmp->init(thid, TupleBody(key.view(), std::move(obj)), nullptr);
  }
}

void makeDB() {
  Table = new Tuple[FLAGS_ycsb_tuple_num];

  size_t maxthread = decideParallelBuildNumber(FLAGS_ycsb_tuple_num);

  std::vector<std::thread> thv;
  for (size_t i = 0; i < maxthread; ++i) {
    auto start = i * (FLAGS_ycsb_tuple_num / maxthread);
    auto end = (i + 1) * (FLAGS_ycsb_tuple_num / maxthread) - 1;
    thv.emplace_back(partTableInit, i, start, end);
  }
  for (auto &th : thv) th.join();
}

void ShowOptParameters() {
  cout << "#ShowOptParameters()"
       << ": ADD_ANALYSIS " << ADD_ANALYSIS
       << ": BACK_OFF " << BACK_OFF
#ifdef DLR0
       << ": DLR0 "
#elif defined DLR1
       << ": DLR1 "
#endif
       << ": MASSTREE_USE " << MASSTREE_USE
       << ": KEY_SIZE " << KEY_SIZE
       << ": KEY_SORT " << KEY_SORT
       << ": VAL_SIZE " << VAL_SIZE
       << endl;
}

bool chkSpan(struct timeval &start, struct timeval &stop, long threshold) {
  long diff = 0;
  diff += (stop.tv_sec - start.tv_sec) * 1000 * 1000 +
          (stop.tv_usec - start.tv_usec);
  if (diff > threshold)
    return true;
  else
    return false;
}

size_t decideParallelBuildNumber(size_t tuple_num) {
  // if table size is very small, it builds by single thread.
  if (tuple_num < 1000) return 1;

  auto hc = std::thread::hardware_concurrency();
  if (hc == 0) return 1;

  for (size_t i = hc; i > 0; --i) {
    if (tuple_num % i == 0) {
      return i;
    }
    if (i == 1) ERR;
  }

  return 1;
}

void displayRusageRUMaxrss() {
  struct rusage r{};
  if (getrusage(RUSAGE_SELF, &r) != 0) ERR;
  printf("maxrss:\t%ld kB\n", r.ru_maxrss);
}

bool isReady(const std::vector<char> &readys) {
  for (const char &b : readys) {
    if (!loadAcquire(b)) return false;
  }
  return true;
}

void waitForReady(const std::vector<char> &readys) {
  while (!isReady(readys)) {
    cpu_relax();
  }
}

void sleepMs(size_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void sleepMicroSec(size_t ms) {
  std::this_thread::sleep_for(std::chrono::microseconds(ms));
}
