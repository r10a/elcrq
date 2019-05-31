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
#include "EventCount.h"

// Definition: RING_POW
// --------------------
// The LCRQ's ring size will be 2^{RING_POW}.
#ifndef RING_POW
#define RING_POW        (17)
#endif
#define RING_SIZE       (1ull << RING_POW)

#define Object          uint64_t
#define CACHE_ALIGN     __attribute__((aligned(64)))
#define EMPTY (uint64_t)-1
#define MAX_PATIENCE 1000

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

static inline int is_empty(uint64_t v) __attribute__ ((pure));
static inline uint64_t node_index(uint64_t i) __attribute__ ((pure));
static inline uint64_t set_unsafe(uint64_t i) __attribute__ ((pure));
static inline uint64_t node_unsafe(uint64_t i) __attribute__ ((pure));
static inline uint64_t tail_index(uint64_t t) __attribute__ ((pure));
static inline int crq_is_closed(uint64_t t) __attribute__ ((pure));

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
    EventCount ec;
} ELCRQ;

static inline void enq(Object arg, int pid, RingQueue* tail);
static inline Object deq(int pid, RingQueue* head);
inline void enqueue(Object arg, int pid, ELCRQ* q);
inline Object dequeue(int pid, ELCRQ* q);
inline void spinEnqueue(Object arg, int pid, ELCRQ *q);
inline Object spinDequeue(int pid, ELCRQ* q);

//RingQueue *head;
//RingQueue *tail;

static inline void init_ring(RingQueue *r) {
    int i;

    for (i = 0; i < RING_SIZE; i++) {
        r->array[i].val = -1;
        r->array[i].idx = i;
    }

    r->head = r->tail = 0;
    r->next = NULL;
}

static inline int is_empty(uint64_t v)  {
    return (v == (uint64_t)-1);
}


static inline uint64_t node_index(uint64_t i) {
    return (i & ~(1ull << 63));
}


static inline uint64_t set_unsafe(uint64_t i) {
    return (i | (1ull << 63));
}


static inline uint64_t node_unsafe(uint64_t i) {
    return (i & (1ull << 63));
}


static inline uint64_t tail_index(uint64_t t) {
    return (t & ~(1ull << 63));
}


static inline int crq_is_closed(uint64_t t) {
    return (t & (1ull << 63)) != 0;
}

static inline void *getMemory(unsigned int size) {
    return shm_malloc(size);
}

inline void init_queue(ELCRQ* q) {
    RingQueue *rq = getMemory(sizeof(RingQueue));
    init_ring(rq);
    q->head = q->tail = rq;
    initEventCount(&q->ec);
}


static inline void fixState(RingQueue *rq) {

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

inline int close_crq(RingQueue *rq, const uint64_t t, const int tries) {
    if (tries < 10)
        return CAS64(&rq->tail, t + 1, (t + 1)|(1ull<<63));
    else
        return BIT_TEST_AND_SET(&rq->tail, 63);
}

static inline void enq(Object arg, int pid, RingQueue* tail) {

    int try_close = 0;

    while (1) {
        RingQueue *rq = tail;
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
//            count_closed_crq();
            goto alloc;
        }
    }
}

static inline Object deq(int pid, RingQueue* head) {

    while (1) {
        RingQueue *rq = head;
        RingQueue *next;

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
//                        count_unsafe_node();
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
                return EMPTY;  // EMPTY
            CASPTR(&head, rq, next);
        }
    }
}

inline void enqueue(Object arg, int pid, ELCRQ* q) {
    enq(arg, pid, q->tail);
    notify(&q->ec);
}

inline Object dequeue(int pid, ELCRQ* q) {
    Object element = deq(pid, q->head);
    if(element == EMPTY) {
        while(1) {
            Key key = prepareWait(&q->ec);
            if (likely((element = deq(pid, q->head)) == EMPTY)) {
                await(&q->ec, key);
            } else {
                cancelWait(&q->ec);
                break;
            }
        }
    }
    return element;
}

inline void spinEnqueue(Object arg, int pid, ELCRQ *q) {
    enq(arg, pid, q->tail);
}

inline Object spinDequeue(int pid, ELCRQ* q) {
    Object element;
    int patience = 0;
    while (likely((element = deq(pid, q->head)) == EMPTY && patience++ < MAX_PATIENCE)) {
//        printf("spinning %lu\n", element);
    }
    return element;
}