#ifndef FF_PLAYER_BASE_H
#define FF_PLAYER_BASE_H

#include <iostream>
#include <atomic>
#include <QApplication>
#include "ff_player_event.h"
#include "ff_decoder_base.h"

namespace FFPlayer {
class ff_player_base: public ff_player_event {
public:
    ff_player_base(QApplication& app,
                   const char *file,
                   const unsigned int dest_width = 400,
                   const unsigned int dest_height = 300,
                   const unsigned int out_sample_rate = 8000):
        app_(app),
        file_(file),
        dest_width_(dest_width),
        dest_height_(dest_height),
        out_sample_rate_(out_sample_rate),
        timer_interval_(0),
        ab_total_len_(ff_audio_default_output_bytes_nb),
        vb_total_len_(ff_data_size::get_video_buffer_size(dest_width,
                                                          dest_height,
                                                          ff_decode_default_output_pixel_format,
                                                          1)),
        ab_((int16_t *)malloc(ab_total_len_)),
        vb_((uint8_t *)malloc(vb_total_len_)),
        seek_starting_(false),
        seek_pos_(0),
        resized_(false) {
        assert(file_);
        assert(ab_);
        assert(vb_);
    };
    ~ff_player_base() {
        if (ab_) free(ab_);
        if (vb_) free(vb_);
    }
    inline QApplication& get_app() {
        return app_;
    }
    inline const char *get_file() {
        return file_;
    }
    inline unsigned int get_dest_width() {
        return dest_width_;
    }
    inline unsigned int get_dest_height() {
        return dest_height_;
    }
    inline unsigned int get_out_sample_rate() {
        return out_sample_rate_;
    }
private:
    ff_player_base();
    ff_player_base(const ff_player_base&);
    ff_player_base& operator =(const ff_player_base&);
protected:
    QApplication& app_;
    const char *file_;
    unsigned int dest_width_;
    unsigned int dest_height_;
    unsigned int out_sample_rate_;
    unsigned int ab_total_len_;
    unsigned int vb_total_len_;
    std::atomic_uint timer_interval_;
    int16_t *ab_;
    uint8_t *vb_;
    double consumed_pcm_len_ = 0.0;
    ff_decoder_base::frame_args tem_fa_;
    std::atomic_bool seek_starting_;
    std::atomic_uint seek_pos_;
    std::atomic_bool resized_;
    const char *file_in_playing_ = nullptr;
};
}

#endif // FF_PLAYER_BASE_H
