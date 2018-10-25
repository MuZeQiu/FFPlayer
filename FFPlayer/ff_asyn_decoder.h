#ifndef FF_ASYN_DECODER_H
#define FF_ASYN_DECODER_H

#include <iostream>
#include <thread>
#include <assert.h>
#include "ff_decoder_base.h"
#include "ff_queue_base.h"

namespace FFPlayer {
class ff_asyn_decoder: public ff_decoder_base {
public:

    ff_asyn_decoder(const char *file,
                    std::shared_ptr<ff_safe_queue<ff_decoder_base::frame_args>>& av_queue,
                    const unsigned int dest_width,
                    const unsigned int dest_height,
                    const unsigned int out_sample_rate):
        ff_decoder_base(file,
                        dest_width,
                        dest_height,
                        out_sample_rate),
        dec_thr_(),
        av_queue_(av_queue),
        cancel_(false),
        seek_cb_(nullptr),
        end_decode_cb_(nullptr),
        ended_(false) {
        assert(file);
    }

    bool start() {
        if (prepare()) {
            std::cout << "decoder prepare" << std::endl;
            asyn_decode();
            return true;
        }
        return false;
    }

    ~ff_asyn_decoder() {
        if (dec_thr_.joinable())
            dec_thr_.join();
    }

    void asyn_decode() {
        std::thread dec_thr([this](){
            while(!cancel_.load()) {
                if (seek_cb_) {
                    if (!seek_cb_()) {
                        handle_error();
                        break;
                    }
                }
                ff_decoder_base::frame_queue pq;
                if (decode_packet(pq)) {
                    for (int i = 0; i < pq.queue.size(); i++) {
                        if (!av_queue_->enpacket_with_sort(pq.queue,[](const frame_args& fa1,
                                                                       const frame_args& fa2){
                            return fa1.position < fa2.position;
                        })) {
                            handle_error();
                            break;
                        }
                    }
                } else break;
            }
            ended_.store(true);
            if (end_decode_cb_) end_decode_cb_();
        });
        dec_thr_.swap(dec_thr);
        if (dec_thr.joinable())
            dec_thr.join();
    }

    virtual void clear_buffer() override {
        ff_decoder_base::clear_buffer();
        av_queue_->clear();
    }

    virtual void cancel() override {
        ff_decoder_base::cancel();
        av_queue_->cancel();
        cancel_.store(true);
    }

    virtual void reset(const char *file) override {
        ff_decoder_base::reset(file);
        cancel_.store(false);
        av_queue_->reset(av_queue_->get_max_size());
        // seek_cb_ = nullptr;
        // end_decode_cb_ = nullptr;
        ended_.store(false);
        std::cout << "decoder reset" << std::endl;
    }

    void set_seek_cb(std::function<bool()> seek_cb) {
        seek_cb_ = seek_cb;
    }

    void set_end_decode_cb(std::function<void()> end_decode_cb) {
        end_decode_cb_ = end_decode_cb;
    }

    bool is_end() {
        return ended_.load();
    }

    void set_unend() {
        ended_.store(false);
    }

    bool is_canceled() {
        return cancel_.load();
    }

private:
    std::thread dec_thr_;
    std::shared_ptr<ff_safe_queue<ff_decoder_base::frame_args>>& av_queue_;
    std::atomic_bool cancel_;
    std::function<bool()> seek_cb_;
    std::function<void()> end_decode_cb_;
    std::atomic_bool ended_;
};
}

#endif // FF_ASYN_DECODER_H
