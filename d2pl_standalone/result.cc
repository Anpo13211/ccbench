#include "include/result.hh"
#include "include/common.hh"

#include "include/cache_line_size.hh"

using namespace std;

alignas(CACHE_LINE_SIZE) std::vector<Result> D2PLResult;

void initResult() { D2PLResult.resize(FLAGS_thread_num); }
