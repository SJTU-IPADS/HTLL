/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
 *               2013 Tudor David
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of his software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "utils.h"

#ifdef ENABLE_LAZY_CHECK
__thread int core_type = -1;
__thread int lazy_cnt = 0;
#endif


// int current_numa_node(void) {
//     return judge_numa_node(sched_getcpu());
// }

int is_big_core(void) {
#ifdef ENABLE_LAZY_CHECK
    if (core_type == -1) {
        core_type = judge_big_core(sched_getcpu());
    } else if (lazy_cnt >= LAZY_CHECK_THRESHOLD) {
        lazy_cnt = 0;
        core_type = judge_big_core(sched_getcpu());
    } else {
        lazy_cnt ++;
    }
    return core_type;
#else
    return judge_big_core(sched_getcpu());
#endif
}

int update_core_type(void) {
#ifdef ENABLE_LAZY_CHECK
    lazy_cnt = 0;
    core_type = (sched_getcpu()) % 4;
    return core_type;
#else
    return judge_big_core(sched_getcpu());
#endif
}

inline void *alloc_cache_align(size_t n) {
    void *res = 0;
    if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
        fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
                (unsigned long long)cache_align(n));
        exit(-1);
    }
    return res;
}
