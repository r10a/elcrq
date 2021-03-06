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

typedef struct EventCount {
    uint64_t val_;
} EventCount;

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

const uint64_t kAddWaiter = (uint64_t) (1);
const uint64_t kSubWaiter = (uint64_t) (-1);
const size_t kEpochShift = 32;
#define kAddEpoch (uint64_t)(1) << kEpochShift
#define kWaiterMask (kAddEpoch) - 1
#define kIsLittleEndian 1
const size_t kEpochOffset = kIsLittleEndian ? 1 : 0;

inline void initEventCount(EventCount* ec) {
    ec->val_ = 0;
}

inline void doNotify(EventCount* ec, int n) {
    uint64_t prev = FAAra(&ec->val_, kAddEpoch);
    if (unlikely(prev & kWaiterMask)) {
        nativeFutexWake((uint32_t*)&ec->val_ + kEpochOffset, n, -1);
    }
}

inline void notify(EventCount* ec) {
    doNotify(ec, 1);
}

inline void notifyAll(EventCount* ec) {
    doNotify(ec, INT_MAX);
}

inline Key prepareWait(EventCount* ec) {
    uint64_t prev = FAAra(&ec->val_, kAddWaiter);
    Key key;
    key.epoch_= prev >> kEpochShift;
    return key;
}

inline void cancelWait(EventCount* ec) {
    FAAcs(&ec->val_, kSubWaiter);
}

inline void await(EventCount* ec, Key key) {
    while ((ACQUIRE(&ec->val_) >> kEpochShift) == key.epoch_) {
        nativeFutexWaitImpl((uint32_t*)&ec->val_ + kEpochOffset, key.epoch_, -1);
    }
    FAAcs(&ec->val_, kSubWaiter);
}

#endif //C_EVENTCOUNT_H
