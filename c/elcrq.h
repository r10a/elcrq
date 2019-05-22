// Copyright (c) 2013, Adam Morrison and Yehuda Afek.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
//  * Neither the name of the Tel Aviv University nor the names of the
//    author of this software may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <sched.h>
#include <sys/time.h>
#include "primitives.h"
#include "malloc.h"

// Definition: RING_POW
// --------------------
// The LCRQ's ring size will be 2^{RING_POW}.
#ifndef RING_POW
#define RING_POW        (17)
#endif
#define RING_SIZE       (1ull << RING_POW)

#define Object          uint64_t
#define CACHE_ALIGN     __attribute__((aligned(64)))

// Definition: RING_STATS
// --------------------
// Define to collect statistics about CRQ closes and nodes
// marked unsafe.
//#define RING_STATS

// Definition: HAVE_HPTRS
// --------------------
// Define to enable hazard pointer setting for safe memory
// reclamation.  You'll need to integrate this with your
// hazard pointers implementation.
//#define HAVE_HPTRS

inline int is_empty(uint64_t v) __attribute__ ((pure));
inline uint64_t node_index(uint64_t i) __attribute__ ((pure));
inline uint64_t set_unsafe(uint64_t i) __attribute__ ((pure));
inline uint64_t node_unsafe(uint64_t i) __attribute__ ((pure));
inline uint64_t tail_index(uint64_t t) __attribute__ ((pure));
inline int crq_is_closed(uint64_t t) __attribute__ ((pure));

typedef struct RingNode {
    volatile uint64_t val;
    volatile uint64_t idx;
    uint64_t pad[14];
} RingNode __attribute__ ((aligned (128)));

typedef struct RingQueue {
    volatile int64_t head __attribute__ ((aligned (128)));
    volatile int64_t tail __attribute__ ((aligned (128)));
    struct RingQueue *next __attribute__ ((aligned (128)));
    RingNode array[RING_SIZE];
} RingQueue __attribute__ ((aligned (128)));

typedef struct ELCRQ {
    RingQueue *head;
    RingQueue *tail;
} ELCRQ;

//RingQueue *head;
//RingQueue *tail;

inline void init_ring(RingQueue *r) {
    int i;

    for (i = 0; i < RING_SIZE; i++) {
        r->array[i].val = -1;
        r->array[i].idx = i;
    }

    r->head = r->tail = 0;
    r->next = NULL;
}

int FULL;


inline int is_empty(uint64_t v)  {
    return (v == (uint64_t)-1);
}


inline uint64_t node_index(uint64_t i) {
    return (i & ~(1ull << 63));
}


inline uint64_t set_unsafe(uint64_t i) {
    return (i | (1ull << 63));
}


inline uint64_t node_unsafe(uint64_t i) {
    return (i & (1ull << 63));
}


inline uint64_t tail_index(uint64_t t) {
    return (t & ~(1ull << 63));
}


inline int crq_is_closed(uint64_t t) {
    return (t & (1ull << 63)) != 0;
}

inline void *getMemory(unsigned int size)
{
    return shm_malloc(size);
//    return malloc(size);
}

/*
static void SHARED_OBJECT_INIT() {
    int i;

    RingQueue *rq = getMemory(sizeof(RingQueue));
    init_ring(rq);
    head = tail = rq;

    if (FULL) {
        // fill ring 
        for (i = 0; i < RING_SIZE/2; i++) {
            rq->array[i].val = 0;
            rq->array[i].idx = i;
            rq->tail++;
        }
        FULL = 0;
    }
}
*/

inline void init_queue(ELCRQ* q) {
    RingQueue *rq = getMemory(sizeof(RingQueue));
    init_ring(rq);
    q->head = q->tail = rq;
}


inline void fixState(RingQueue *rq) {

    uint64_t t, h, n;

    while (1) {
        uint64_t t = FAA64(&rq->tail, 0);
        uint64_t h = FAA64(&rq->head, 0);

        if (unlikely(rq->tail != t))
            continue;

        if (h > t) {
            if (CAS64(&rq->tail, t, h)) break;
            continue;
        }
        break;
    }
}

__thread RingQueue *nrq;
__thread RingQueue *hazardptr;

#ifdef RING_STATS
__thread uint64_t mycloses;
__thread uint64_t myunsafes;

uint64_t closes;
uint64_t unsafes;

inline void count_closed_crq(void) {
    mycloses++;
}


inline void count_unsafe_node(void) {
    myunsafes++;
}
#else
inline void count_closed_crq(void) { }
inline void count_unsafe_node(void) { }
#endif


inline int close_crq(RingQueue *rq, const uint64_t t, const int tries) {
    if (tries < 10)
        return CAS64(&rq->tail, t + 1, (t + 1)|(1ull<<63));
    else
        return BIT_TEST_AND_SET(&rq->tail, 63);
}

inline void enqueue(Object arg, int pid, RingQueue* tail) {

    int try_close = 0;

    while (1) {
        RingQueue *rq = tail;

#ifdef HAVE_HPTRS
        SWAP(&hazardptr, rq);
        if (unlikely(tail != rq))
            continue;
#endif

        RingQueue *next = rq->next;

        if (unlikely(next != NULL)) {
            CASPTR(&tail, rq, next);
            continue;
        }

        uint64_t t = FAA64(&rq->tail, 1);

        if (crq_is_closed(t)) {
            alloc:
            if (nrq == NULL) {
                nrq = getMemory(sizeof(RingQueue));
                init_ring(nrq);
            }

            // Solo enqueue
            nrq->tail = 1, nrq->array[0].val = arg, nrq->array[0].idx = 0;

            if (CASPTR(&rq->next, NULL, nrq)) {
                CASPTR(&tail, rq, nrq);
                nrq = NULL;
                return;
            }
            continue;
        }

        RingNode* cell = &rq->array[t & (RING_SIZE-1)];
        StorePrefetch(cell);

        uint64_t idx = cell->idx;
        uint64_t val = cell->val;

        if (likely(is_empty(val))) {
            if (likely(node_index(idx) <= t)) {
                if ((likely(!node_unsafe(idx)) || rq->head < t) && CAS2((uint64_t*)cell, -1, idx, arg, t)) {
                    return;
                }
            }
        }

        uint64_t h = rq->head;

        if (unlikely(t - h >= RING_SIZE) && close_crq(rq, t, ++try_close)) {
            count_closed_crq();
            goto alloc;
        }
    }
}

inline Object dequeue(int pid, RingQueue* head) {

    while (1) {
        RingQueue *rq = head;
        RingQueue *next;

#ifdef HAVE_HPTRS
        SWAP(&hazardptr, rq);
        if (unlikely(head != rq))
            continue;
#endif

        uint64_t h = FAA64(&rq->head, 1);


        RingNode* cell = &rq->array[h & (RING_SIZE-1)];
        StorePrefetch(cell);

        uint64_t tt;
        int r = 0;

        while (1) {

            uint64_t cell_idx = cell->idx;
            uint64_t unsafe = node_unsafe(cell_idx);
            uint64_t idx = node_index(cell_idx);
            uint64_t val = cell->val;

            if (unlikely(idx > h)) break;

            if (likely(!is_empty(val))) {
                if (likely(idx == h)) {
                    if (CAS2((uint64_t*)cell, val, cell_idx, -1, unsafe | h + RING_SIZE))
                        return val;
                } else {
                    if (CAS2((uint64_t*)cell, val, cell_idx, val, set_unsafe(idx))) {
                        count_unsafe_node();
                        break;
                    }
                }
            } else {
                if ((r & ((1ull << 10) - 1)) == 0)
                    tt = rq->tail;

                // Optimization: try to bail quickly if queue is closed.
                int crq_closed = crq_is_closed(tt);
                uint64_t t = tail_index(tt);

                if (unlikely(unsafe)) { // Nothing to do, move along
                    if (CAS2((uint64_t*)cell, val, cell_idx, val, unsafe | h + RING_SIZE))
                        break;
                } else if (t - 1 <= h || r > 200000 || crq_closed) {
                    if (CAS2((uint64_t*)cell, val, idx, val, h + RING_SIZE))
                        break;
                } else {
                    ++r;
                }
            }
        }

        if (tail_index(rq->tail) - 1 <= h) {
            fixState(rq);
            // try to return empty
            next = rq->next;
            if (next == NULL)
                return NULL;  // EMPTY
            CASPTR(&head, rq, next);
        }
    }
}

//pthread_barrier_t barr;
/*

unsigned long long d1 CACHE_ALIGN, d2;
#define MAX_WORK 100
cpu_set_t cpuset;

void simSRandom(unsigned int seed)
{
    srand(seed);
}

int simRandomRange(int start, int end)
{
    return rand() % (end - start) + start;
}

void _thread_pin(int id)
{
    CPU_SET(id, &cpuset);

    int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (s != 0)
        perror("pthread_setaffinity_np");
}

static inline unsigned long long rdtsc_ll() {
    unsigned long long __h__, __l__;
    __asm__ __volatile__
    ("rdtsc" : "=d" (__h__), "=a" (__l__));
    return (__h__ << 32) | __l__;
}

int getTimeMillis(void)
{
    */
/* struct timeval tv; *//*

    */
/* gettimeofday(&tv, NULL); *//*

    */
/* return tv.tv_sec * 1000 + tv.tv_usec / 1000; *//*

    */
/* return tv.tv_usec; *//*


    return rdtsc_ll();
}

inline void Execute(void* Arg) {
    long i, rnum;
    volatile int j;
    long id = (long) Arg;

    _thread_pin(id);
    simSRandom(id + 1);
    nrq = NULL;

    if (id == N_THREADS - 1)
        d1 = getTimeMillis();
    // Synchronization point
//    int rc = pthread_barrier_wait(&barr);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        printf("Could not wait on barrier\n");
        exit(-1);
    }

    */
/* start_cpu_counters(id); *//*

    for (i = 0; i < RUNS; i++) {
        // perform an enqueue operation
        enqueue(id, id);
        rnum = simRandomRange(1, MAX_WORK);
        for (j = 0; j < rnum; j++)
            ;
        // perform a dequeue operation
        dequeue(id);
        rnum = simRandomRange(1, MAX_WORK);
        for (j = 0; j < rnum; j++)
            ;
    }
    */
/* stop_cpu_counters(id); *//*


#ifdef RING_STATS
    FAA64(&closes, mycloses);
    FAA64(&unsafes, myunsafes);
#endif
}

inline static void* EntryPoint(void* Arg) {
    Execute(Arg);
    return NULL;
}

inline pthread_t StartThread(int arg) {
    long id = (long) arg;
    void *Arg = (void*) id;
    pthread_t thread_p;
    int thread_id;

    pthread_attr_t my_attr;
    pthread_attr_init(&my_attr);
    thread_id = pthread_create(&thread_p, &my_attr, EntryPoint, Arg);

    return thread_p;
}

int maineeeeee(int argc, char **argv) {
    pthread_t threads[N_THREADS];
    int i;

    FULL = 10;

    // Barrier initialization
    */
/*if (pthread_barrier_init(&barr, NULL, N_THREADS)) {
        printf("Could not create the barrier\n");
        return -1;
    }*//*


    int full = FULL;

//    SHARED_OBJECT_INIT();
    CPU_ZERO(&cpuset);

    for (i = 0; i < N_THREADS; i++)
        threads[i] = StartThread(i);

    for (i = 0; i < N_THREADS; i++)
        pthread_join(threads[i], NULL);
    d2 = getTimeMillis();

    printf("time=%ld full=%d ", (unsigned long long) (d2 - d1), full);
#ifdef RING_STATS
    printf("closes=%ld unsafes=%ld ", closes, unsafes);
#endif
    */
/* printStats(); *//*


    if (pthread_barrier_destroy(&barr)) {
        printf("Could not destroy the barrier\n");
        return -1;
    }
    return 0;
}*/
