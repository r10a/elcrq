//
// Created by rohit on 5/20/2019.
//

#ifndef C_EVENTCOUNT_H
#define C_EVENTCOUNT_H

#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <limits.h>
#include "primitives.h"

typedef struct Key {
    uint32_t epoch_;
} Key;

int nativeFutexWake(const void *addr, int count, uint32_t wakeMask) {
#ifndef PSHARED
    int rv = syscall(
            __NR_futex,
            addr, /* addr1 */
            FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG, /* op */
            count, /* val */
            NULL, /* timeout */
            NULL, /* addr2 */
            wakeMask); /* val3 */
#else
    int rv = syscall(
            __NR_futex,
            addr, /* addr1 */
            FUTEX_WAKE_BITSET, /* op */
            count, /* val */
            NULL, /* timeout */
            NULL, /* addr2 */
            wakeMask); /* val3 */
#endif

    /* NOTE: we ignore errors on wake for the case of a futex
       guarding its own destruction, similar to this
       glibc bug with sem_post/sem_wait:
       https://sourceware.org/bugzilla/show_bug.cgi?id=12674 */
    if (rv < 0) {
        return 0;
    }
    return rv;
}

int nativeFutexWaitImpl(const void *addr, uint32_t expected, uint32_t waitMask) {

#ifndef PSHARED
    int op = FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG;
#else
    int op = FUTEX_WAIT_BITSET;
#endif

    // Unlike FUTEX_WAIT, FUTEX_WAIT_BITSET requires an absolute timeout
    // value - http://locklessinc.com/articles/futex_cheat_sheet/
    int rv = syscall(
            __NR_futex,
            addr, /* addr1 */
            op, /* op */
            expected, /* val */
            NULL, /* timeout */
            NULL, /* addr2 */
            waitMask); /* val3 */
    return rv;
}

uint64_t* val_;
const uint64_t kAddWaiter = (uint64_t) (1);
const uint64_t kSubWaiter = (uint64_t) (-1);
const size_t kEpochShift = 32;
#define kAddEpoch (uint64_t)(1) << kEpochShift
#define kWaiterMask (kAddEpoch) - 1
#define kIsLittleEndian 1
const size_t kEpochOffset = kIsLittleEndian ? 1 : 0;

inline void initEventCount() {
    val_ = shm_malloc(sizeof(uint64_t));
    *val_ = 0;
}

inline void doNotify(int n) {
//    printf("notify val: %lu\n", ACQUIREcs(&val_));
    uint64_t prev = FAAra(val_, kAddEpoch);
//    printf("notify val: %lu %lu\n", prev, ACQUIREcs(val_));
//    printf("%lu %lu %lu %lu %lu %lu %lu\n", prev, kAddWaiter, kSubWaiter, kEpochShift, kAddEpoch, kWaiterMask, kEpochOffset);
    if (unlikely(prev & kWaiterMask)) {
//        printf("waking2\n");
        nativeFutexWake((uint32_t*)val_ + kEpochOffset, n, -1);
    }
}

inline void notify() {
    doNotify(1);
}

inline void notifyAll() {
    doNotify(INT_MAX);
}


inline Key prepareWait() {
    uint64_t prev = FAAra(val_, kAddWaiter);
//    printf("notify val: %lu %lu\n", prev, ACQUIREcs(val_));
    Key key;
    key.epoch_= prev >> kEpochShift;
//    printf("key: %lu %lu %d\n", ACQUIREcs(&val_), prev, key.epoch_);
    return key;
}

inline void cancelWait() {
    FAAcs(val_, kSubWaiter);
}

inline void waitIndef(Key key) {
    while ((ACQUIRE(val_) >> kEpochShift) == key.epoch_) {
        nativeFutexWaitImpl((uint32_t*)val_ + kEpochOffset, key.epoch_, -1);
    }
    FAAcs(val_, kSubWaiter);
}

#endif //C_EVENTCOUNT_H
