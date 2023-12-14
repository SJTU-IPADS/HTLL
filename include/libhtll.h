#pragma once
#include <stdint.h>

int segment_start(int segment_id);
int segment_end(int segment_id, uint64_t required_latency);

