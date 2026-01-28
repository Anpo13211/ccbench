#pragma once

#include <cstdint>
#include <thread>
#include <vector>

#include "cache_line_size.hh"
#include "procedure.hh"
#include "random.hh"
#include "util.hh"
#include "zipf.hh"
#include "workload.hh"

#include "gflags/gflags.h"

#ifdef GLOBAL_VALUE_DEFINE
DEFINE_bool(ycsb_rmw, false, "True means read modify write, false means blind write.");
DEFINE_uint64(ycsb_max_ope, 10, "Total number of operations per single transaction.");
DEFINE_uint64(ycsb_rratio, 50, "read ratio of single transaction.");
DEFINE_uint64(ycsb_tuple_num, 1000000, "Total number of records.");
DEFINE_double(ycsb_zipf_skew, 0, "zipf skew. 0 ~ 0.999...");
#else
DECLARE_bool(ycsb_rmw);
DECLARE_uint64(ycsb_max_ope);
DECLARE_uint64(ycsb_rratio);
DECLARE_uint64(ycsb_tuple_num);
DECLARE_double(ycsb_zipf_skew);
#endif

struct YCSB {
  alignas(CACHE_LINE_SIZE)
  std::uint64_t id_;
  char val_[VAL_SIZE];

  // Primary Key: key_
  // key size is 8 bytes.
  static void CreateKey(uint64_t id, char *out) {
    assign_as_bigendian(id, &out[0]);
  }

  void createKey(char *out) const { return CreateKey(id_, out); }

  [[nodiscard]] std::string_view view() const { return struct_str_view(*this); }
};

inline static void makeProcedure(std::vector<Procedure> &pro,
                                 Xoroshiro128Plus& rnd, FastZipf& zipf) {
  pro.clear();
  bool ronly_flag(true), wonly_flag(true);
  for (size_t i = 0; i < FLAGS_ycsb_max_ope; ++i) {
    uint64_t tmpkey;
    // decide access destination key.
    tmpkey = zipf() % FLAGS_ycsb_tuple_num;

    // decide operation type.
    if ((rnd.next() % 100) < FLAGS_ycsb_rratio) {
      wonly_flag = false;
      pro.emplace_back(Ope::READ, tmpkey);
    } else {
      ronly_flag = false;
      if (FLAGS_ycsb_rmw) {
        pro.emplace_back(Ope::READ_MODIFY_WRITE, tmpkey);
      } else {
        pro.emplace_back(Ope::WRITE, tmpkey);
      }
    }
  }

  (*pro.begin()).ronly_ = ronly_flag;
  (*pro.begin()).wonly_ = wonly_flag;

#if KEY_SORT
  std::sort(pro.begin(), pro.end());
#endif // KEY_SORT
}

class YcsbWorkload {
public:
  Xoroshiro128Plus rnd_;
  FastZipf zipf_;

  YcsbWorkload() {
    rnd_.init();
    FastZipf zipf(&rnd_, FLAGS_ycsb_zipf_skew, FLAGS_ycsb_tuple_num);
    zipf_ = zipf;
  }

  static void displayWorkloadParameter() {
    std::cout << "#FLAGS_ycsb_max_ope:\t" << FLAGS_ycsb_max_ope << std::endl;
    std::cout << "#FLAGS_ycsb_rmw:\t" << FLAGS_ycsb_rmw << std::endl;
    std::cout << "#FLAGS_ycsb_rratio:\t" << FLAGS_ycsb_rratio << std::endl;
    std::cout << "#FLAGS_ycsb_tuple_num:\t" << FLAGS_ycsb_tuple_num << std::endl;
    std::cout << "#FLAGS_ycsb_zipf_skew:\t" << FLAGS_ycsb_zipf_skew << std::endl;
  }
};
