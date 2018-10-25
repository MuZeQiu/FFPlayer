#ifndef FF_CONFI_H
#define FF_CONFI_H

#include <QImage>
#include <QPixmap>
#include <portaudio.h>
extern "C" {
#include <libavformat/avformat.h>
}

namespace FFPlayer {
extern enum AVSampleFormat ff_decode_deafult_out_sample_format;
extern AVPixelFormat       ff_decode_default_output_pixel_format;
extern uint64_t            ff_decode_default_out_channel_layout;
extern PaSampleFormat      ff_audio_default_output_sample_format;
extern unsigned int        ff_audio_default_output_channel_nb;
extern unsigned int        ff_audio_deafult_output_frames_per_buffer_nb;
extern unsigned int        ff_audio_default_output_bytes_nb;
extern uint16_t            ff_audio_default_output_fake_data[512];
extern unsigned int        ff_player_queue_default_max_size;
extern unsigned int        ff_player_task_pool_default_max_size;
extern unsigned int        ff_player_timer_default_loop_microseconds;

static inline double calculate_pcm_duration(double sample_rate,
                                            double channel_nb,
                                            double sample_format_bytes_nb,
                                            double pcm_bytes_len) {
    return pcm_bytes_len/(sample_rate*channel_nb*sample_format_bytes_nb);
}

class ff_pixel_format_transformer {
public:
    static void rgb888_to_qig(const uint8_t *rgb888,
                              const unsigned int width,
                              const unsigned int height,
                              QPixmap &qmp) {
        qmp = QPixmap::fromImage(QImage(rgb888, width, height, QImage::Format_RGB888));
    }
};

}

#endif // FF_CONFI_H
