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
#include "ELCRQ.h"
#include "malloc.h"

#ifndef NUM_ITERS
#define NUM_ITERS 100
#endif

#ifndef NUM_RUNS
#define NUM_RUNS 1
#endif

#define NUM_THREAD 6

#define SHM_FILE "tshm_file"

#define size_lt unsigned long long

pthread_barrier_t *barrier_t;

typedef struct params {
    ELCRQ *q12;
    ELCRQ *q23;
    ELCRQ *q32;
    ELCRQ *q21;
    int id;
} params;

static void *sender(void *par);

static void *intermediate(void *params);

static void *receiver(void *par);

static inline size_lt elapsed_time_ns(size_lt ns) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000L + t.tv_nsec - ns;
}

static inline int thread_pin(int core_id) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

int main() {

    shm_init(SHM_FILE, NULL);

    barrier_t = shm_malloc(sizeof(pthread_barrier_t));

    pthread_barrierattr_t barattr;
    pthread_barrierattr_setpshared(&barattr, PTHREAD_PROCESS_SHARED);
    if (pthread_barrier_init(barrier_t, &barattr, NUM_THREAD * 3)) {
        printf("Could not create the barrier\n");
        return -1;
    }
    pthread_barrierattr_destroy(&barattr);

    ELCRQ *queue12 = shm_malloc(sizeof(ELCRQ));
    ELCRQ *queue23 = shm_malloc(sizeof(ELCRQ));
    ELCRQ *queue32 = shm_malloc(sizeof(ELCRQ));
    ELCRQ *queue21 = shm_malloc(sizeof(ELCRQ));
    init_queue(queue12);
    init_queue(queue23);
    init_queue(queue32);
    init_queue(queue21);

    params p[NUM_THREAD];
    for (int i = 0; i < NUM_THREAD; i++) {
        p[i].q12 = queue12;
        p[i].q23 = queue23;
        p[i].q32 = queue32;
        p[i].q21 = queue23;
        p[i].id = i;
    }

    if (fork() == 0) { // Process 1
        shm_child();
        pthread_t sthreads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&sthreads[i], NULL, sender, &p[i]);
        }
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_join(sthreads[i], NULL);
        }
        printf("Process 1 Done\n");
        exit(0);
    } // Process 1 ends

    if (fork() == 0) { // Process 2
        shm_child();
        pthread_t ithreads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&ithreads[i], NULL, intermediate, &p[i]);
        }
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_join(ithreads[i], NULL);
        }
        printf("Process 2 Done\n");
        exit(0);
    } // Process 2 ends

    if (fork() == 0) { // Process 3
        shm_child();
        pthread_t rthreads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&rthreads[i], NULL, receiver, &p[i]);
        }

        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_join(rthreads[i], NULL);
        }
        printf("Process 3 Done\n");
        exit(0);
    } // Process 3 ends

    int status;
    while (wait(&status) > 0); // wait for all processes to finish

    // cleanup
    if ((status = pthread_barrier_destroy(barrier_t))) {
        printf("Could not destroy the barrier: %d\n", status);
        return -1;
    }
//    shm_free(queue12);
//    shm_free(queue23);
//    shm_free(queue32);
//    shm_free(queue21);
//    shm_free(barrier_t);

    shm_fini();
    shm_destroy();
    return 0;
}

static void *sender(void *par) {
    params *p = (params *) par;
    int id = p->id;
    ELCRQ *q12 = p->q12;
    ELCRQ *q21 = p->q21;
    thread_pin(id);
    printf("Ready S: %d\n", id);
    Object deq;
    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t); // barrier to wait for all threads to initialize
        for (int k = 0; k < NUM_ITERS; k++) {
            enqueue(k, id, q12);
//            usleep(10);
            deq = spinDequeue(id, q21);
//            printf("Process 1 %d %lu \n", id,  deq);
        }
    }
    return 0;
}

static void *intermediate(void *par) {
    params *p = (params *) par;
    int id = p->id + NUM_THREAD;
    ELCRQ *q12 = p->q12;
    ELCRQ *q23 = p->q23;
    ELCRQ *q32 = p->q32;
    ELCRQ *q21 = p->q21;
    thread_pin(id);
    printf("Ready R: %d\n", id);
    Object deq;
    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t); // barrier to wait for all threads to initialize
        for (int k = 0; k < NUM_ITERS - 1; k++) {
            deq = dequeue(id, q12);
            enqueue(deq, id, q23);
            deq = spinDequeue(id, q32);
            spinEnqueue(deq, id, q21);
        }
    }
    return 0;
}

static void *receiver(void *par) {
    params *p = (params *) par;
    int id = p->id + NUM_THREAD*2;
    ELCRQ *q23 = p->q23;
    ELCRQ *q32 = p->q32;
    thread_pin(id);
    printf("Ready S: %d\n", id);
    Object deq;
    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t); // barrier to wait for all threads to initialize
        for (int k = 0; k < NUM_ITERS; k++) {
            deq = dequeue(id, q23);
            spinEnqueue(deq, id, q32);
        }
    }
    return 0;
}
