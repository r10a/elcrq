#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>
#include "LCRQueue.hpp"
#include "EventCount.hpp"

template<typename T>
class SCRQueue {
private:
    LCRQueue<T> *queue;
    fixed_managed_shared_memory *main_pool;
    fixed_managed_shared_memory *mem_pool;
    EventCount *ev;
    const int MAX_PATIENCE = 1000;
public:
    SCRQueue(fixed_managed_shared_memory *main_pool, fixed_managed_shared_memory *mem_pool, const int num_threads)
            : main_pool(main_pool), mem_pool(mem_pool) {
        queue = main_pool->construct<LCRQueue<T>>(anonymous_instance)(mem_pool, num_threads * 2);
        ev = main_pool->construct<EventCount>(anonymous_instance)();
    }

    ~SCRQueue() {
        main_pool->destroy_ptr(queue);
        main_pool->destroy_ptr(ev);
    }

    inline void enqueue(T *item, const int tid) {
        queue->enqueue(item, tid);
        ev->notify();
        usleep(10);
    }

    inline void spinEnqueue(T *item, const int tid) {
        queue->enqueue(item, tid);
    }

    inline T *dequeue(const int tid) {
        /**
         * Uncomment for spin-SCRQ
         */
//        T *temp = spinDequeue(tid);
        /**
         * Comment for spin-SCRQ
         */
        T *temp = queue->dequeue(tid);

        /*using Clock = std::chrono::system_clock;
        using Duration = std::chrono::microseconds;
        auto start = Clock::now();
        auto deadline = std::chrono::time_point_cast<Duration>(start + Duration(100));*/
        int patience = 0;
        if (temp == nullptr) {
            while (true) {
                auto key = ev->prepareWait();
                if (LIKELY((temp = queue->dequeue(tid)) == nullptr && patience++ < MAX_PATIENCE)) {
//                    ev->waitUntil(key, deadline);
//                    std::cout << "Waiting" << std::endl;
                    ev->wait(key);
                } else {
                    ev->cancelWait();
                    break;
                }
            }
        }
        return temp;
    }

    inline T *spinDequeue(const int tid) {
        int patience = 0;
        T *temp;
        while (LIKELY((temp = queue->dequeue(tid)) == nullptr && patience++ < MAX_PATIENCE));
        return temp;
    }
};