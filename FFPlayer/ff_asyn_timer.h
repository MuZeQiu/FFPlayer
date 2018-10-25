#ifndef FF_ASYN_TIMER_H
#define FF_ASYN_TIMER_H

#include <iostream>
#include <assert.h>
#include <chrono>
#include <functional>
#include <atomic>
#include <thread>

namespace FFPlayer {
class ff_asyn_timer {
public:
    ff_asyn_timer(const ff_asyn_timer&) = delete;
    ff_asyn_timer& operator=(const ff_asyn_timer&) = delete;

    ff_asyn_timer(unsigned int ms,
                  bool repeated,
                  std::function<void*(void*)> task):
        ms_(ms),
        task_(task),
        clock_(),
        canceled_(false),
        repeated_(repeated),
        timer_out_cb_(NULL) {
        assert(ms!=0);
    }

    ~ff_asyn_timer() {}

    void start() {
        std::cout << "timer start." << std::endl;
        canceled_.store(false);
        std::thread t = std::thread([this](){
            std::cout << "timer thread start." << std::endl;
            timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>\
                    (clock_.now().time_since_epoch());
            do {
                std::chrono::milliseconds timestamp = \
                        std::chrono::duration_cast<std::chrono::milliseconds>\
                                (clock_.now().time_since_epoch());
                std::chrono::milliseconds dif = timestamp - timestamp_;
                if (dif >= ms_) {
                    if (task_) task_(this);
                    timestamp_ = timestamp;
                    if (!repeated_.load()) {
                        break;
                    }
                }
            } while(!canceled_.load());
            if (timer_out_cb_) timer_out_cb_(this);
        });
        t.detach();
    }

    unsigned long long get_interval() const {return ms_.count();}

    void cancel(std::function<bool(ff_asyn_timer *)> timer_out_cb) {
        timer_out_cb_ = timer_out_cb;
        canceled_.store(true);
    }

    template<typename T = std::chrono::microseconds>
    static unsigned long long get_timestamp() {
        return std::chrono::duration_cast<T>\
                (std::chrono::steady_clock().now().time_since_epoch()).count();
    }

private:
    ff_asyn_timer();
    std::chrono::milliseconds ms_;
    std::chrono::milliseconds timestamp_;
    std::function<void*(void*)> task_;
    std::chrono::steady_clock clock_;
    std::atomic_bool canceled_;
    std::atomic_bool repeated_;
    std::function<bool(ff_asyn_timer *)> timer_out_cb_;
};
}

#endif // FF_ASYN_TIMER_H
