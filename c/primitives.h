//
// Created by rvm2815xx on 5/20/2019.
//

#ifndef C_PRIMITIVES_H
#define C_PRIMITIVES_H

/**
 * An atomic fetch-and-add.
 */
#define FAA(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED)
/**
 * An atomic fetch-and-add that also ensures sequential consistency.
 */
#define FAAcs(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST)

#define FAAra(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_ACQ_REL)

#define FAA64(ptr, inc)                         \
    __sync_fetch_and_add((ptr), (inc))

#define CAS64(ptr, old, new)                    \
    __sync_bool_compare_and_swap((ptr), (old), (new))

#define CASPTR          CAS64
#define StorePrefetch(val)                      \
    do { } while (0)

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

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

#ifdef DEBUG
#define CAS2(ptr, o1, o2, n1, n2)                               \
({                                                              \
    int res;                                                    \
    res = __CAS2(ptr, o1, o2, n1, n2);                          \
    __executed_cas[__stats_thread_id].v++;                      \
    __failed_cas[__stats_thread_id].v += 1 - res;               \
    res;                                                        \
})
#else
#define CAS2(ptr, o1, o2, n1, n2)    __CAS2(ptr, o1, o2, n1, n2)
#endif


#define BIT_TEST_AND_SET(ptr, b)                                \
({                                                              \
    char __ret;                                                 \
    asm volatile("lock btsq $63, %0; setnc %1" : "+m"(*ptr), "=a"(__ret) : : "cc"); \
    __ret;                                                      \
})

/**
 * A load with a following acquire fence to ensure no following load and
 * stores can start before the current load completes.
 */
#define ACQUIRE(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)

#define ACQUIREcs(ptr) __atomic_load_n(ptr, __ATOMIC_SEQ_CST)

#endif //C_PRIMITIVES_H
