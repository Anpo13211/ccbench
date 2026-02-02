#pragma once

#include <stdint.h>

#include "config.h"
#include "storage.h"
#include "zipf.h"
#include "rng.h"
#include "workload.h"

void parse_args(int argc, char **argv, Config *cfg);
void print_config(const Config *cfg);
void validate_config(const Config *cfg);

void make_db(const Config *cfg);

void make_procedure(Op *ops, size_t max_ope, const Config *cfg, Zipf *zipf, XorShift64 *rng);

void sleep_ms(uint64_t ms);
