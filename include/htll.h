/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
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
#ifndef __HTLL_H__
#define __HTLL_H__

#include "padding.h"
#define LOCK_ALGORITHM "HTLL"
#define NEED_CONTEXT 0
#define SUPPORT_WAITING 0

#define PADDING 1
#define HTLL_SPIN_TRIES_LOCK 8192
#define HTLL_SPIN_TRIES_UNLOCK 128

#define UNLOCKED 0
#define LOCKED 1
#define UNCONTENDED 0
#define CONTENDED 1
#define LOCKED_AND_CONTENDED 257

#define ADJUST_THRESHOLD 2047
#define WAKE_MAX_THRESHOLD 32
#define WAKE_MIN_THRESHOLD 16
#define INCREASE_UNIT 512
#define DECREASE_UNIT 256

#define DEFAULT_REORDER 10000
#define MIN_REORDER 10000
#define DEFAULT_ADJUST_UNIT 5000
#define MIN_ADJUST_UNIT 1000
#define SEGMENT_REQ_THRESHOLD 100

static inline void delay_ticks(const int cycles) {
  int cy = cycles;
  while (cy--) {
    asm volatile("");
  }
}

static inline uint32_t htll_swap_uint32(volatile uint32_t *target, uint32_t x) {
  asm volatile("xchgl %0,%1"
               : "=r"((uint32_t)x)
               : "m"(*(volatile uint32_t *)target), "0"((uint32_t)x)
               : "memory");

  return x;
}

static inline uint8_t htll_swap_uint8(volatile uint8_t *target, uint8_t x) {
  asm volatile("xchgb %0,%1"
               : "=r"((uint8_t)x)
               : "m"(*(volatile uint8_t *)target), "0"((uint8_t)x)
               : "memory");

  return x;
}

static inline uint64_t htll_getticks(void) {
  unsigned hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

#define HTLL_FOR_N_CYCLES(n, do)                                               \
  {                                                                            \
    uint64_t ___s = htll_getticks();                                           \
    while (1) {                                                                \
      do                                                                       \
        ;                                                                      \
      uint64_t ___e = htll_getticks();                                         \
      if ((___e - ___s) > n) {                                                 \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
  }
#define CACHE_LINE_SIZE 64

typedef __attribute__((aligned(CACHE_LINE_SIZE))) struct htll_lock {
  union {
    volatile unsigned u;
    struct {
      volatile unsigned char locked;
      volatile unsigned char contended;
    } b;
  } l;
  uint8_t padding[CACHE_LINE_SIZE - sizeof(unsigned)];
  unsigned int ticks_spin;
  uint8_t padding0[CACHE_LINE_SIZE - sizeof(unsigned)];
  unsigned int cnt_unlock;
  uint8_t padding1[CACHE_LINE_SIZE - sizeof(unsigned)];
  unsigned int cnt_wake;
  uint8_t padding2[CACHE_LINE_SIZE - sizeof(unsigned)];
  unsigned int flag;
  uint8_t padding3[CACHE_LINE_SIZE - sizeof(unsigned)];
} htll_mutex_t;

typedef struct htll_cond1 {
  htll_mutex_t *m;
  int seq;
  int pad;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} htll_cond1_t;

#define HTLL_COND1_INITIALIZER                                              \
  { NULL, 0, 0 }
typedef void *htll_context_t;

htll_mutex_t *htll_mutex_create(const pthread_mutexattr_t *attr);
int htll_mutex_lock(htll_mutex_t *impl, htll_context_t *me);
int htll_mutex_trylock(htll_mutex_t *impl, htll_context_t *me);
int htll_mutex_unlock(htll_mutex_t *impl, htll_context_t *me);
int htll_mutex_destroy(htll_mutex_t *lock);
int htll_cond_init(htll_cond1_t *cond, const pthread_condattr_t *attr);
int htll_cond_timedwait(htll_cond1_t *cond, htll_mutex_t *lock,
                        htll_context_t *me, const struct timespec *ts);
int htll_cond_wait(htll_cond1_t *cond, htll_mutex_t *lock,
                   htll_context_t *me);
int htll_cond_signal(htll_cond1_t *cond);
int htll_cond_destroy(htll_cond1_t *cond);
int htll_cond1_init(htll_cond1_t *c, const pthread_condattr_t *a);
int htll_cond_broadcast(htll_cond1_t *cond);
int htll_cond1_signal(htll_cond1_t *c);
int htll_cond1_broadcast(htll_cond1_t *c);
int htll_cond1_wait(htll_cond1_t *c, htll_mutex_t *m, htll_context_t *me);
int htll_cond1_signal(htll_cond1_t *c);
void htll_thread_start(void);
void htll_thread_exit(void);
void htll_application_init(void);
void htll_application_exit(void);

typedef htll_mutex_t lock_mutex_t;
typedef htll_context_t lock_context_t;
typedef htll_cond1_t lock_cond_t;

#define lock_mutex_create htll_mutex_create
#define lock_mutex_lock htll_mutex_lock
#define lock_mutex_trylock htll_mutex_trylock
#define lock_mutex_unlock htll_mutex_unlock
#define lock_mutex_destroy htll_mutex_destroy
#define lock_cond_init htll_cond1_init
#define lock_cond_timedwait htll_cond_timedwait
#define lock_cond_wait htll_cond1_wait
#define lock_cond_signal htll_cond1_signal
#define lock_cond_broadcast htll_cond1_broadcast
#define lock_cond_destroy htll_cond1_destroy
#define lock_thread_start htll_thread_start
#define lock_thread_exit htll_thread_exit
#define lock_application_init htll_application_init
#define lock_application_exit htll_application_exit
#define lock_init_context htll_init_context
#define PTHREAD_COND_INITIALIZER HTLL_COND1_INITIALIZER
#endif // __htll_H__
