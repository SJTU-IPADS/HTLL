#include <asm-generic/errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <malloc.h>
#include <limits.h>
#include <htll.h>
#include <libhtll.h>
#include <sched.h>
#include "interpose.h"
#include "utils.h"


extern __thread unsigned int cur_thread_id;
#define MAX_SEGMENT 256

typedef struct {
  uint64_t wait_time;
  uint64_t unit;
  uint64_t has_waiter;
  uint64_t start_ts;
  uint64_t quick_start;
} segment_t;

__thread segment_t segment[MAX_SEGMENT] = {0};

__thread int cur_segment_id = -1;

static inline int sys_futex(void *addr1, int op, int val1,
                            struct timespec *timeout, void *addr2, int val3) {
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

htll_mutex_t *htll_mutex_create(const pthread_mutexattr_t *attr) {
  htll_mutex_t *impl = (htll_mutex_t *)alloc_cache_align(sizeof(htll_mutex_t));
  impl->l.u = 0;
  impl->ticks_spin = HTLL_SPIN_TRIES_LOCK;
  impl->cnt_unlock = 0;
  impl->cnt_wake = 0;
  impl->flag = 0;
  return impl;
}

int htll_mutex_destroy(htll_mutex_t *m) {
  /* Do nothing */
  (void)m;
  return 0;
}

int htll_mutex_lock(htll_mutex_t *m, htll_context_t *me) {
  int spin_ticks = m->ticks_spin;
  int sleep_time = segment[cur_segment_id].wait_time;
  int flag = (cur_segment_id == -1 ? 1 : 0);
  while (1) {
    if (!htll_swap_uint8(&m->l.b.locked, LOCKED)) {
      return 0;
    }

    HTLL_FOR_N_CYCLES(
        spin_ticks,
        if (!htll_swap_uint8(&m->l.b.locked, LOCKED)) { return 0; });

    /* Have to sleep */
    if ((htll_swap_uint32(&m->l.u, LOCKED_AND_CONTENDED) & LOCKED) ==
        UNLOCKED) {
      return 0;
    }

    if (flag == 0) {
      sys_futex(m, FUTEX_WAIT_PRIVATE, 257,
                (struct timespec[]){{0, segment[cur_segment_id].wait_time}},
                NULL, 0);

      segment[cur_segment_id].has_waiter = 1;
      spin_ticks = spin_ticks * 2;
    } else {
      while (htll_swap_uint32(&m->l.u, 257) & 1) {
        sys_futex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
      }
      return 0;
    }
  }
  return 0;
}

#define __htll_unlikely(x) __builtin_expect((x), 0)
void adjust_spin_ticks(htll_mutex_t *m) {

  if (m->cnt_wake > WAKE_MAX_THRESHOLD)
    m->ticks_spin = m->ticks_spin + INCREASE_UNIT;
  else if (m->cnt_wake < WAKE_MIN_THRESHOLD)
    m->ticks_spin = m->ticks_spin - DECREASE_UNIT;
  m->cnt_wake = 0;
}

int htll_mutex_unlock(htll_mutex_t *m, htll_context_t *me) {
  /* Locked and not contended */
  if ((m->l.u == 1) && (__sync_val_compare_and_swap(&m->l.u, 1, 0) == 1)) {
    return 0;
  }

  m->cnt_unlock++;
  if (__htll_unlikely((m->cnt_unlock & ADJUST_THRESHOLD) == 0)) {
    adjust_spin_ticks(m);
  }

  /* Unlock */
  m->l.b.locked = UNLOCKED;
  asm volatile("mfence");
  if (m->l.b.locked) {
    return 0;
  }
  asm volatile("mfence");
  delay_ticks(HTLL_SPIN_TRIES_UNLOCK);
  asm volatile("mfence");
  if (m->l.b.locked == LOCKED) {
    return 0;
  }
  asm volatile("mfence");
  /* We need to wake someone up */
  m->l.b.contended = UNCONTENDED;
  m->cnt_wake++;
  sys_futex(m, FUTEX_WAKE_PRIVATE, LOCKED, NULL, NULL, 0);
  return 0;
}

int htll_mutex_trylock(htll_mutex_t *m, htll_context_t *me) {
  unsigned c = htll_swap_uint8(&m->l.b.locked, 1);
  if (!c)
    return 0;
  return EBUSY;
}

void htll_application_init(void) {}

void htll_application_exit(void) {}

void htll_thread_start(void) {
  for (int i = 0; i < MAX_SEGMENT; i++) {
    segment[i].wait_time = DEFAULT_REORDER;
    segment[i].unit = DEFAULT_ADJUST_UNIT;
  }
  cur_segment_id = -1;
}

void htll_thread_exit(void) {}

int htll_cond1_init(htll_cond1_t *c, const pthread_condattr_t *a) {
  (void)a;

  c->m = NULL;

  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->seq = 0;

  return 0;
}

int htll_cond1_destroy(htll_cond1_t *c) {
  /* No need to do anything */
  (void)c;
  return 0;
}

int htll_cond1_signal(htll_cond1_t *c) {
  /* We are waking someone up */
  __sync_fetch_and_add(&c->seq, 1);

  /* Wake up a thread */
  sys_futex(&c->seq, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

  return 0;
}
int htll_cond1_broadcast(htll_cond1_t *c) {
  htll_mutex_t *m = c->m;

  /* No mutex means that there are no waiters */
  if (!m)
    return 0;

  /* We are waking everyone up */
  __sync_fetch_and_add(&c->seq, 1);

  /* Wake one thread, and requeue the rest on the mutex */
  sys_futex(&c->seq, FUTEX_REQUEUE_PRIVATE, 1, (struct timespec *)INT_MAX, m,
            0);

  return 0;
}

int htll_cond1_wait(htll_cond1_t *c, htll_mutex_t *m,
                       htll_context_t *me) {
  int seq = c->seq;
  // htll_context_t * me;
  if (c->m != m) {
    if (c->m)
      return EINVAL;
    /* Atomically set mutex inside cv */
    __attribute__((unused)) int dummy =
        (uintptr_t)__sync_val_compare_and_swap(&c->m, NULL, m);
    if (c->m != m)
      return EINVAL;
  }

  htll_mutex_unlock(m, me);

  sys_futex(&c->seq, FUTEX_WAIT_PRIVATE, seq, NULL, NULL, 0);

  while (htll_swap_uint32(&m->l.b.locked, 257) & 1) {
    sys_futex(m, FUTEX_WAIT_PRIVATE, 257,
              (struct timespec[]){{0, segment[cur_segment_id].wait_time}}, NULL,
              0);

    segment[cur_segment_id].has_waiter = 1;
  }

  return 0;
}

int htll_cond_timedwait(htll_cond1_t *c, htll_mutex_t *m, htll_context_t *me,
                        const struct timespec *ts) {
  int ret = 0;
  int seq = c->seq;

  if (c->m != m) {
    if (c->m)
      return EINVAL;
    /* Atomically set mutex inside cv */
    __attribute__((unused)) int dummy =
        (uintptr_t)__sync_val_compare_and_swap(&c->m, NULL, m);
    if (c->m != m)
      return EINVAL;
  }

  htll_mutex_unlock(m, me);

  struct timespec rt;
  /* Get the current time.  So far we support only one clock.  */
  struct timeval tv;
  (void)gettimeofday(&tv, NULL);

  /* Convert the absolute timeout value to a relative timeout.  */
  rt.tv_sec = ts->tv_sec - tv.tv_sec;
  rt.tv_nsec = ts->tv_nsec - tv.tv_usec * 1000;

  if (rt.tv_nsec < 0) {
    rt.tv_nsec += 1000000000;
    --rt.tv_sec;
  }
  /* Did we already time out?  */
  if (__builtin_expect(rt.tv_sec < 0, 0)) {
    ret = ETIMEDOUT;
    goto timeout;
  }

  sys_futex(&c->seq, FUTEX_WAIT_PRIVATE, seq, &rt, NULL, 0);

  (void)gettimeofday(&tv, NULL);
  rt.tv_sec = ts->tv_sec - tv.tv_sec;
  rt.tv_nsec = ts->tv_nsec - tv.tv_usec * 1000;
  if (rt.tv_nsec < 0) {
    rt.tv_nsec += 1000000000;
    --rt.tv_sec;
  }

  if (rt.tv_sec < 0) {
    ret = ETIMEDOUT;
  }

timeout:
  while (htll_swap_uint32(&m->l.b.locked, 257) & 1) {
    sys_futex(m, FUTEX_WAIT_PRIVATE, 257,
              (struct timespec[]){{0, segment[cur_segment_id].wait_time}}, NULL,
              0);

    segment[cur_segment_id].has_waiter = 1;
  }

  return ret;
}

static inline int htll_mutex_timedlock(htll_mutex_t *l,
                                       const struct timespec *ts) {
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}

/* Epoch-based interface */
/* Per-thread private stack */

/* A stack to implement nested segment */
#define MAX_DEPTH 30
__thread int segment_stack[MAX_DEPTH];
__thread int stack_pos = -1;

int push_segment(int segment_id) {
  stack_pos++;
  if (stack_pos == MAX_SEGMENT)
    return -ENOSPC;
  segment_stack[stack_pos] = segment_id;
  return 0;
}

/* Per-thread private stack */
int pop_segment(void) {
  if (stack_pos < 0)
    return -EINVAL;
  return segment_stack[stack_pos--];
}

int is_stack_empty(void) { return stack_pos < 0; }

int segment_start(int segment_id) {
  if (segment_id < 0 || segment_id > MAX_SEGMENT || cur_segment_id < -1)
    return -EINVAL;
  if (push_segment(cur_segment_id) < 0)
    return -ENOSPC;
  /* Set cur_segment_id */
  cur_segment_id = segment_id;
  /* Get the segment start time */
  segment[cur_segment_id].start_ts = htll_getticks();
  return 0;
}

int segment_end(int segment_id, uint64_t required_latency) {
  uint64_t duration = 0;
  uint64_t segment_end_ts;
  uint64_t has_waiter = segment[cur_segment_id].has_waiter;
  uint64_t wait_time = segment[cur_segment_id].wait_time;
  uint64_t unit = segment[cur_segment_id].unit;
  segment_end_ts = htll_getticks();
  duration = segment_end_ts - segment[cur_segment_id].start_ts;
  if (has_waiter) {
    /* Fast out */
    if (required_latency < SEGMENT_REQ_THRESHOLD) {
      segment[cur_segment_id].wait_time = MIN_REORDER;
      goto out;
    }
    if (segment_id < 0 || segment_id > MAX_SEGMENT)
      return -EINVAL;
    if (segment_id != cur_segment_id)
      return -EINVAL;
    /* Adjust the reorder window */
    if (duration > required_latency) {
      wait_time = wait_time >> 1;
      unit = wait_time / 99;
      if (unit < MIN_ADJUST_UNIT)
        unit = MIN_ADJUST_UNIT;
    } else {
      wait_time += unit;
    }
    if (wait_time < MIN_REORDER)
      wait_time = MIN_REORDER;
    segment[cur_segment_id].wait_time = wait_time;
  }
  segment[cur_segment_id].has_waiter = 0;
  segment[cur_segment_id].unit = unit;
out:
  /* Support nested segmentes */
  if (is_stack_empty())
    cur_segment_id = -1;
  else
    cur_segment_id = pop_segment();
  return 0;
}
