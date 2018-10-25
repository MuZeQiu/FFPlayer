#include "ff_confi.h"

namespace FFPlayer {
 enum AVSampleFormat ff_decode_deafult_out_sample_format = AV_SAMPLE_FMT_S16;
 AVPixelFormat       ff_decode_default_output_pixel_format = AV_PIX_FMT_RGB24;
 uint64_t            ff_decode_default_out_channel_layout = AV_CH_LAYOUT_MONO;
 PaSampleFormat      ff_audio_default_output_sample_format = paInt16;
 unsigned int        ff_audio_default_output_channel_nb = 1;
 unsigned int        ff_audio_deafult_output_frames_per_buffer_nb = 512;
 unsigned int        ff_audio_default_output_bytes_nb = 2*ff_audio_deafult_output_frames_per_buffer_nb;
 uint16_t            ff_audio_default_output_fake_data[512] = {0};
 unsigned int        ff_player_queue_default_max_size = 50;
 unsigned int        ff_player_task_pool_default_max_size = 50;
 unsigned int        ff_player_timer_default_loop_microseconds = 5;
}
