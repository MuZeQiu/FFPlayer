#ifndef TASK_POOL_SYNC_H
#define TASK_POOL_SYNC_H

#include <iostream>
#include <thread>
#include <functional>
#include <mutex>
#include <atomic>
#include "ff_queue_base.h"

namespace FFPlayer {
class task_pool_sync {
public:
    explicit task_pool_sync(unsigned int max_size):
        started_(false),
        queue_(max_size) {}

    inline void start() {
        std::unique_lock<std::mutex> lock(m_);
        if (!started_.load()) {
            started_.store(true);
            std::thread t([this](){
                std::function<void(void)> task;
                while(started_.load()) {
                    if (queue_.dequeue(task)) task();
                    std::this_thread::sleep_for(std::chrono::microseconds(5));
                }
                if (closing_cb_) closing_cb_(this);
            });
            t.detach();
        }
    }

    inline bool add_task(std::function<void(void)>&& task) {
        std::unique_lock<std::mutex> lock(m_);
        if (started_.load()) {
            return queue_.enqueue(task);
        }
        return false;
    }

    inline bool add_task(std::function<void(void)>& task) {
        std::unique_lock<std::mutex> lock(m_);
        if (started_.load()) {
            return queue_.enqueue(task);
        }
        return false;
    }

    inline void close(std::function<void(void*)>&& closing_cb) {
        std::unique_lock<std::mutex> lock(m_);
        closing_cb_ = closing_cb;
        started_.store(false);
    }

    inline void close(std::function<void(void*)>& closing_cb) {
        std::unique_lock<std::mutex> lock(m_);
        closing_cb_ = closing_cb;
        started_.store(false);
    }

    inline void clear() {
        std::unique_lock<std::mutex> lock(m_);
        queue_.clear();
    }

    inline void reset() {
        std::unique_lock<std::mutex> lock(m_);
        queue_.clear();
        started_.store(false);
    }

    inline bool idle() {
        std::unique_lock<std::mutex> lock(m_);
        return started_.load() && queue_.get_empty();
    }

    inline unsigned int size() {
        std::unique_lock<std::mutex> lock(m_);
        return queue_.get_size();
    }

    inline bool is_started() {
        std::unique_lock<std::mutex> lock(m_);
        return started_.load();
    }

    ~task_pool_sync() {}

private:
    task_pool_sync();
    task_pool_sync(const task_pool_sync&);
    task_pool_sync& operator =(const task_pool_sync&);
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool started_;
    ff_queue_base<std::function<void(void)>> queue_;
    std::function<void(void*)> closing_cb_;
};
}



#endif // TASK_POOL_SYNC_H
