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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#ifdef MCS
#include <mcs.h>
#elif defined(HTLL)
#include <htll.h>
#elif defined(PTHREADINTERPOSE)
#include <pthreadinterpose.h>
#else
#error "No lock algorithm known"
#endif

#include "waiting_policy.h"
#include "utils.h"
#include "interpose.h"

// The NO_INDIRECTION flag allows disabling the pthread-to-lock hash table
// and directly calling the specific lock function
// See empty.c for example.

unsigned int last_thread_id;
__thread unsigned int cur_thread_id;
int spin_cnt = 0;
int park_cnt = 0;
int wake_cnt = 0;

#if !NO_INDIRECTION
typedef struct {
	lock_mutex_t *lock_lock;
	char __pad0[pad_to_cache_line(sizeof(lock_mutex_t *))];

#if NEED_CONTEXT
	lock_context_t lock_node[MAX_THREADS];
	char __pad1[pad_to_cache_line(sizeof(lock_context_t) * MAX_THREADS)];
#endif
}
lock_transparent_mutex_t;

/* ************** ONLY A NAIVE HASH IMPLMENTATION HERE ************* */
// Since CLHT is not avalible in ARM, we use a very simple hash table
// Can be replaced with other high-efficient HASH Table implementation

#define BUCKET_SIZE     40960
#define BUCKET_LENGTH   10
uint64_t my_hash(void *key)
{
	return (((uint64_t) key >> 4) + ((uint64_t) key >> 24)) & (BUCKET_SIZE -
								   1);
}

struct lock_table {
	void *key;
	void *value;
};
static struct lock_table pthread_lock_table[BUCKET_SIZE][BUCKET_LENGTH];

int hash_put(void *mutex, void *impl)
{
	uint64_t cur_hash = my_hash(mutex);

	for (int i = 0; i < BUCKET_LENGTH; i++) {
		if (pthread_lock_table[cur_hash][i].key == 0) {
			void *expected = 0;
			if (__atomic_compare_exchange_n
			    (&(pthread_lock_table[cur_hash][i].key), &expected,
			     mutex, 0, __ATOMIC_ACQUIRE,
			     __ATOMIC_ACQUIRE) == 1) {
				pthread_lock_table[cur_hash][i].value = impl;
				return 0;
			}
		}
	}
	return -1;
}

void *hash_get(void *mutex)
{
	uint64_t cur_hash = my_hash(mutex);

	for (int i = 0; i < BUCKET_LENGTH; i++) {
		if (pthread_lock_table[cur_hash][i].key == mutex) {
			while (pthread_lock_table[cur_hash][i].value == 0) {
				asm volatile ("nop":::"memory");
			}
			return (void *)pthread_lock_table[cur_hash][i].value;
		}
	}
	return NULL;
}

#endif

struct routine {
	void *(*fct)(void *);
	void *arg;
};

// With this flag enabled, the mutex_destroy function will be called on each
// alive lock
// at application exit (e.g., for printing statistics about a lock -- see
// src/concurrency.c)
#ifndef DESTROY_ON_EXIT
#define DESTROY_ON_EXIT 0
#endif

// With this flag enabled, SIGINT and SIGTERM are caught to call the destructor
// of the library (see interpose_exit below)
#ifndef CLEANUP_ON_SIGNAL
#define CLEANUP_ON_SIGNAL 0
#endif

#if !NO_INDIRECTION
int lock_cnt;
static lock_transparent_mutex_t *ht_lock_create(pthread_mutex_t * mutex,
						const pthread_mutexattr_t *
						attr)
{
	lock_transparent_mutex_t *impl = alloc_cache_align(sizeof *impl);
	impl->lock_lock = lock_mutex_create(attr);
#if NEED_CONTEXT
	lock_init_context(impl->lock_lock, impl->lock_node, MAX_THREADS);
#endif

	// If a lock is initialized statically and two threads acquire the locks at
	// the same time, then only one call to clht_put will succeed.
	// For the failing thread, we free the previously allocated mutex data
	// structure and do a lookup to retrieve the ones inserted by the successful
	// thread.
	if (hash_put(mutex, impl) == 0) {
		return impl;
	}
	printf("Hash Table FULL (LitL)\n");
	exit(-1);
	return NULL;
}

static lock_transparent_mutex_t *ht_lock_get(pthread_mutex_t * mutex)
{
	lock_transparent_mutex_t *impl =
	    (lock_transparent_mutex_t *) hash_get(mutex);
	if (impl == NULL) {
		impl = ht_lock_create(mutex, NULL);
	}
	return impl;
}
#endif

int (*REAL(pthread_mutex_init))(pthread_mutex_t * mutex,
				const pthread_mutexattr_t * attr)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_mutex_destroy))(pthread_mutex_t * mutex)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_mutex_lock))(pthread_mutex_t * mutex)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_mutex_timedlock))(pthread_mutex_t * mutex,
				     const struct timespec * abstime)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_mutex_trylock))(pthread_mutex_t * mutex)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_mutex_unlock))(pthread_mutex_t * mutex)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_create))(pthread_t * thread, const pthread_attr_t * attr,
			    void *(*start_routine)(void *), void *arg)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_cond_init))(pthread_cond_t * cond,
			       const pthread_condattr_t * attr)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_cond_destroy))(pthread_cond_t * cond)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_cond_timedwait))(pthread_cond_t * cond,
				    pthread_mutex_t * mutex,
				    const struct timespec * abstime)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_cond_wait))(pthread_cond_t * cond, pthread_mutex_t * mutex)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_cond_signal))(pthread_cond_t * cond)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_cond_broadcast))(pthread_cond_t * cond)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));

    // spin locks
int (*REAL(pthread_spin_init))(pthread_spinlock_t * lock, int pshared)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_spin_destroy))(pthread_spinlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_spin_lock))(pthread_spinlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_spin_trylock))(pthread_spinlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_spin_unlock))(pthread_spinlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));

    // rw locks
int (*REAL(pthread_rwlock_init))(pthread_rwlock_t * lock,
				 const pthread_rwlockattr_t * attr)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_destroy))(pthread_rwlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_rdlock))(pthread_rwlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_wrlock))(pthread_rwlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_timedrdlock))(pthread_rwlock_t * lock,
					const struct timespec * abstime)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_timedwrlock))(pthread_rwlock_t * lock,
					const struct timespec * abstime)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_tryrdlock))(pthread_rwlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_trywrlock))(pthread_rwlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));
int (*REAL(pthread_rwlock_unlock))(pthread_rwlock_t * lock)
    __attribute__((aligned(L_CACHE_LINE_SIZE)));

#if CLEANUP_ON_SIGNAL
static void signal_exit(int signo);
#endif

volatile uint8_t init_spinlock = 0;

static void __attribute__((constructor)) REAL(interpose_init) (void) {
#if !(SUPPORT_WAITING) && !(defined(WAITING_ORIGINAL))
#error "Trying to compile a lock algorithm with a generic waiting policy."
#endif

	// Init once, other concurrent threads wait
	// 0 = not initiated
	// 1 = initializing
	// 2 = already initiated
	uint8_t cur_init = __sync_val_compare_and_swap(&init_spinlock, 0, 1);
	if (cur_init == 1) {
		while (init_spinlock == 1) {
			CPU_PAUSE();
		}
		return;
	} else if (cur_init == 2) {
		return;
	}

	// printf("Using Lib%s with waiting %s\n", LOCK_ALGORITHM, WAITING_POLICY);
#if !NO_INDIRECTION
	// pthread_to_lock = clht_create(NUM_BUCKETS);
	memset(pthread_lock_table, 0,
	       sizeof(struct lock_table) * BUCKET_SIZE * BUCKET_LENGTH);
	// assert(pthread_to_lock != NULL);
#endif

	// The main thread should also have an ID
	cur_thread_id = __sync_fetch_and_add(&last_thread_id, 1);
	if (cur_thread_id >= MAX_THREADS) {
		fprintf(stderr,
			"Maximum number of threads reached. Consider raising "
			"MAX_THREADS in interpose.c\n");
		exit(-1);
	}
#if !NO_INDIRECTION
	// clht_gc_thread_init(pthread_to_lock, cur_thread_id);
#endif

	lock_application_init();

#if CLEANUP_ON_SIGNAL
	// Signal handler for destroying locks at then end
	// We can't batch the registrations of the handler with a single syscall
	if (signal(SIGINT, signal_exit) == SIG_ERR) {
		fprintf(stderr,
			"Unable to install signal handler to catch SIGINT\n");
		abort();
	}

	if (signal(SIGTERM, signal_exit) == SIG_ERR) {
		fprintf(stderr,
			"Unable to install signal handler to catch SIGTERM\n");
		abort();
	}
#endif
	LOAD_FUNC(pthread_mutex_init, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_mutex_destroy, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_mutex_lock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_mutex_timedlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_mutex_trylock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_mutex_unlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_cond_timedwait, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_cond_wait, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_cond_broadcast, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_cond_destroy, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_cond_init, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_cond_signal, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_create, 1, FCT_LINK_SUFFIX);

	LOAD_FUNC(pthread_spin_init, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_spin_destroy, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_spin_lock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_spin_trylock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_spin_unlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_init, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_destroy, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_rdlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_wrlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_timedrdlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_timedwrlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_tryrdlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_trywrlock, 1, FCT_LINK_SUFFIX);
	LOAD_FUNC(pthread_rwlock_unlock, 1, FCT_LINK_SUFFIX);

	__sync_synchronize();
	init_spinlock = 2;
}

static void __attribute__((destructor)) REAL(interpose_exit) (void) {
	lock_application_exit();
}

#if !NO_INDIRECTION
static inline lock_context_t *get_node(lock_transparent_mutex_t * impl)
{
#if NEED_CONTEXT
	return &impl->lock_node[cur_thread_id];
#else
	return NULL;
#endif
};
#endif

#if CLEANUP_ON_SIGNAL
static void signal_exit(int UNUSED(signo))
{
	fprintf(stderr, "Signal received\n");
	exit(-1);
}
#endif

static void *lp_start_routine(void *_arg)
{
	struct routine *r = _arg;
	void *(*fct)(void *) = r->fct;
	void *arg = r->arg;
	void *res;

	// free(r);

	cur_thread_id = __sync_fetch_and_add(&last_thread_id, 1);

	if (cur_thread_id >= MAX_THREADS) {
		fprintf(stderr,
			"Maximum number of threads reached. Consider raising "
			"MAX_THREADS in interpose.c (current = %u)\n",
			MAX_THREADS);
		exit(-1);
	}
#if !NO_INDIRECTION
	// clht_gc_thread_init(pthread_to_lock, cur_thread_id);
#endif
	lock_thread_start();
	res = fct(arg);
	lock_thread_exit();
	return res;
}

int pthread_create(pthread_t * thread, const pthread_attr_t * attr,
		   void *(*start_routine)(void *), void *arg)
{
	DEBUG_PTHREAD("[p] pthread_create\n");
	struct routine *r = malloc(sizeof(struct routine));
	r->fct = start_routine;
	r->arg = arg;
	return REAL(pthread_create) (thread, attr, lp_start_routine, r);
}

int pthread_mutex_init(pthread_mutex_t * mutex,
		       const pthread_mutexattr_t * attr)
{
	DEBUG_PTHREAD("[p] pthread_mutex_init\n");
	// if (unlikely(!pthread_to_lock))
	// REAL(interpose_init)();
#if !NO_INDIRECTION
	ht_lock_create(mutex, attr);
	return 0;
#else
	return REAL(pthread_mutex_init) (mutex, attr);
#endif
}

int pthread_mutex_destroy(pthread_mutex_t * mutex)
{
	DEBUG_PTHREAD("[p] pthread_mutex_destroy\n");
#if !NO_INDIRECTION
	printf("spin %d park %d wake %d\n", spin_cnt, park_cnt, wake_cnt);
    return 0; /* Not impl */
#else
	return lock_mutex_destroy(mutex);
#endif
}

int pthread_mutex_lock(pthread_mutex_t * mutex)
{
	DEBUG_PTHREAD("[p] pthread_mutex_lock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get(mutex);
	return lock_mutex_lock(impl->lock_lock, get_node(impl));
#else
	return lock_mutex_lock(mutex, NULL);
#endif
}

int pthread_mutex_timedlock(pthread_mutex_t * mutex,
			    const struct timespec *abstime)
{
	assert(0 && "Timed locks not supported");
}

int pthread_mutex_trylock(pthread_mutex_t * mutex)
{
	DEBUG_PTHREAD("[p] pthread_mutex_trylock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get(mutex);
	return lock_mutex_trylock(impl->lock_lock, get_node(impl));
#else
	return lock_mutex_trylock(mutex, NULL);
#endif
}

int pthread_mutex_unlock(pthread_mutex_t * mutex)
{
	DEBUG_PTHREAD("[p] pthread_mutex_unlock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get(mutex);
	lock_mutex_unlock(impl->lock_lock, get_node(impl));
	return 0;
#else
	lock_mutex_unlock(mutex, NULL);
	return 0;
#endif
}

int pthread_cond_init(pthread_cond_t * cond, const pthread_condattr_t * attr)
{
	DEBUG_PTHREAD("[p] pthread_cond_init\n");
	return lock_cond_init(cond, attr);
}

// __asm__(".symver __pthread_cond_init,pthread_cond_init@@" GLIBC_2_3_2);

int pthread_cond_destroy(pthread_cond_t * cond)
{
	DEBUG_PTHREAD("[p] pthread_cond_destroy\n");
	return lock_cond_destroy(cond);
}

// __asm__(".symver __pthread_cond_destroy,pthread_cond_destroy@@" GLIBC_2_3_2);

int pthread_cond_timedwait(pthread_cond_t * cond, pthread_mutex_t * mutex,
			   const struct timespec *abstime)
{
	DEBUG_PTHREAD("[p] pthread_cond_timedwait\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get(mutex);
	return lock_cond_timedwait(cond, impl->lock_lock, get_node(impl),
				   abstime);
#else
	return lock_cond_timedwait(cond, mutex, NULL, abstime);
#endif
}

// __asm__(".symver __pthread_cond_timedwait,pthread_cond_timedwait@@"
// GLIBC_2_3_2);

int pthread_cond_wait(pthread_cond_t * cond, pthread_mutex_t * mutex)
{
	DEBUG_PTHREAD("[p] pthread_cond_wait\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get(mutex);
	return lock_cond_wait(cond, impl->lock_lock, get_node(impl));
#else
	return lock_cond_wait(cond, mutex, NULL);
#endif
}

// __asm__(".symver __pthread_cond_wait,pthread_cond_wait@@" GLIBC_2_3_2);

int pthread_cond_signal(pthread_cond_t * cond)
{
	DEBUG_PTHREAD("[p] pthread_cond_signal\n");
	return lock_cond_signal(cond);
}

// __asm__(".symver __pthread_cond_signal,pthread_cond_signal@@" GLIBC_2_3_2);

int pthread_cond_broadcast(pthread_cond_t * cond)
{
	DEBUG_PTHREAD("[p] pthread_cond_broadcast\n");
	return lock_cond_broadcast(cond);
}

// __asm__(".symver __pthread_cond_broadcast,pthread_cond_broadcast@@"
// GLIBC_2_3_2);

// // Spinlocks
// int pthread_spin_init(pthread_spinlock_t * spin, int pshared)
// {
// 	DEBUG_PTHREAD("[p] pthread_spin_init\n");
// 	if (init_spinlock != 2) {
// 		REAL(interpose_init) ();
// 	}
// #if !NO_INDIRECTION
// 	ht_lock_create((void *)spin, NULL);
// 	return 0;
// #else
// 	return REAL(pthread_spin_init) (spin, pshared);
// #endif
// }

// int pthread_spin_destroy(pthread_spinlock_t * spin)
// {
// 	DEBUG_PTHREAD("[p] pthread_spin_destroy\n");
// #if !NO_INDIRECTION
// 	return 0;
// #else
// 	assert(0 && "spinlock not supported without indirection");
// #endif
// }

// int pthread_spin_lock(pthread_spinlock_t * spin)
// {
// 	DEBUG_PTHREAD("[p] pthread_spin_lock\n");
// #if !NO_INDIRECTION
// 	lock_transparent_mutex_t *impl = ht_lock_get((void *)spin);
// 	return lock_mutex_lock(impl->lock_lock, get_node(impl));
// #else
// 	assert(0 && "spinlock not supported without indirection");
// #endif
// }

// int pthread_spin_trylock(pthread_spinlock_t * spin)
// {
// 	DEBUG_PTHREAD("[p] pthread_spin_trylock\n");
// #if !NO_INDIRECTION
// 	lock_transparent_mutex_t *impl = ht_lock_get((void *)spin);
// 	return lock_mutex_trylock(impl->lock_lock, get_node(impl));
// #else
// 	assert(0 && "spinlock not supported without indirection");
// #endif
// }

// int pthread_spin_unlock(pthread_spinlock_t * spin)
// {
// 	DEBUG_PTHREAD("[p] pthread_spin_unlock\n");
// #if !NO_INDIRECTION
// 	lock_transparent_mutex_t *impl = ht_lock_get((void *)spin);
// 	lock_mutex_unlock(impl->lock_lock, get_node(impl));
// 	return 0;
// #else
// 	assert(0 && "spinlock not supported without indirection");
// #endif
// }

#if defined(RWTAS)

#if !NO_INDIRECTION
typedef struct {
	lock_rwlock_t *lock_lock;
	char __pad[pad_to_cache_line(sizeof(lock_rwlock_t *))];
#if NEED_CONTEXT
	lock_context_t lock_node[MAX_THREADS];
#endif
}
lock_transparent_rwlock_t;

static lock_transparent_rwlock_t *ht_rwlock_create(pthread_rwlock_t * rwlock,
						   const pthread_rwlockattr_t *
						   attr)
{
	lock_transparent_rwlock_t *impl = alloc_cache_align(sizeof *impl);
	impl->lock_lock = lock_rwlock_create(attr);
#if NEED_CONTEXT
	lock_init_context(impl->lock_lock, impl->lock_node, MAX_THREADS);
#endif

	// If a lock is initialized statically and two threads acquire the locks at
	// the same time, then only one call to clht_put will succeed.
	// For the failing thread, we free the previously allocated mutex data
	// structure and do a lookup to retrieve the ones inserted by the successful
	// thread.
	if (hash_put(rwlock, impl) == 0) {
		return impl;
	}
	printf("Hash Table FULL (LitL)\n");
	exit(-1);
	return NULL;
}

static lock_transparent_rwlock_t *ht_rwlock_get(pthread_rwlock_t * rwlock)
{
	lock_transparent_rwlock_t *impl =
	    (lock_transparent_rwlock_t *) hash_get(rwlock);
	if (impl == NULL) {
		impl = ht_rwlock_create(rwlock, NULL);
	}

	return impl;
}

static inline lock_context_t *get_rwlock_node(lock_transparent_rwlock_t * impl)
{
#if NEED_CONTEXT
	return &impl->lock_node[cur_thread_id];
#else
	return NULL;
#endif
};
#endif				/* !NO_INDIRECTION */

int pthread_rwlock_init(pthread_rwlock_t * rwlock,
			const pthread_rwlockattr_t * attr)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_init\n");
	if (init_spinlock != 2) {
		REAL(interpose_init) ();
	}

#if !NO_INDIRECTION
	ht_rwlock_create((void *)rwlock, NULL);
	return 0;
#else
	return REAL(pthread_rwlock_init) (rwlock, attr);
#endif
}

int pthread_rwlock_destroy(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_destroy\n");
#if !NO_INDIRECTION
	lock_transparent_rwlock_t *impl = hash_get(rwlock);
	if (impl != NULL) {
		lock_rwlock_destroy(impl->lock_lock);
		free(impl);
	}
	return 0;
#else
	assert(0 && "rwlock not supported without indirection");
#endif
}

int pthread_rwlock_rdlock(pthread_rwlock_t * rwlock)
{
	int ret;
	DEBUG_PTHREAD("[p] pthread_rwlock_rdlock\n");
#if !NO_INDIRECTION
	lock_transparent_rwlock_t *impl = ht_rwlock_get((void *)rwlock);
	ret = lock_rwlock_rdlock(impl->lock_lock, get_rwlock_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
	return ret;
}

int pthread_rwlock_wrlock(pthread_rwlock_t * rwlock)
{
	int ret;
	DEBUG_PTHREAD("[p] pthread_rwlock_wrlock\n");
#if !NO_INDIRECTION
	lock_transparent_rwlock_t *impl = ht_rwlock_get((void *)rwlock);
	ret = lock_rwlock_wrlock(impl->lock_lock, get_rwlock_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
	return ret;
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t * lcok,
			       const struct timespec *abstime)
{
	assert(0 && "Timed locks not supported");
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t * lock,
			       const struct timespec *abstime)
{
	assert(0 && "Timed locks not supported");
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t * rwlock)
{
	int ret;
	DEBUG_PTHREAD("[p] pthread_rwlock_trylock\n");
#if !NO_INDIRECTION
	lock_transparent_rwlock_t *impl = ht_rwlock_get((void *)rwlock);
	ret = lock_rwlock_tryrdlock(impl->lock_lock, get_rwlock_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
	return ret;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t * rwlock)
{
	int ret;
	DEBUG_PTHREAD("[p] pthread_rwlock_trylock\n");
#if !NO_INDIRECTION
	lock_transparent_rwlock_t *impl = ht_rwlock_get((void *)rwlock);
	ret = lock_rwlock_trywrlock(impl->lock_lock, get_rwlock_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
	return ret;
}

int pthread_rwlock_unlock(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_unlock\n");
#if !NO_INDIRECTION
	lock_transparent_rwlock_t *impl = ht_rwlock_get((void *)rwlock);
	lock_rwlock_unlock(impl->lock_lock, get_rwlock_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
	return 0;
}

#else

// Rw locks
int pthread_rwlock_init(pthread_rwlock_t * rwlock,
			const pthread_rwlockattr_t * attr)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_init\n");
	if (init_spinlock != 2) {
		REAL(interpose_init) ();
	}
#if !NO_INDIRECTION
	ht_lock_create((void *)rwlock, NULL);
	return 0;
#else
	return REAL(pthread_rwlock_init) (rwlock, attr);
#endif
}

int pthread_rwlock_destroy(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_destroy\n");
#if !NO_INDIRECTION
	return 0;
#else
	assert(0 && "rwlock not supported without indirection");
#endif
}

int pthread_rwlock_rdlock(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_rdlock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get((void *)rwlock);
	return lock_mutex_lock(impl->lock_lock, get_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
}

int pthread_rwlock_wrlock(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_wrlock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get((void *)rwlock);
	return lock_mutex_lock(impl->lock_lock, get_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t * lcok,
			       const struct timespec *abstime)
{
	assert(0 && "Timed locks not supported");
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t * lock,
			       const struct timespec *abstime)
{
	assert(0 && "Timed locks not supported");
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_trylock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get((void *)rwlock);
	return lock_mutex_trylock(impl->lock_lock, get_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
}

int pthread_rwlock_trywrlock(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_trylock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get((void *)rwlock);
	return lock_mutex_trylock(impl->lock_lock, get_node(impl));
#else
	assert(0 && "rwlock not supported without indirection");
#endif
}

int pthread_rwlock_unlock(pthread_rwlock_t * rwlock)
{
	DEBUG_PTHREAD("[p] pthread_rwlock_unlock\n");
#if !NO_INDIRECTION
	lock_transparent_mutex_t *impl = ht_lock_get((void *)rwlock);
	lock_mutex_unlock(impl->lock_lock, get_node(impl));
	return 0;
#else
	assert(0 && "rwlock not supported without indirection");
#endif
}

#endif
