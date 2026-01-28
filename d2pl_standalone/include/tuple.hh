#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>

#include "cache_line_size.hh"
#include "inline.hh"
#include "lock.hh"
#include "tuple_body.hh"

using namespace std;

class Tuple {
public:
  alignas(CACHE_LINE_SIZE) RWLock lock_;
  TupleBody body_;
  std::atomic<bool> present_{false};

  Tuple() {}

  void init([[maybe_unused]] size_t thid, TupleBody&& body, [[maybe_unused]] void* p) {
    body_ = std::move(body);
    present_.store(true, std::memory_order_release);
  }

  void init(TupleBody&& body) {
    body_ = std::move(body);
    present_.store(true, std::memory_order_release);
    lock_.w_lock();
  }
};
