#pragma once

#include <atomic>
#include "HazardPointers.hpp"

// CAS2 macro

#define __CAS2(ptr, o1, o2, n1, n2)                             \
({                                                              \
    char __ret;                                                 \
    __typeof__(o2) __junk;                                      \
    __typeof__(*(ptr)) __old1 = (o1);                           \
    __typeof__(o2) __old2 = (o2);                               \
    __typeof__(*(ptr)) __new1 = (n1);                           \
    __typeof__(o2) __new2 = (n2);                               \
    asm volatile("lock cmpxchg16b %2;setz %1"                   \
                   : "=d"(__junk), "=a"(__ret), "+m" (*ptr)     \
                   : "b"(__new1), "c"(__new2),                  \
                     "a"(__old1), "d"(__old2));                 \
    __ret; })

#define CAS2(ptr, o1, o2, n1, n2)    __CAS2(ptr, o1, o2, n1, n2)


#define BIT_TEST_AND_SET(ptr, b)                                \
({                                                              \
    char __ret;                                                 \
    asm volatile("lock btsq $63, %0; setnc %1" : "+m"(*ptr), "=a"(__ret) : : "cc"); \
    __ret;                                                      \
})




/**
 * <h1> LCRQ Queue </h1>
 *
 * This is LCRQ by Adam Morrison and Yehuda Afek
 * http://www.cs.tau.ac.il/~mad/publications/ppopp2013-x86queues.pdf
 *
 * This implementation does NOT obey the C++ memory model rules AND it is x86 specific.
 *
 * <p>
 * enqueue algorithm: MS enqueue + LCRQ with re-usage
 * dequeue algorithm: MS dequeue + LCRQ with re-usage
 * Consistency: Linearizable
 * enqueue() progress: lock-free
 * dequeue() progress: lock-free
 * Memory Reclamation: Hazard Pointers (lock-free)
 *
 * <p>
 * The paper on Hazard Pointers is named "Hazard Pointers: Safe Memory
 * Reclamation for Lock-Free objects" and it is available here:
 * http://web.cecs.pdx.edu/~walpole/class/cs510/papers/11.pdf
 *
 * @author Pedro Ramalhete
 * @author Andreia Correia
 */
template<typename T>
class LCRQueue {

private:
    static const int RING_POW = 10;
    static const uint64_t RING_SIZE = 1ull << RING_POW;

    struct Cell {
        std::atomic<T*>       val;
        std::atomic<uint64_t> idx;
        uint64_t pad[14];
    } __attribute__ ((aligned (128)));

    struct Node {
        std::atomic<int64_t> head  __attribute__ ((aligned (128)));
        std::atomic<int64_t> tail  __attribute__ ((aligned (128)));
        std::atomic<Node*> next    __attribute__ ((aligned (128)));
        Cell array[RING_SIZE];

        Node() {
            for (unsigned i = 0; i < RING_SIZE; i++) {
                array[i].val.store(nullptr, std::memory_order_relaxed);
                array[i].idx.store(i, std::memory_order_relaxed);
            }
            head.store(0, std::memory_order_relaxed);
            tail.store(0, std::memory_order_relaxed);
            next.store(nullptr, std::memory_order_relaxed);
        }
    };

    fixed_managed_shared_memory *shm;
    alignas(128) std::atomic<Node*> head;
    alignas(128) std::atomic<Node*> tail;

    static const int MAX_THREADS = 128;
    const int maxThreads;

    HazardPointers<Node> *hp;
    const int kHpTail = 0;
    const int kHpHead = 0;


    /*
     * Private methods
     */
    int is_empty(T* v)  {
        return (v == nullptr);
    }

    uint64_t node_index(uint64_t i) {
        return (i & ~(1ull << 63));
    }

    uint64_t set_unsafe(uint64_t i) {
        return (i | (1ull << 63));
    }

    uint64_t node_unsafe(uint64_t i) {
        return (i & (1ull << 63));
    }

    inline uint64_t tail_index(uint64_t t) {
        return (t & ~(1ull << 63));
    }

    int crq_is_closed(uint64_t t) {
        return (t & (1ull << 63)) != 0;
    }

    void fixState(Node *lhead) {
        while (1) {
            uint64_t t = lhead->tail.fetch_add(0);
            uint64_t h = lhead->head.fetch_add(0);
            // TODO: is it ok or not to cast "t" to int64_t ?
            if (lhead->tail.load() != (int64_t)t) continue;
            if (h > t) {
                int64_t tmp = t;
                if (lhead->tail.compare_exchange_strong(tmp, h)) break;
                continue;
            }
            break;
        }
    }

    int close_crq(Node *rq, const uint64_t tailticket, const int tries) {
        if (tries < 10) {
            int64_t tmp = tailticket + 1;
            return rq->tail.compare_exchange_strong(tmp, (tailticket + 1)|(1ull<<63));
        }
        else {
            return BIT_TEST_AND_SET(&rq->tail, 63);
        }
    }


public:
    LCRQueue(fixed_managed_shared_memory *shm, int maxThreads=MAX_THREADS) : shm(shm), maxThreads{maxThreads} {
        // Shared object init
        Node *sentinel = shm->construct<Node>(anonymous_instance)();
        hp = shm->construct<HazardPointers<Node>>(anonymous_instance)(shm, 1, maxThreads);
        head.store(sentinel, std::memory_order_relaxed);
        tail.store(sentinel, std::memory_order_relaxed);
    }


    ~LCRQueue() {
        while (dequeue(0) != nullptr); // Drain the queue
        shm->destroy_ptr(head.load());
    }

    std::string className() { return "LCRQueue"; }


    void enqueue(T* item, const int tid) {
        int try_close = 0;
        while (true) {
            Node* ltail = hp->protectPtr(kHpTail, tail.load(), tid);
            if (ltail != tail.load()) continue;
            Node *lnext = ltail->next.load();
            if (lnext != nullptr) {  // Help advance the tail
                tail.compare_exchange_strong(ltail, lnext);
                continue;
            }

            uint64_t tailticket = ltail->tail.fetch_add(1);
            if (crq_is_closed(tailticket)) {
                Node* newNode = shm->construct<Node>(anonymous_instance)();
                // Solo enqueue (superfluous?)
                newNode->tail.store(1, std::memory_order_relaxed);
                newNode->array[0].val.store(item, std::memory_order_relaxed);
                newNode->array[0].idx.store(0, std::memory_order_relaxed);
                Node* nullnode = nullptr;
                if (ltail->next.compare_exchange_strong(nullnode, newNode)) {// Insert new ring
                    tail.compare_exchange_strong(ltail, newNode); // Advance the tail
                    hp->clear(tid);
                    return;
                }
                shm->destroy_ptr(newNode);
                continue;
            }
            Cell* cell = &ltail->array[tailticket & (RING_SIZE-1)];
            uint64_t idx = cell->idx.load();
            if (cell->val.load() == nullptr) {
                if (node_index(idx) <= tailticket) {
                    if ((!node_unsafe(idx) || ltail->head.load() < (int64_t)tailticket)) {
                        if (CAS2((void**)cell, nullptr, idx, item, tailticket)) {
                            hp->clear(tid);
                            return;
                        }
                    }
                }
            }
            if (((int64_t)(tailticket - ltail->head.load()) >= (int64_t)RING_SIZE) && close_crq(ltail, tailticket, ++try_close)) continue;
        }
    }


    T* dequeue(const int tid) {
        while (true) {
            Node* lhead = hp->protectPtr(kHpHead, head.load(), tid);
            if (lhead != head.load()) continue;
            uint64_t headticket = lhead->head.fetch_add(1);
            Cell* cell = &lhead->array[headticket & (RING_SIZE-1)];

            int r = 0;
            uint64_t tt = 0;

            while (true) {
                uint64_t cell_idx = cell->idx.load();
                uint64_t unsafe = node_unsafe(cell_idx);
                uint64_t idx = node_index(cell_idx);
                T* val = cell->val.load();

                if (idx > headticket) break;

                if (val != nullptr) {
                    if (idx == headticket) {
                        if (CAS2((void**)cell, val, cell_idx, nullptr, unsafe | (headticket + RING_SIZE))) {
                            hp->clear(tid);
                            return val;
                        }
                    } else {
                        if (CAS2((void**)cell, val, cell_idx, val, set_unsafe(idx))) break;
                    }
                } else {
                    if ((r & ((1ull << 10) - 1)) == 0) tt = lhead->tail.load();
                    // Optimization: try to bail quickly if queue is closed.
                    int crq_closed = crq_is_closed(tt);
                    uint64_t t = tail_index(tt);
                    if (unsafe) { // Nothing to do, move along
                        if (CAS2((void**)cell, val, cell_idx, val, unsafe | (headticket + RING_SIZE)))
                            break;
                    } else if (t < headticket + 1 || r > 200000 || crq_closed) {
                        if (CAS2((void**)cell, val, idx, val, headticket + RING_SIZE)) {
                            if (r > 200000 && tt > RING_SIZE) BIT_TEST_AND_SET(&lhead->tail, 63);
                            break;
                        }
                    } else {
                        ++r;
                    }
                }
            }

            if (tail_index(lhead->tail.load()) <= headticket + 1) {
                fixState(lhead);
                // try to return empty
                Node* lnext = lhead->next.load();
                if (lnext == nullptr) {
                    hp->clear(tid);
                    return nullptr;  // Queue is empty
                }
                if (tail_index(lhead->tail) <= headticket + 1) {
                    if (head.compare_exchange_strong(lhead, lnext)) hp->retire(lhead, tid);
                }
            }
        }
    }
};