#ifndef FF_PLAYER_H
#define FF_PLAYER_H

#include "ff_player_base.h"
#include "ff_asyn_timer.h"
#include "ff_player_face.h"
#include "ff_asyn_decoder.h"
#include "ff_blocking_audio_player.h"
#include "task_pool_sync.h"
#include "ff_confi.h"

namespace FFPlayer {
class ff_player: public ff_player_base {
public:
    ff_player(QApplication& app,
              const char *file,
              const unsigned int dest_width = 400,
              const unsigned int dest_height = 300,
              const unsigned int out_sample_rate = 8000):
        ff_player_base(app,
                       file,
                       dest_width,
                       dest_height,
                       out_sample_rate),
        queue_(new ff_safe_queue<ff_decoder_base::frame_args>(ff_player_queue_default_max_size)),
        timer_(ff_player_timer_default_loop_microseconds,
               true,[this](void *arg){return timer_task(arg);}),
        face_(this),
        decoder_(file,
                 queue_,
                 dest_width,
                 dest_height,
                 out_sample_rate),
        audio_player_(ff_blocking_audio_player::get_audio_player()),
        closed_cb_(NULL),
        atp_(ff_player_task_pool_default_max_size),
        vtp_(ff_player_task_pool_default_max_size),
        uitp_(ff_player_task_pool_default_max_size) {
        decoder_.set_seek_cb([this]() {
            if (seek_starting_.load()) {
                if (seek_pos_.load()-1.0<=0.0 && seek_pos_.load()-1.0>=-1.0) {
                    seek_starting_.store(false);
                    file_in_playing_ = file_;
                    decoder_.clear_buffer();
                    decoder_.cancel();
                    return true;
                }
                if (face_.player_slider_.maximum()-seek_pos_.load() <= 1) {
                    seek_starting_.store(false);
                    decoder_.clear_buffer();
                    decoder_.cancel();
                    return true;
                }
                if (decoder_.seek_video(decoder_.get_duration()/(double)(face_.player_slider_.maximum())\
                                        *(double)seek_pos_.load())) {
                    decoder_.set_unend();
                    seek_starting_.store(false);
                    decoder_.clear_buffer();
                    tem_fa_.ft = ff_decoder_base::Unknow_Frame;
                    ff_decoder_base::frame_queue pq;
                    if (!decoder_.decode_packet(pq)) return false;
                    ff_decoder_base::frame_args fa = pq.queue.front();
                    timer_interval_.store(fa.position*1000);
                    for (int i = 0; i < pq.queue.size(); i++) {
                        if (!queue_->enpacket_with_sort(pq.queue, [](const ff_decoder_base::frame_args& fa1,
                                                                     const ff_decoder_base::frame_args& fa2){
                            return fa1.position < fa2.position;
                        })) return false;
                    }
                    return true;
                }
                return false;
            }
            return true;
        });
        decoder_.set_end_decode_cb([this]() {
            std::cout << "decode end." << std::endl;
            if (decoder_.is_canceled()) {
                decoder_.cancel();
                uitp_.close([this](void*){std::cout<<"uitp_ closed."<<std::endl;uitp_.reset();});
                vtp_.close([this](void*){std::cout<<"vtp_ closed."<<std::endl;vtp_.reset();});
                atp_.close([this](void*){std::cout<<"atp_ closed."<<std::endl;atp_.reset();});
                if (audio_player_->stop()) {audio_player_->close();}
                std::cout<<"audio_player_ closed."<<std::endl;
                timer_.cancel([this](ff_asyn_timer*){
                    std::cout<<"timer_ closed."<<std::endl;
                    decoder_.reset(file_in_playing_);
                    av_playing_closed_cb();
                    return true;
                });
            }
        });
    }

    void* timer_task(void*) {
        timer_interval_.fetch_add(ff_player_timer_default_loop_microseconds);
        if (decoder_.is_end()) {
            if (queue_->get_size() == 0) {
                player_next(file_);
                uitp_.close([this](void*){std::cout<<"uitp_ closed."<<std::endl;uitp_.reset();});
                vtp_.close([this](void*){std::cout<<"vtp_ closed."<<std::endl;vtp_.reset();});
                atp_.close([this](void*){std::cout<<"atp_ closed."<<std::endl;atp_.reset();});
                if (audio_player_->stop()) {audio_player_->close();}
                std::cout<<"audio_player_ closed."<<std::endl;
                timer_.cancel([this](ff_asyn_timer*){
                    std::cout<<"timer_ closed."<<std::endl;
                    decoder_.reset(file_);
                    av_playing_closed_cb();
                    return true;
                });
            }
        }
        if (tem_fa_.ft==ff_decoder_base::Unknow_Frame) queue_->dequeue(tem_fa_);
        if (tem_fa_.position*1000.0 <= timer_interval_.load()) {
            ff_asyn_decoder::frame_args fa = tem_fa_;
            if (tem_fa_.ft == ff_asyn_decoder::Video_Frame && tem_fa_.size > 0) {
                vtp_.add_task([this, fa](){
                    if (!fa.video_stream->consume(vb_, fa.size)) return;
                    QPixmap qmp;
                    ff_pixel_format_transformer::rgb888_to_qig(vb_,
                                                               decoder_.get_dest_width(),
                                                               decoder_.get_dest_height(),
                                                               qmp);
                    if (!resized_.load()) {
                        face_.content_face_.setPixmap(qmp);
                    }
                    resized_.store(false);
                });
            } else if (tem_fa_.ft == ff_asyn_decoder::Audio_Frame && tem_fa_.size > 0) {
                ff_asyn_decoder::frame_args fa = tem_fa_;
                atp_.add_task([this, fa]{
                    while (audio_player_->write_available()>=ab_total_len_ \
                           && !fa.audio_stream->is_canceled()) {
                        if (fa.audio_stream->valid_len() >= ab_total_len_) {
                            if (consumed_pcm_len_/((double)out_sample_rate_*2.0)<=(fa.position+fa.duration)) {
                                consumed_pcm_len_ += ab_total_len_;
                                if (!fa.audio_stream->consume(ab_, ab_total_len_)) break;
                                if (!audio_player_->play(ab_)) break;
                            } else {
                                if (!audio_player_->play((void*)ff_audio_default_output_fake_data)) break;
                            }
                        }
                    }
                });
            }
            uitp_.add_task([this, fa](){
                if (!face_.player_slider_.isSliderDown()) {
                    double ui_max_pos = face_.player_slider_.maximum();
                    face_.player_slider_.setSliderPosition(fa.position/(decoder_.get_duration()/ui_max_pos));
                }
            });
            tem_fa_.ft = ff_decoder_base::Unknow_Frame;
        }
        return (void*)0;
    }

    ~ff_player() {}

    bool start_up_audio_player() {
        audio_player_->set_channel_nb(ff_audio_default_output_channel_nb);
        audio_player_->set_frames_per_buffer(ff_audio_deafult_output_frames_per_buffer_nb);
        audio_player_->set_sample_rate(out_sample_rate_);
        audio_player_->set_sample_format(ff_audio_default_output_sample_format);
        if (audio_player_->prepare()) {
            if (audio_player_->start()) return true;
        }
        return false;
    }

    bool play() {
        if (start_up_audio_player()) {
            atp_.start();
            vtp_.start();
            uitp_.start();
            if (decoder_.start()) {
                timer_.start();
                return true;
            }
        }
        return false;
    }

    void close(std::function<void(ff_player *)> closed_cb) {
        closed_cb_ = closed_cb;
        decoder_.cancel();
    }
    virtual void player_pause() override {
        timer_.cancel(nullptr);
    }
    virtual void player_start() override {
        timer_.start();
    }
    virtual void player_next(const char *next_file) override {
        file_in_playing_ = next_file;
        close([this](ff_player *){
            std::cout<<"player closed."<<std::endl;
            play();
        });
    }
    virtual void player_last(const char *last_file) override {
        file_in_playing_ = last_file;
        close([this](ff_player *){
            std::cout<<"player closed."<<std::endl;
            play();
        });
    }
    virtual void player_slide(int pos) override {
        seek_pos_.store(pos);
    }
    virtual void slider_release() override {
        seek_starting_.store(true);
    }
    virtual void player_resize() override {
        resized_.store(true);
    }
    virtual void player_close() override {}

protected:
    void av_playing_closed_cb() {
        reset();
        if (closed_cb_) closed_cb_(this);
    }

    void reset() {
        file_ = file_in_playing_;
        timer_interval_.store(0);
        consumed_pcm_len_ = 0.0;
        tem_fa_ = ff_decoder_base::frame_args();;
        seek_starting_.store(false);
        seek_pos_.store(0);
        resized_.store(false);
        std::cout << "player reset." << std::endl;
    }

private:
    std::shared_ptr<ff_safe_queue<ff_decoder_base::frame_args>> queue_;
    ff_asyn_timer timer_;
    ff_player_face face_;
    ff_asyn_decoder decoder_;
    ff_blocking_audio_player *audio_player_;
    task_pool_sync atp_;
    task_pool_sync vtp_;
    task_pool_sync uitp_;
    std::function<void(ff_player *)> closed_cb_ = nullptr;
};
}

#endif // FF_PLAYER_H
