#define _GNU_SOURCE

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <papi.h>

/* Define Platform Here */
#define r74x

#ifdef r74x
#define PLAT_CPU_NUM 40
#elif defined(apple)
#define PLAT_CPU_NUM 8
#elif defined(amd)
#define PLAT_CPU_NUM 64
#else
#define PLAT_CPU_NUM 16
#endif

#define TST_NUM 100000
#define THD_NUM 80
#define RECORD_FREQ 1

#define NOP0 __asm__ __volatile__("\nnop\n");
#define NOP1 NOP0 NOP0
#define NOP2 NOP1 NOP1
#define NOP3 NOP2 NOP2
#define NOP4 NOP3 NOP3
#define NOP5 NOP4 NOP4
#define NOP6 NOP5 NOP5
#define NOP7 NOP6 NOP6
#define NOP8 NOP7 NOP7
#define NOP9 NOP8 NOP8
#define NOP10 NOP9 NOP9
#define NOP11 NOP10 NOP10
#define NOP12 NOP11 NOP11

volatile int g_max_shared_variables;
volatile int long_number_of_shared_variables;
volatile uint64_t *long_shared_variables_memory_area;
volatile int short_number_of_shared_variables;
volatile uint64_t *short_shared_variables_memory_area;
__thread long long tt_startp, tt_endp;
__thread long long tt_startp1, tt_endp1;
__thread int long_cri = 1;
int possibility = 4;
int ratio = 0;
int ux_num = 100;
void access_variables(volatile uint64_t * memory_area, int number_of_variables)
{
	int i = 0;

	for (i = 0; i < number_of_variables; i++) {
		*(memory_area + 8 * i + 2) = *(memory_area + 8 * i + 7) + 1;
		*(memory_area + 8 * i + 7) = *(memory_area + 8 * i + 2) + 1;
	}
}

pthread_mutex_t global_lock;
unsigned long global_cnt[THD_NUM] = { 0 };

#ifdef	MULTIPLE_LOCK
pthread_mutex_t global_secondary_lock;
#endif

pthread_barrier_t sig_start;
volatile int global_stop = 0;
int delay, mode;
uint64_t target_latency;

#ifdef TIME
#else
#define LATENCY_RECORD 5000000
uint64_t *record_all[THD_NUM];
__thread uint64_t record_latency[LATENCY_RECORD] = { 0 };
#endif

#ifdef r74x
#define AVALIABLE_CORE_NUM	10
int avaliable_core[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18 };
#elif defined(apple)
#define AVALIABLE_CORE_NUM	8
int avaliable_core[] = { 4, 5, 6, 7, 0, 1, 2, 3 };
#elif defined(arm)
#define AVALIABLE_CORE_NUM	64
int avaliable_core[] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23,
	48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63
};
#else
#define AVALIABLE_CORE_NUM	16
int avaliable_core[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
#endif

#ifdef	LIBHTLL_INTERFACE
#include "libhtll.h"
#endif

void delay_nops(int time)
{
	for (int i = 0; i < time; i++) {
		NOP7;
	}
}

int short_thread_number = THD_NUM;

void request_normal(int tid)
{

#ifdef	LIBHTLL_INTERFACE
	segment_start(0);
#endif
	tt_startp = PAPI_get_real_cyc();
	pthread_mutex_lock(&global_lock);
	tt_endp = PAPI_get_real_cyc();
	
	// delay_nops(delay);
	global_cnt[tid]++;
	if (tid < short_thread_number)
		access_variables(short_shared_variables_memory_area,
				 short_number_of_shared_variables);
	else
		access_variables(long_shared_variables_memory_area,
				 long_number_of_shared_variables);
	
	pthread_mutex_unlock(&global_lock);

#ifdef	LIBHTLL_INTERFACE
	segment_end(0, target_latency);
#endif
}

void *thread_routine_transparent(void *arg)
{

	uint64_t duration;
	double wait;
	int64_t tid = (int64_t) arg;
	int record_cnt = 0;
	int i = 0, core_id;
#if 0
	/* Bind core */
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id % PLAT_CPU_NUM, &mask);
	sched_setaffinity(0, sizeof(mask), &mask);
	sched_yield();
#endif
	record_all[tid] = record_latency;
	/* Start Barrier */
	pthread_barrier_wait(&sig_start);

	while (!global_stop) {
		i++;
		if (1) {
			/* Record Frequency */
			request_normal(tid);

		} else {
			request_normal(tid);
		}

		delay_nops(delay);

		duration = tt_endp - tt_startp;
		record_latency[record_cnt] = duration;
		record_cnt++;
	}
	return NULL;
}

void print_help(void)
{
	printf("UTA micro-benchmark\n");
	printf("Usage:\n");
	printf("    -h print this message\n");
	printf("    -t [thread num]\n");
	printf("    -g [number of visited shared cache lines in CS]\n");
	printf("    -d [delay between 2 acquistions]\n");
	printf("    -p [every p thread have one ux thread]n");
	printf("    -T [measure time (seconds)]\n");
}

int main(int argc, char *argv[])
{
	pthread_t tid[THD_NUM];
	int64_t i = 0, j = 0;
	unsigned long total_cnt = 0;
	int command;
	int sleep_time = 3;
	int nb_thread = 20;
	void *(*thread_entry)(void *);
	srand(10);
	mode = 0;
	delay = 100;
	target_latency = 100000;
	long_number_of_shared_variables = 5120;
	short_number_of_shared_variables = 32;
	while ((command = getopt(argc, argv, "m:g:u:s:p:d:t:hT:r:l:S:")) != -1) {
		switch (command) {
		case 'h':
			print_help();
			exit(0);
		case 't':
			nb_thread = atoi(optarg);
			break;
		case 'g':
			long_number_of_shared_variables = atoi(optarg);
			break;
		case 's':
			short_number_of_shared_variables = atoi(optarg);
			break;
		case 'S':
			short_thread_number = atoi(optarg);
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'T':
			sleep_time = atoi(optarg);
			break;
		case 'u':
			ux_num = atoi(optarg);
			break;
		case 'p':
			possibility = atoi(optarg);
			break;
		case 'l':
			target_latency = atoi(optarg);
			break;
		default:
		case '?':
			printf("unknown option:%s\n", optarg);
			break;
		}
	}
	g_max_shared_variables = long_number_of_shared_variables;
	pthread_mutex_init(&global_lock, 0);
	switch (mode) {
	case 0:
		thread_entry = thread_routine_transparent;
		break;
	default:
		printf("Unknown mode!\n");
		exit(-1);
		break;
	}
	//long critical to uon-ux thread
	long_shared_variables_memory_area =
	    (uint64_t *) malloc(long_number_of_shared_variables * 64);
	//short critical to ux thread
	short_shared_variables_memory_area =
	    (uint64_t *) malloc(short_number_of_shared_variables * 64);
	pthread_barrier_init(&sig_start, 0, nb_thread + 1);

	for (i = 0; i < nb_thread; i++)
		pthread_create(&tid[i], NULL, thread_entry, (void *)i);
	sched_yield();
	pthread_barrier_wait(&sig_start);
	/* Start Barrier */
	sleep(sleep_time);
	global_stop = 1;
	int cur;
	/* Stop Signal */
	uint64_t res = 0;
	
	pthread_mutex_destroy(&global_lock);
	for (i = 0; i < nb_thread; i++)
		total_cnt += global_cnt[i];
	// printf("pthread\n");

	printf("%lf\n", (double)(total_cnt) / sleep_time);
	for (i = 0; i < nb_thread; i++) {
		cur = global_cnt[i];
		printf("core %ld cnt %ld\n", i, global_cnt[i]);
		for (j = 0; j < cur; j++) {
			printf("%lu\n", record_all[i][j]);
		}
	}
	return 0;
}
