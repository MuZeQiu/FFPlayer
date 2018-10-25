#ifndef FF_QUEUE_BASE_H
#define FF_QUEUE_BASE_H

#include <assert.h>
#include <iostream>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <functional>

namespace FFPlayer {
template<typename T>
class ff_queue_base {
public:
    ff_queue_base(const ff_queue_base&) = delete;
    ff_queue_base& operator=(const ff_queue_base&) = delete;

    explicit ff_queue_base(unsigned int max_size):
        max_size_(max_size) {
        assert(max_size != 0);
    }

    ~ff_queue_base() {}

    inline virtual unsigned int get_max_size() {
        return max_size_;
    }

    inline virtual unsigned int get_size() {
        return queue_.size();
    }

    inline virtual bool get_empty() {
        return queue_.empty();
    }

    inline virtual bool enqueue(T&& t) {
        if(max_size_ <= queue_.size()) {
            return false;
        }
        queue_.push_back(t);
        return true;
    }

    inline virtual bool enqueue(T& t) {
        if(max_size_ <= queue_.size()) {
            return false;
        }
        queue_.push_back(t);
        return true;
    }

    inline virtual bool dequeue(T& t) {
        if (queue_.empty()) {
            return false;
        }
        t = queue_.front();
        queue_.pop_front();
        return true;
    }

    inline virtual bool popqueue(T& t) {
        if (queue_.empty()) {
            return false;
        }
        t = queue_.back();
        queue_.pop_back();
        return true;
    }

    inline virtual bool enpacket(std::deque<T>& que) {
        if (que.size()+queue_.size() >= max_size_) {
            return false;
        }
        for (int i = 0; i < que.size(); i++) {
            T t = que.at(i);
            queue_.push_back(t);
        }
        return true;
    }

    inline virtual void clear() {
        queue_.clear();
    }

    inline virtual void reset(unsigned int max_size) {
        queue_.clear();
        max_size_ = max_size;
    }

    inline virtual void sort(std::function<bool(const T&, const T&)> compare) {
        std::sort(queue_.begin(), queue_.end(), compare);
    }

    virtual bool enpacket_with_sort(std::deque<T>& que,
                            std::function<bool(const T&, const T&)> compare) {
        if (que.size()+queue_.size() >= max_size_) {
            return false;
        }
        for (int i = 0; i < que.size(); i++) {
            T t = que.at(i);
            queue_.push_back(t);
        }
        std::sort(queue_.begin(), queue_.end(), compare);
        return true;
    }

private:
    ff_queue_base();
    unsigned int max_size_;
    std::deque<T> queue_;
};

template<typename T>
class ff_safe_queue: public ff_queue_base<T> {
public:
    explicit ff_safe_queue(unsigned int max_size):
        ff_queue_base<T>(max_size),
        canceled_(false) {}

    ~ff_safe_queue() {}

    virtual unsigned int get_max_size() {
        std::unique_lock<std::mutex> lock(m_);
        return ff_queue_base<T>::get_max_size();
    }

    virtual unsigned int get_size() override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_queue_base<T>::get_size();
    }

    virtual bool get_empty() override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_queue_base<T>::get_empty();
    }

    virtual bool enqueue(T&& t) override {
        std::unique_lock<std::mutex> lock(m_);
        while(!canceled_.load() && !ff_queue_base<T>::enqueue(t)) {
            //std::cout << "safe queue enqueue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    virtual bool enqueue(T& t) override {
        std::unique_lock<std::mutex> lock(m_);
        while(!canceled_.load() && !ff_queue_base<T>::enqueue(t)) {
            //std::cout << "safe queue enqueue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    virtual bool dequeue(T& t) override {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::dequeue(t)) {
            //std::cout << "safe queue dequeue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    inline virtual bool popqueue(T& t) {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::popqueue(t)) {
            //std::cout << "safe queue popqueue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    virtual bool enpacket(std::deque<T>& que) override {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::enpacket(que)) {
            //std::cout << "safe queue enpacket in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    inline bool is_canceled() {
        std::unique_lock<std::mutex> lock(m_);
        return canceled_.load();
    }

    void cancel() {
        std::unique_lock<std::mutex> lock(m_);
        canceled_.store(true);
        cv_.notify_all();
    }

    virtual void clear() override {
        std::unique_lock<std::mutex> lock(m_);
        ff_queue_base<T>::clear();
        cv_.notify_all();
    }

    virtual void reset(unsigned int max_size) override {
        std::unique_lock<std::mutex> lock(m_);
        ff_queue_base<T>::reset(max_size);
        canceled_.store(false);
        cv_.notify_all();
    }

    virtual void sort(std::function<bool(const T&, const T&)> compare) override {
        std::unique_lock<std::mutex> lock(m_);
        ff_queue_base<T>::sort(compare);
    }

    virtual bool enpacket_with_sort(std::deque<T>& que,
                                    std::function<bool(const T&, const T&)> compare) override {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::enpacket(que)) {
            //std::cout << "safe queue enpacket_with_sort in blocking: " <<\
                      ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        ff_queue_base<T>::sort(compare);
        cv_.notify_all();
        return !canceled_.load();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool canceled_;
};
}



#endif // FF_QUEUE_BASE_H
