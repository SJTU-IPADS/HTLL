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
#ifndef __mcssteal_H__
#define __mcssteal_H__

#include "padding.h"
#define LOCK_ALGORITHM "MCSSTEAL"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1
// #define COND_VAR 1
#define S_FAST 2
#define S_SPINING 1
#define S_PARKING 0
typedef struct mcssteal_node {
    struct mcssteal_node *volatile next;
    char __pad[pad_to_cache_line(sizeof(struct mcssteal_node *))];
    int status;
    char __pad1[pad_to_cache_line(sizeof(int))];
    int time;
    char __pad2[pad_to_cache_line(sizeof(int))];
    volatile int spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcssteal_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct mcssteal_mutex {
#ifdef COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
    int no_stealing;
    char __pad1[pad_to_cache_line(sizeof(int))];

    struct mcssteal_node *volatile tail __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcssteal_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t mcssteal_cond_t;
mcssteal_mutex_t *mcssteal_mutex_create(const pthread_mutexattr_t *attr);
int mcssteal_mutex_lock(mcssteal_mutex_t *impl, mcssteal_node_t *me);
int mcssteal_mutex_trylock(mcssteal_mutex_t *impl, mcssteal_node_t *me);
void mcssteal_mutex_unlock(mcssteal_mutex_t *impl, mcssteal_node_t *me);
int mcssteal_mutex_destroy(mcssteal_mutex_t *lock);
int mcssteal_cond_init(mcssteal_cond_t *cond, const pthread_condattr_t *attr);
int mcssteal_cond_timedwait(mcssteal_cond_t *cond, mcssteal_mutex_t *lock, mcssteal_node_t *me,
                       const struct timespec *ts);
int mcssteal_cond_wait(mcssteal_cond_t *cond, mcssteal_mutex_t *lock, mcssteal_node_t *me);
int mcssteal_cond_signal(mcssteal_cond_t *cond);
int mcssteal_cond_broadcast(mcssteal_cond_t *cond);
int mcssteal_cond_destroy(mcssteal_cond_t *cond);
void mcssteal_thread_start(void);
void mcssteal_thread_exit(void);
void mcssteal_application_init(void);
void mcssteal_application_exit(void);
void mcssteal_init_context(mcssteal_mutex_t *impl, mcssteal_node_t *context, int number);

typedef mcssteal_mutex_t lock_mutex_t;
typedef mcssteal_node_t lock_context_t;
typedef mcssteal_cond_t lock_cond_t;

#define lock_mutex_create mcssteal_mutex_create
#define lock_mutex_lock mcssteal_mutex_lock
#define lock_mutex_trylock mcssteal_mutex_trylock
#define lock_mutex_unlock mcssteal_mutex_unlock
#define lock_mutex_destroy mcssteal_mutex_destroy
#define lock_cond_init mcssteal_cond_init
#define lock_cond_timedwait mcssteal_cond_timedwait
#define lock_cond_wait mcssteal_cond_wait
#define lock_cond_signal mcssteal_cond_signal
#define lock_cond_broadcast mcssteal_cond_broadcast
#define lock_cond_destroy mcssteal_cond_destroy
#define lock_thread_start mcssteal_thread_start
#define lock_thread_exit mcssteal_thread_exit
#define lock_application_init mcssteal_application_init
#define lock_application_exit mcssteal_application_exit
#define lock_init_context mcssteal_init_context

#endif // __mcssteal_H__
