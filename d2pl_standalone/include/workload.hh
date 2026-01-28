#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "cache_line_size.hh"

enum class Storage : std::uint32_t {
  YCSB = 0,
  Size,
};

inline uint32_t get_storage(Storage s) {
  return static_cast<std::uint32_t>(s);
}

template<size_t N>
struct SimpleKey {
  char data[N]; // not null-terminated.

  char *ptr() { return &data[0]; }

  const char *ptr() const { return &data[0]; }

  [[nodiscard]] std::string_view view() const {
    return std::string_view(&data[0], N);
  }

  int compare(const SimpleKey& rhs) const {
    return ::memcmp(data, rhs.data, N);
  }
  bool operator<(const SimpleKey& rhs) const {
    return compare(rhs) < 0;
  }
  bool operator==(const SimpleKey& rhs) const {
    return compare(rhs) == 0;
  }
};
