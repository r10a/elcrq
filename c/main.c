#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <math.h>
#include <sys/wait.h>
#include <asm/errno.h>

#ifndef NUM_ITERS
#define NUM_ITERS 1000000
#endif

#ifndef NUM_RUNS
#define NUM_RUNS 3
#endif

#define NUM_THREAD 8

#define size_lt unsigned long long
#define SHM_G "GLOBAL"

pthread_barrier_t *barrier_t;
size_lt *reqs;

//static void *sender(void *params);
//
//static void *intermediate(void *params);
//
//static void *receiver(void *params);

static inline size_lt elapsed_time_ns(size_lt ns) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000L + t.tv_nsec - ns;
}

int assign_thread_to_core(int core_id, pthread_t pthread) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    return pthread_setaffinity_np(pthread, sizeof(cpu_set_t), &cpuset);
}