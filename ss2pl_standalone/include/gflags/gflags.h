#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gflags {
enum class FlagType {
  U64,
  U32,
  BOOL,
  DOUBLE,
  INT32,
  INT64,
};

struct FlagEntry {
  std::string name;
  FlagType type;
  void* ptr;
  std::string description;
  std::string default_value;
  std::string filename;
  int line = 0;
};

void RegisterFlag(FlagEntry entry);
const std::vector<FlagEntry>& Registry();

struct FlagRegistrar {
  FlagRegistrar(const char* name,
                void* ptr,
                FlagType type,
                const char* desc,
                const char* def,
                const char* file,
                int line) {
    RegisterFlag(FlagEntry{name ? name : "",
                           type,
                           ptr,
                           desc ? desc : "",
                           def ? def : "",
                           file ? file : "",
                           line});
  }
};

void SetUsageMessage(const std::string& msg);
void ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags);
}  // namespace gflags

#define DEFINE_uint64(name, default_value, description) \
  uint64_t FLAGS_##name = default_value; \
  [[maybe_unused]] static gflags::FlagRegistrar FLAGS_registrar_##name(#name, &FLAGS_##name, gflags::FlagType::U64, description, #default_value, __FILE__, __LINE__)
#define DEFINE_uint32(name, default_value, description) \
  uint32_t FLAGS_##name = default_value; \
  [[maybe_unused]] static gflags::FlagRegistrar FLAGS_registrar_##name(#name, &FLAGS_##name, gflags::FlagType::U32, description, #default_value, __FILE__, __LINE__)
#define DEFINE_bool(name, default_value, description) \
  bool FLAGS_##name = default_value; \
  [[maybe_unused]] static gflags::FlagRegistrar FLAGS_registrar_##name(#name, &FLAGS_##name, gflags::FlagType::BOOL, description, #default_value, __FILE__, __LINE__)
#define DEFINE_double(name, default_value, description) \
  double FLAGS_##name = default_value; \
  [[maybe_unused]] static gflags::FlagRegistrar FLAGS_registrar_##name(#name, &FLAGS_##name, gflags::FlagType::DOUBLE, description, #default_value, __FILE__, __LINE__)
#define DEFINE_int32(name, default_value, description) \
  int32_t FLAGS_##name = default_value; \
  [[maybe_unused]] static gflags::FlagRegistrar FLAGS_registrar_##name(#name, &FLAGS_##name, gflags::FlagType::INT32, description, #default_value, __FILE__, __LINE__)
#define DEFINE_int64(name, default_value, description) \
  int64_t FLAGS_##name = default_value; \
  [[maybe_unused]] static gflags::FlagRegistrar FLAGS_registrar_##name(#name, &FLAGS_##name, gflags::FlagType::INT64, description, #default_value, __FILE__, __LINE__)

#define DECLARE_uint64(name) extern uint64_t FLAGS_##name
#define DECLARE_uint32(name) extern uint32_t FLAGS_##name
#define DECLARE_bool(name) extern bool FLAGS_##name
#define DECLARE_double(name) extern double FLAGS_##name
#define DECLARE_int32(name) extern int32_t FLAGS_##name
#define DECLARE_int64(name) extern int64_t FLAGS_##name

namespace google {
using gflags::ParseCommandLineFlags;
using gflags::SetUsageMessage;
}  // namespace google
