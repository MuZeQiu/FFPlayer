#ifndef FF_STREAM_BASE_H
#define FF_STREAM_BASE_H

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <assert.h>
#include <atomic>

namespace FFPlayer {
template<typename T>
class ff_stream_base {
public:
    ff_stream_base(unsigned int capacity, unsigned int diff):
        capacity_(capacity),
        stream_((T*)malloc(capacity_)),
        diff_(diff),
        type_size_(sizeof(T)) {
        assert(stream_);
    }
    ~ff_stream_base() {if (stream_) free(stream_);}
    inline virtual bool append(T *data, unsigned int size) {
        if (capacity_ - (pos_*type_size_+valid_len_) <= diff_) {
            memcpy(stream_, stream_+pos_, valid_len_);
            pos_ = 0;
        }
        if (capacity_ - (pos_*type_size_+valid_len_) <= diff_) return false;;
        memcpy(stream_+pos_+valid_len_/type_size_, data, size);
        valid_len_ += size;
        return true;
    }
    inline virtual bool consume(T *data, unsigned int size) {
        if (valid_len_ <= size) return false;
        if (data) memcpy(data, stream_+pos_, size);
        pos_ += size%type_size_==0?size/type_size_:size/type_size_+1;
        valid_len_ -= size;
        return true;
    }
    inline virtual bool clear(unsigned int size) {
        if (size > valid_len_) return false;
        valid_len_ -= size;
    }
    inline virtual void clear_all() {
        pos_ = 0;
        valid_len_ = 0;
    }
    inline virtual T* data() {
        return stream_+pos_;
    }
    inline virtual unsigned int get_capacity() {
        return capacity_;
    }
    inline unsigned int get_type_size() {
        return type_size_;
    }
    inline virtual unsigned int valid_len() {
        return valid_len_;
    }
    inline virtual void reset() {
        pos_ = 0;
        valid_len_ = 0;
    }
private:
    ff_stream_base();
    ff_stream_base(const ff_stream_base&);
    ff_stream_base& operator =(const ff_stream_base&);
    unsigned int capacity_;
    T *stream_;
    unsigned int diff_;
    unsigned int pos_ = 0;
    unsigned int valid_len_ = 0;
    unsigned int type_size_;
};

template<typename T>
class ff_safe_stream: public ff_stream_base<T> {
public:
    ff_safe_stream(unsigned int capacity, unsigned int diff):
        ff_stream_base<T>(capacity, diff),
        canceled_(false) {}
    virtual bool append(T *data, unsigned int size) override {
        std::unique_lock<std::mutex> lock(m_);
        while(!canceled_.load() && !ff_stream_base<T>::append(data, size)) {
            //std::cout << "safe stream append in blocking" << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }
    virtual bool consume(T *data, unsigned int size) override {
        std::unique_lock<std::mutex> lock(m_);
        while(!canceled_.load() && !ff_stream_base<T>::consume(data, size)) {
            //std::cout << "safe stream consume in blocking" << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }
    virtual bool clear(unsigned int size) override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_stream_base<T>::clear(size);
        cv_.notify_all();
    }
    virtual void clear_all() {
        std::unique_lock<std::mutex> lock(m_);
        ff_stream_base<T>::clear_all();
        cv_.notify_all();
    }
    virtual T* data() override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_stream_base<T>::data();
    }
    virtual unsigned int get_capacity() override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_stream_base<T>::get_capacity();
    }
    void cancel() {
        std::unique_lock<std::mutex> lock(m_);
        canceled_.store(true);
        cv_.notify_all();
    }
    virtual unsigned int valid_len() override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_stream_base<T>::valid_len();
    }
    virtual void reset() {
        std::unique_lock<std::mutex> lock(m_);
        ff_stream_base<T>::reset();
        canceled_.store(false);
        cv_.notify_all();
    }
    inline bool is_canceled() {
        std::unique_lock<std::mutex> lock(m_);
        return canceled_.load();
    }
    ~ff_safe_stream() {}
private:
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool canceled_;
};
}


#endif // FF_STREAM_BASE_H
