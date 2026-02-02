#include "include/gflags/gflags.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::string usage_message;
std::vector<std::string> argv_storage;
std::vector<char*> argv_ptr_storage;

std::vector<gflags::FlagEntry>& registry() {
  static std::vector<gflags::FlagEntry> flags;
  return flags;
}

std::string toLower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool parseBool(const std::string& value, bool& out) {
  if (value.empty()) {
    out = true;
    return true;
  }
  std::string lower = toLower(value);
  if (lower == "1" || lower == "true" || lower == "t" || lower == "yes" || lower == "y") {
    out = true;
    return true;
  }
  if (lower == "0" || lower == "false" || lower == "f" || lower == "no" || lower == "n") {
    out = false;
    return true;
  }
  return false;
}

bool matchesHelpFilter(const gflags::FlagEntry& entry,
                       const std::string& pattern,
                       bool use_regex,
                       bool match_on_file) {
  if (pattern.empty()) return true;
  if (match_on_file) {
    return entry.filename.find(pattern) != std::string::npos;
  }
  if (!use_regex) {
    return entry.name.find(pattern) != std::string::npos ||
           entry.description.find(pattern) != std::string::npos;
  }
  try {
    std::regex re(pattern);
    return std::regex_search(entry.name, re) ||
           std::regex_search(entry.description, re);
  } catch (const std::regex_error&) {
    return entry.name.find(pattern) != std::string::npos ||
           entry.description.find(pattern) != std::string::npos;
  }
}

void printUsage(const std::string& pattern = std::string(),
                bool use_regex = false,
                bool match_on_file = false) {
  if (!usage_message.empty()) {
    std::cout << usage_message << std::endl;
  }
  auto flags = registry();
  std::sort(flags.begin(), flags.end(),
            [](const gflags::FlagEntry& a, const gflags::FlagEntry& b) {
              return a.name < b.name;
            });
  for (const auto& f : flags) {
    if (!matchesHelpFilter(f, pattern, use_regex, match_on_file)) continue;
    std::cout << "  --" << f.name;
    if (f.type == gflags::FlagType::BOOL) {
      std::cout << " (bool, use --no" << f.name << " to set false)";
    }
    if (!f.default_value.empty()) {
      std::cout << " (default: " << f.default_value << ")";
    }
    if (!f.description.empty()) {
      std::cout << "\t" << f.description;
    }
    std::cout << std::endl;
  }
  std::cout << "  --help/--helpshort/--helpfull\tshow this message" << std::endl;
  std::cout << "  --helpon=FILE\tshow help for flags from matching file" << std::endl;
  std::cout << "  --helpmatch=REGEX\tshow help for flags matching pattern" << std::endl;
  std::cout << "  --helpflags/--helppackage\tshow help for flags/package" << std::endl;
  std::cout << "  --undefok[=f1,f2,...]\tallow unknown flags" << std::endl;
  std::cout << "  --flagfile=PATH\tload flags from file" << std::endl;
  std::cout << "  --fromenv=f1,f2,...\tload flags from env vars" << std::endl;
  std::cout << "  --tryfromenv=f1,f2,...\tlike fromenv, but skip missing" << std::endl;
}

std::string trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  return s.substr(start, end - start);
}

std::vector<std::string> splitWhitespace(const std::string& line) {
  std::istringstream iss(line);
  std::vector<std::string> tokens;
  std::string tok;
  while (iss >> tok) {
    tokens.push_back(tok);
  }
  return tokens;
}

std::vector<std::string> readFlagFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::cerr << "ERROR: cannot open flagfile: " << path << std::endl;
    std::exit(1);
  }

  std::vector<std::string> tokens;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line.rfind("#", 0) == 0) continue;
    if (line.rfind("//", 0) == 0) continue;
    auto line_tokens = splitWhitespace(line);
    for (auto& t : line_tokens) {
      if (!t.empty() && t[0] != '-') {
        t = "--" + t;
      }
      tokens.push_back(t);
    }
  }
  return tokens;
}

void applyFromEnv(const std::string& list, bool allow_missing);

gflags::FlagEntry* findFlag(const std::string& name) {
  for (auto& f : registry()) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

bool parseUInt64(const std::string& value, uint64_t& out) {
  std::string trimmed = trim(value);
  if (trimmed.empty()) return false;
  if (trimmed[0] == '-') return false;
  errno = 0;
  char* end = nullptr;
  unsigned long long v = std::strtoull(trimmed.c_str(), &end, 10);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') return false;
  if (v > std::numeric_limits<uint64_t>::max()) return false;
  out = static_cast<uint64_t>(v);
  return true;
}

bool parseUInt32(const std::string& value, uint32_t& out) {
  uint64_t v = 0;
  if (!parseUInt64(value, v)) return false;
  if (v > std::numeric_limits<uint32_t>::max()) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

bool parseInt64(const std::string& value, int64_t& out) {
  std::string trimmed = trim(value);
  if (trimmed.empty()) return false;
  errno = 0;
  char* end = nullptr;
  long long v = std::strtoll(trimmed.c_str(), &end, 10);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') return false;
  if (v < std::numeric_limits<int64_t>::min() || v > std::numeric_limits<int64_t>::max()) return false;
  out = static_cast<int64_t>(v);
  return true;
}

bool parseInt32(const std::string& value, int32_t& out) {
  int64_t v = 0;
  if (!parseInt64(value, v)) return false;
  if (v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max()) return false;
  out = static_cast<int32_t>(v);
  return true;
}

bool parseDouble(const std::string& value, double& out) {
  std::string trimmed = trim(value);
  if (trimmed.empty()) return false;
  errno = 0;
  char* end = nullptr;
  double v = std::strtod(trimmed.c_str(), &end);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') return false;
  out = v;
  return true;
}

void setFlagValue(gflags::FlagEntry& entry, const std::string& value, bool is_no_prefix, bool has_value) {
  using gflags::FlagType;
  switch (entry.type) {
    case FlagType::BOOL: {
      bool v = false;
      bool parsed = false;
      if (is_no_prefix) {
        v = false;
        parsed = true;
      } else if (!has_value) {
        v = true;
        parsed = true;
      } else {
        parsed = parseBool(value, v);
      }
      if (!parsed) {
        std::cerr << "ERROR: invalid bool value for --" << entry.name << std::endl;
        std::exit(1);
      }
      *reinterpret_cast<bool*>(entry.ptr) = v;
      break;
    }
    case FlagType::U32: {
      if (!has_value) {
        std::cerr << "ERROR: missing value for --" << entry.name << std::endl;
        std::exit(1);
      }
      uint32_t v = 0;
      if (!parseUInt32(value, v)) {
        std::cerr << "ERROR: invalid uint32 value for --" << entry.name << std::endl;
        std::exit(1);
      }
      *reinterpret_cast<uint32_t*>(entry.ptr) = v;
      break;
    }
    case FlagType::U64: {
      if (!has_value) {
        std::cerr << "ERROR: missing value for --" << entry.name << std::endl;
        std::exit(1);
      }
      uint64_t v = 0;
      if (!parseUInt64(value, v)) {
        std::cerr << "ERROR: invalid uint64 value for --" << entry.name << std::endl;
        std::exit(1);
      }
      *reinterpret_cast<uint64_t*>(entry.ptr) = v;
      break;
    }
    case FlagType::DOUBLE: {
      if (!has_value) {
        std::cerr << "ERROR: missing value for --" << entry.name << std::endl;
        std::exit(1);
      }
      double v = 0;
      if (!parseDouble(value, v)) {
        std::cerr << "ERROR: invalid double value for --" << entry.name << std::endl;
        std::exit(1);
      }
      *reinterpret_cast<double*>(entry.ptr) = v;
      break;
    }
    case FlagType::INT32: {
      if (!has_value) {
        std::cerr << "ERROR: missing value for --" << entry.name << std::endl;
        std::exit(1);
      }
      int32_t v = 0;
      if (!parseInt32(value, v)) {
        std::cerr << "ERROR: invalid int32 value for --" << entry.name << std::endl;
        std::exit(1);
      }
      *reinterpret_cast<int32_t*>(entry.ptr) = v;
      break;
    }
    case FlagType::INT64: {
      if (!has_value) {
        std::cerr << "ERROR: missing value for --" << entry.name << std::endl;
        std::exit(1);
      }
      int64_t v = 0;
      if (!parseInt64(value, v)) {
        std::cerr << "ERROR: invalid int64 value for --" << entry.name << std::endl;
        std::exit(1);
      }
      *reinterpret_cast<int64_t*>(entry.ptr) = v;
      break;
    }
  }
}

void applyFromEnv(const std::string& list, bool allow_missing) {
  std::istringstream iss(list);
  std::string name;
  while (std::getline(iss, name, ',')) {
    name = trim(name);
    if (name.empty()) continue;
    auto* entry = findFlag(name);
    if (!entry) {
      std::cerr << "ERROR: unknown flag in --fromenv: " << name << std::endl;
      std::exit(1);
    }
    const char* env = std::getenv(name.c_str());
    if (!env) {
      if (allow_missing) continue;
      std::cerr << "ERROR: env var not set for --fromenv: " << name << std::endl;
      std::exit(1);
    }
    setFlagValue(*entry, env, false, true);
  }
}

struct ParseState {
  bool allow_undefined = false;
  std::set<std::string> undefok_list;
};

bool isHelpToken(const std::string& token) {
  return token == "help" || token == "h" || token == "helpshort" || token == "helpfull" ||
         token == "helpon" || token == "helpmatch" || token == "helpflags" || token == "helppackage";
}

void parseTokens(const std::vector<std::string>& args,
                 std::vector<std::string>& out_args,
                 bool remove_flags,
                 ParseState& state,
                 int depth = 0) {
  if (depth > 10) {
    std::cerr << "ERROR: flagfile recursion too deep" << std::endl;
    std::exit(1);
  }

  bool end_of_flags = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (end_of_flags) {
      out_args.push_back(arg);
      continue;
    }

    if (arg == "--") {
      end_of_flags = true;
      if (!remove_flags) out_args.push_back(arg);
      continue;
    }

    if (arg == "-") {
      out_args.push_back(arg);
      continue;
    }

    if (arg.rfind("-", 0) != 0) {
      out_args.push_back(arg);
      continue;
    }

    std::string token = arg;
    if (token.rfind("--", 0) == 0) token = token.substr(2);
    else if (token.rfind("-", 0) == 0) token = token.substr(1);

    std::string name;
    std::string value;
    bool has_value = false;

    auto eq = token.find('=');
    if (eq == std::string::npos) {
      name = token;
    } else {
      name = token.substr(0, eq);
      value = token.substr(eq + 1);
      has_value = true;
    }

    if (isHelpToken(name)) {
      if (!has_value && i + 1 < args.size()) {
        if (name == "helpon" || name == "helpmatch") {
          value = args[++i];
          has_value = true;
        }
      }
      if (name == "helpon") {
        printUsage(value, false, true);
      } else if (name == "helpmatch") {
        printUsage(value, true, false);
      } else {
        printUsage();
      }
      std::exit(0);
    }

    if (name == "undefok") {
      if (has_value) {
        std::istringstream iss(value);
        std::string n;
        while (std::getline(iss, n, ',')) {
          n = trim(n);
          if (!n.empty()) state.undefok_list.insert(n);
        }
        if (state.undefok_list.empty()) {
          state.allow_undefined = true;
        }
      } else {
        state.allow_undefined = true;
      }
      if (!remove_flags) out_args.push_back(arg);
      continue;
    }

    if (name == "flagfile") {
      if (!has_value && i + 1 < args.size()) {
        value = args[++i];
        has_value = true;
      }
      if (!has_value) {
        std::cerr << "ERROR: missing value for --flagfile" << std::endl;
        std::exit(1);
      }
      auto file_tokens = readFlagFile(value);
      parseTokens(file_tokens, out_args, remove_flags, state, depth + 1);
      if (!remove_flags) out_args.push_back(arg);
      continue;
    }

    if (name == "fromenv" || name == "tryfromenv") {
      if (!has_value && i + 1 < args.size()) {
        value = args[++i];
        has_value = true;
      }
      if (!has_value) {
        std::cerr << "ERROR: missing value for --" << name << std::endl;
        std::exit(1);
      }
      applyFromEnv(value, name == "tryfromenv");
      if (!remove_flags) out_args.push_back(arg);
      continue;
    }

    bool is_no_prefix = false;
    gflags::FlagEntry* entry = findFlag(name);
    if (!entry && name.rfind("no", 0) == 0) {
      std::string maybe_name = name.substr(2);
      auto* cand = findFlag(maybe_name);
      if (cand && cand->type == gflags::FlagType::BOOL) {
        entry = cand;
        name = maybe_name;
        is_no_prefix = true;
      }
    }

    if (!entry) {
      bool allowed = state.allow_undefined;
      if (!allowed && !state.undefok_list.empty()) {
        allowed = state.undefok_list.count(name) > 0;
      }
      if (!allowed) {
        std::cerr << "ERROR: unknown flag: --" << name << std::endl;
        std::exit(1);
      }
      out_args.push_back(arg);
      continue;
    }

    if (!has_value && entry->type != gflags::FlagType::BOOL && i + 1 < args.size()) {
      value = args[++i];
      has_value = true;
    }

    setFlagValue(*entry, value, is_no_prefix, has_value);
    if (!remove_flags) out_args.push_back(arg);
  }
}

}  // namespace

namespace gflags {

void RegisterFlag(FlagEntry entry) {
  if (entry.name.empty()) return;
  for (const auto& f : registry()) {
    if (f.name == entry.name) {
      std::cerr << "ERROR: duplicate flag definition: " << entry.name << std::endl;
      std::exit(1);
    }
  }
  registry().push_back(std::move(entry));
}

const std::vector<FlagEntry>& Registry() {
  return registry();
}

void SetUsageMessage(const std::string& msg) {
  usage_message = msg;
}

void ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags) {
  if (argc == nullptr || argv == nullptr || *argv == nullptr) return;
  std::vector<std::string> args;
  args.reserve(*argc - 1);
  for (int i = 1; i < *argc; ++i) {
    if ((*argv)[i]) args.emplace_back((*argv)[i]);
  }

  ParseState state;
  std::vector<std::string> out_args;
  parseTokens(args, out_args, remove_flags, state);

  if (remove_flags) {
    argv_storage.clear();
    argv_storage.reserve(out_args.size() + 1);
    const char* prog = ((*argv)[0] != nullptr) ? (*argv)[0] : "";
    argv_storage.emplace_back(prog);
    for (const auto& s : out_args) {
      argv_storage.push_back(s);
    }
    argv_ptr_storage.clear();
    argv_ptr_storage.reserve(argv_storage.size() + 1);
    for (auto& s : argv_storage) {
      argv_ptr_storage.push_back(const_cast<char*>(s.c_str()));
    }
    argv_ptr_storage.push_back(nullptr);
    *argc = static_cast<int>(argv_storage.size());
    *argv = argv_ptr_storage.data();
  }
}

}  // namespace gflags
