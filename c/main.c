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
#include "elcrq.h"
#include "malloc.h"

#ifndef NUM_ITERS
#define NUM_ITERS 1000
#endif

#ifndef NUM_RUNS
#define NUM_RUNS 3
#endif

#define NUM_THREAD 8

#define SHM_FILE "tshm_file"

#define size_lt unsigned long long

pthread_barrier_t *barrier_t;

typedef struct params {
    ELCRQ *q;
    int id;
} params;

static void *sender(void *par);

//
//static void *intermediate(void *params);
//
static void *receiver(void *par);

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

int main() {

    shm_init(SHM_FILE, NULL);

    barrier_t = shm_malloc(sizeof(pthread_barrier_t));

    pthread_barrierattr_t barattr;
    pthread_barrierattr_setpshared(&barattr, PTHREAD_PROCESS_SHARED);
    if (pthread_barrier_init(barrier_t, &barattr, NUM_THREAD * 2)) {
        printf("Could not create the barrier\n");
        return -1;
    }
    pthread_barrierattr_destroy(&barattr);

    ELCRQ *queue = shm_malloc(sizeof(ELCRQ));
    init_queue(queue);

    params p[NUM_THREAD];
    for (int i = 0; i < NUM_THREAD; i++) {
        p[i].q = queue;
        p[i].id = i;
    }

    if (fork() == 0) {
        shm_child();
        pthread_t sthreads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&sthreads[i], NULL, sender, &p[i]);
        }
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_join(sthreads[i], NULL);
        }
        printf("Done 1\n");
        exit(0);
    }

    if (fork() == 0) {
        shm_child();
        pthread_t rthreads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&rthreads[i], NULL, receiver, &p[i]);
        }

        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_join(rthreads[i], NULL);
        }
        printf("Done 2\n");
        exit(0);
    }

    int status;
    while (wait(&status) > 0);

    if ((status = pthread_barrier_destroy(barrier_t))) {
        printf("Could not destroy the barrier: %d\n", status);
        return -1;
    }
    shm_free(queue);
    shm_free(barrier_t);

    shm_fini();
    shm_destroy();
    return 0;
}

static void *sender(void *par) {
    params *p = (params *) par;
    ELCRQ *q = p->q;
    printf("Ready S: %d\n", p->id);
    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t); // barrier to wait for all threads to initialize
        for (int k = 0; k < NUM_ITERS; k++) {
            enqueue(k, p->id, q);
            usleep(10);
        }
    }
    return 0;
}

static void *receiver(void *par) {
    params *p = (params *) par;
    ELCRQ *q = p->q;
    printf("Ready R: %d\n", p->id);
    Object deq;
    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t); // barrier to wait for all threads to initialize
        for (int k = 0; k < NUM_ITERS - 1; k++) {
            Object element = dequeue(p->id, q);
            printf("%lu \n", element);
        }
    }
    return 0;
}