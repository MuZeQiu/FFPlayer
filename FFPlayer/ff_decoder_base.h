#ifndef FF_DECODER_BASE_H
#define FF_DECODER_BASE_H

#include <assert.h>
#include <iostream>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include "ff_stream_base.h"
#include "ff_queue_base.h"
#include "ff_data_size.h"
#include "ff_confi.h"

namespace FFPlayer {
class ff_decoder_base {
public:
    typedef enum {
        Unknow_Frame = 2,
        Video_Frame = 4,
        Audio_Frame = 8
    } Frame_Type;

    typedef enum {
        Success = 2,
        Fail = 4,
        No_More = 8
    } Decode_Status;

    class frame_args {
    public:
        frame_args():
            ft(Unknow_Frame),
            position(0.0),
            duration(0.0),
            size(0),
            audio_stream(NULL),
            video_stream(NULL){}
        Frame_Type ft;
        double position;
        double duration;
        unsigned int size;
        ff_safe_stream<int16_t> *audio_stream;
        ff_safe_stream<uint8_t> *video_stream;
    };

    class frame_queue {
    public:
        Frame_Type type = Unknow_Frame;
        std::deque<frame_args> queue;
    } ;

    explicit ff_decoder_base(const char *file,
                             const unsigned int dest_width,
                             const unsigned int dest_height,
                             const unsigned int out_sample_rate):
        file_(file),
        dest_width_(dest_width),
        dest_height_(dest_height),
        out_sample_rate_(out_sample_rate),
        dest_audio_frame_buf_((uint8_t *)malloc(ff_data_size::get_audio_buffer_size(out_sample_rate,
                                                                                    ff_decode_default_out_channel_layout,
                                                                                    ff_decode_deafult_out_sample_format,
                                                                                    2))),
        audio_fss_diff_(ff_data_size::get_audio_buffer_size(out_sample_rate,
                                                            ff_decode_default_out_channel_layout,
                                                            ff_decode_deafult_out_sample_format,
                                                            1)),
        audio_fss_capacity_(ff_data_size::get_audio_buffer_size(out_sample_rate,
                                                                ff_decode_default_out_channel_layout,
                                                                ff_decode_deafult_out_sample_format,
                                                                5)),
        video_fss_diff_(ff_data_size::get_video_buffer_size(dest_width,
                                                            dest_height,
                                                            ff_decode_default_output_pixel_format,
                                                            1)),
        video_fss_capacity_(ff_data_size::get_video_buffer_size(dest_width,
                                                                dest_height,
                                                                ff_decode_default_output_pixel_format,
                                                                30)),
        audio_fss_(audio_fss_capacity_, audio_fss_diff_),
        video_fss_(video_fss_capacity_, video_fss_diff_)
    {
        assert(file_);
        assert(dest_audio_frame_buf_);
    }

    ~ff_decoder_base() {free(dest_audio_frame_buf_);}

    const char *get_file() const {return file_;}

    void set_file(const char *file) {
        file_ = file;
    }

    bool prepare() {
        if (find_stream_info()) {
            if (get_av_stream()) {
                print_av_info();
                if (get_audio_stream() >= 0) {
                    if (open_audio_codec()) {
                        set_video_time_base();
                        set_fps();
                    }
                }
                if (get_video_stream() >= 0) {
                    if (open_video_codec()) {
                        set_audio_time_base();
                    }
                }
                if (set_original_frame()) {
                    if (set_dest_frame()) {
                        if (set_dest_frame_buffer()) {
                            if (set_sws_context()) {
                                if (fill_picture()) {
                                    if (set_swr_context()) {
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    bool decode_packet(frame_queue& pq) {
        if (read_frame()) {
            if (compressed_video_packet()) {
                pq.type = Video_Frame;
                return decode_video_packet(pq.queue);
            }
            if (compressed_audio_packet()) {
                pq.type = Audio_Frame;
                return decode_audio_packet(pq.queue);
            }
            return decode_packet(pq);
        }
        handle_error();
        return false;
    }

    inline double get_duration() {
        return duration_;
    }

    unsigned int get_dest_width() const {
        return dest_width_;
    }

    unsigned int get_dest_height() const {
        return dest_height_;
    }

    AVPixelFormat get_dest_vft() const {
        return dest_vft_;
    }

    int get_out_sample_rate() const {
        return out_sample_rate_;
    }

    uint64_t get_out_ch_layout_() const {
        return out_ch_layout_;
    }

    enum AVSampleFormat get_out_sample_fmt() const {
        return out_sample_fmt_;
    }

    bool seek_audio(const double pos) {
        int64_t ts = (int64_t)(pos/audio_time_base_);
        err_code_ = avformat_seek_file(format_context_,
                                       audio_stream_,
                                       ts,
                                       ts,
                                       ts,
                                       AVSEEK_FLAG_FRAME);
        if (err_code_ < 0) {
            handle_error();
            return false;
        }
        avcodec_flush_buffers(audio_codec_context_);
        return true;
    }

    bool seek_video(const double pos) {
        int64_t ts = (int64_t)(pos/video_time_base_);
        err_code_ = avformat_seek_file(format_context_,
                                       video_stream_,
                                       ts,
                                       ts,
                                       ts,
                                       AVSEEK_FLAG_FRAME);
        if (err_code_ < 0) {
            handle_error();
            return false;
        }
        avcodec_flush_buffers(video_codec_context_);
        return true;
    }

    void handle_error() {
        if (swr_context_) {
            swr_free(&swr_context_);
            swr_context_ = NULL;
        }
        if (sws_context_) {
            sws_freeContext(sws_context_);
            sws_context_ = NULL;
        }
        if (buffer_) {
            av_free(buffer_);
            buffer_ = NULL;
        }
        if (dest_frame_) {
            av_frame_free(&dest_frame_);
            dest_frame_ = NULL;
        }
        if (video_codec_context_) {
            avcodec_close(video_codec_context_);
            video_codec_context_ = NULL;
        }
        if (original_frame_) {
            av_frame_free(&original_frame_);
            original_frame_ = NULL;
        }
        if (audio_codec_context_) {
            avcodec_close(audio_codec_context_);
            audio_codec_context_ = NULL;
        }
        if (format_context_) {
            avformat_close_input(&format_context_);
            format_context_ = NULL;
        }
    }

    void clear() {
        handle_error();
        file_ = NULL;
        format_context_ = NULL;
        video_stream_ = -1;
        audio_stream_ = -1;
        video_codec_context_ = NULL;
        video_codec_ = NULL;
        audio_codec_context_ = NULL;
        audio_codec_ = NULL;
        original_frame_ = NULL;
        dest_frame_ = NULL;
        frame_finished_ = 0.0;
        num_bytes_ = 0.0;
        buffer_ = NULL;
        dict_ = NULL;
        sws_context_ = NULL;
        video_time_base_ = 0.0;
        audio_time_base_ = 0.0;
        fps_ = 0.0;
        swr_context_ = NULL;
        err_code_ = 0;
        duration_ = 0.0;
        in_sample_fmt_ = AV_SAMPLE_FMT_NONE;
        out_sample_fmt_ = ff_decode_deafult_out_sample_format;
        in_sample_rate_ = 0;
        //out_sample_rate_ = 0;
        in_ch_layout_ = 0;
        out_ch_layout_ = ff_decode_default_out_channel_layout;
        out_channel_nb_ = 0;
        dest_vft_ = ff_decode_default_output_pixel_format;
        //dest_width_ = 0;
        //dest_height_ = 0;
        packet_size_ = 0;

        audio_fss_.clear_all();
        video_fss_.clear_all();
    }

    virtual void reset(const char *file) {
        clear();
        audio_fss_.reset();
        video_fss_.reset();
        file_ = file;
    }

    virtual void cancel() {
        audio_fss_.cancel();
        video_fss_.cancel();
    }

    virtual void clear_buffer() {
        audio_fss_.clear_all();
        video_fss_.clear_all();
    }

private:
    bool find_stream_info() {
        av_register_all();
        format_context_ = avformat_alloc_context();
        err_code_ = avformat_open_input(&format_context_, file_, NULL, NULL);
        if (err_code_) {
            handle_error();
            return false;
        }
        err_code_ = avformat_find_stream_info(format_context_, NULL);
        if (err_code_ < 0) {
            handle_error();
            return false;
        }
        return true;
    }

    bool get_av_stream() {
        if (format_context_->duration == AV_NOPTS_VALUE)
            duration_ = std::numeric_limits<float>::max();
        else
            duration_ = static_cast<double>(format_context_->duration)/static_cast<double>(AV_TIME_BASE);
        video_stream_ = av_find_best_stream(format_context_,
                                            AVMEDIA_TYPE_VIDEO,
                                            -1,
                                            -1,
                                            NULL,
                                            0);
        audio_stream_ = av_find_best_stream(format_context_,
                                            AVMEDIA_TYPE_AUDIO,
                                            -1,
                                            -1,
                                            NULL,
                                            0);
        if (audio_stream_ < 0 && video_stream_ < 0) {
            handle_error();
            return false;
        }
        return true;
    }

    bool open_video_codec() {
        video_codec_context_ = format_context_->streams[video_stream_]->codec;
        if (!video_codec_context_) {
            handle_error();
            return false;
        }
        video_codec_ = avcodec_find_decoder(video_codec_context_->codec_id);
        if (!video_codec_) {
            handle_error();
            return false;
        }
        err_code_ = avcodec_open2(video_codec_context_, video_codec_, &dict_);
        if (err_code_ < 0) {
            handle_error();
            return false;
        }
        return true;
    }

    bool open_audio_codec() {
        audio_codec_context_ = format_context_->streams[audio_stream_]->codec;
        if (!audio_codec_context_) {
            handle_error();
            return false;
        }
        audio_codec_ = avcodec_find_decoder(audio_codec_context_->codec_id);
        if (!audio_codec_) {
            handle_error();
            return false;
        }
        err_code_ = avcodec_open2(audio_codec_context_, audio_codec_, &dict_);
        if (err_code_ < 0) {
            handle_error();
            return false;
        }
        return true;
    }

    int get_video_stream() const {
        return video_stream_;
    }

    int get_audio_stream() const {
        return audio_stream_;
    }

    void set_video_time_base() {
        AVStream *st = format_context_->streams[video_stream_];
        if (st->time_base.den && st->time_base.num) {
            video_time_base_ = av_q2d(st->time_base);
        } else if (st->codec->time_base.den && st->codec->time_base.num) {
           video_time_base_ = av_q2d(st->codec->time_base);
        } else {
            video_time_base_ = 1.0/25.0;
        }
    }

    void set_fps() {
        AVStream *st = format_context_->streams[video_stream_];
        if (st->avg_frame_rate.den && st->avg_frame_rate.num) {
            fps_ = av_q2d(st->avg_frame_rate);
        } else if (st->r_frame_rate.den && st->r_frame_rate.num) {
            fps_ = av_q2d(st->r_frame_rate);
        } else {
            fps_ = 1.0/video_time_base_;
        }
    }

    void set_audio_time_base() {
        AVStream *st = format_context_->streams[audio_stream_];
        if (st->time_base.den && st->time_base.num) {
            audio_time_base_ = av_q2d(st->time_base);
        } else if (st->codec->time_base.den && st->codec->time_base.num) {
           audio_time_base_ = av_q2d(st->codec->time_base);
        } else {
            audio_time_base_ = 0.025;
        }
    }

    bool set_original_frame() {
        original_frame_ = av_frame_alloc();
        if (!original_frame_) {
            handle_error();
            return false;
        }
        return true;
    }

    bool set_dest_frame() {
        dest_frame_ = av_frame_alloc();
        if (!dest_frame_) {
            handle_error();
            return false;
        }
        return true;
    }

    bool set_dest_frame_buffer() {
        num_bytes_ = avpicture_get_size(dest_vft_,
                                        dest_width_,
                                        dest_height_);
        buffer_ = (uint8_t *)av_malloc(num_bytes_);
        if (!buffer_) {
            handle_error();
            return false;
        }
        return true;
    }

    bool set_sws_context() {
        sws_context_ = sws_getContext(video_codec_context_->width,
                                      video_codec_context_->height,
                                      video_codec_context_->pix_fmt,
                                      dest_width_,
                                      dest_height_,
                                      (enum AVPixelFormat)dest_vft_,
                                      SWS_BICUBIC,
                                      NULL,
                                      NULL,
                                      NULL);
        if (!sws_context_) {
            handle_error();
            return false;
        }
        return true;
    }

    bool fill_picture() {
        err_code_ = avpicture_fill((AVPicture*)dest_frame_,
                                   buffer_,
                                   (enum AVPixelFormat)dest_vft_,
                                   dest_width_,
                                   dest_height_);
        if (err_code_ < 0) {
            handle_error();
            return false;
        }
        return true;
    }

    bool set_swr_context() {
        if (audio_stream_ >= 0) {
            swr_context_ = swr_alloc();
            if (!swr_context_) {
                handle_error();
                return false;
           }
            in_sample_fmt_ = audio_codec_context_->sample_fmt;
            in_sample_rate_ = audio_codec_context_->sample_rate;
            in_ch_layout_ = audio_codec_context_->channel_layout;
            swr_context_ = swr_alloc_set_opts(swr_context_,
                                         out_ch_layout_,
                                         out_sample_fmt_,
                                         out_sample_rate_,
                                         in_ch_layout_,
                                         in_sample_fmt_,
                                         in_sample_rate_,
                                         0,
                                         NULL);
            if (!swr_context_ || swr_init(swr_context_)) {
                handle_error();
                return false;
            }
            out_channel_nb_ = av_get_channel_layout_nb_channels(out_ch_layout_);
        }
        return true;
    }

private:
    bool read_frame() {
        int len = av_read_frame(format_context_, &packet_);
        if (len < 0) {
            handle_error();
            return false;
        }
        packet_size_ = packet_.size;
        return true;
    }

    double get_video_frame_position() {
        return av_frame_get_best_effort_timestamp(original_frame_)*\
                video_time_base_;
    }

    double get_video_frame_duration() {
        double frame_duration = 0.0;
        int64_t pkt_duration = av_frame_get_pkt_duration(original_frame_);
        if (pkt_duration) {
            frame_duration = pkt_duration*video_time_base_;
            frame_duration += original_frame_->repeat_pict*video_time_base_*0.5;
        } else {
            frame_duration = 1.0/fps_;
        }
        return frame_duration;
    }

    int video_frame_scale() {
        return sws_scale(sws_context_,
                  (const uint8_t* const*)original_frame_->data,
                  original_frame_->linesize,
                  0,
                  video_codec_context_->height,
                  dest_frame_->data,
                  dest_frame_->linesize);
    }

    uint8_t * get_video_frame(unsigned int& fs) {
        int size = dest_width_ * dest_height_;
        uint8_t *frame = NULL;
        if (dest_vft_ == AV_PIX_FMT_RGB24) {
            fs = 3 * size;
            cv::Mat img(cv::Size((int)get_dest_width(),(int)get_dest_height()),
                        CV_8UC3,
                        (void *)(dest_frame_->data[0]));
            std::string text = "QMZ";
            double text_size = 3.0;
            int color_num = 128;
            int text_width = 3;
            cv::putText(img,
                    text,
                    cv::Point(img.cols * 0, img.rows * 0.9),
                    cv::FONT_HERSHEY_PLAIN,
                    text_size,
                    cv::Scalar(color_num, color_num, color_num),
                    text_width);
            if (!video_fss_.append(img.data, fs)) {
                handle_error();
                return NULL;
            }
            img.release();
            return (uint8_t*)&video_fss_;
        } else {
            fs = size * 3 / 2;
            frame = (uint8_t *)malloc(fs);
            if (!frame) {
                handle_error();
                return frame;
            }
            memcpy(frame, dest_frame_->data[0], size);
            memcpy(frame+size, dest_frame_->data[1], size/4);
            memcpy(frame+size*5/4, dest_frame_->data[2], size/4);
            return frame;
        }
    }

    bool handle_video_frame(frame_args& fa) {
        double frame_position = get_video_frame_position();
        double frame_duration = get_video_frame_duration();
        if (video_frame_scale() != dest_height_) {
            handle_error();
            return false;
        }
        unsigned int frame_size = 0;
        uint8_t *video_frame = NULL;
        if (!(video_frame = get_video_frame(frame_size))) {
            handle_error();
            return false;
        }
        if (dest_vft_ == AV_PIX_FMT_YUV420P) {
            if (!video_fss_.append(video_frame, frame_size)) {
                free(video_frame);
                handle_error();
                return false;
            }
            free(video_frame);
        }
        fa.ft = Video_Frame;
        fa.position = frame_position;
        fa.duration = frame_duration;
        fa.size = frame_size;
        fa.video_stream = &video_fss_;
        return true;
    }

    double get_audio_frame_position() {
        return av_frame_get_best_effort_timestamp(original_frame_)*\
                audio_time_base_;
    }

    double get_audio_frame_duration() {
        return av_frame_get_pkt_duration(original_frame_) * audio_time_base_;
    }

    int audio_frame_convert() {
        return swr_convert(swr_context_,
                           &dest_audio_frame_buf_,
                           20 * 44100,
                           (const uint8_t **)original_frame_->data,
                           original_frame_->nb_samples);
    }

    int conver_audio_buffer_size(const unsigned int out_samples) {
        return av_samples_get_buffer_size(NULL,
                                          out_channel_nb_,
                                          out_samples,
                                          out_sample_fmt_,
                                          1);
    }

    int16_t * get_audio_frame(unsigned int& frame_size) {
        int out_samples = audio_frame_convert();
        if (out_samples > 0) {
            int out_buffer_size = conver_audio_buffer_size(out_samples);
            if (out_buffer_size < 0) {
                handle_error();
                return NULL;
            }
            frame_size = out_buffer_size;
            int16_t *dest_audio_frame_buf = (int16_t*)dest_audio_frame_buf_;
            return dest_audio_frame_buf;
        }
        handle_error();
        return NULL;
    }

    bool handle_audio_frame(frame_args& fa) {
        double frame_position = get_audio_frame_position();
        double frame_duration = get_audio_frame_duration();
        unsigned int frame_size = 0;
        int16_t *pcm = get_audio_frame(frame_size);
        if (frame_size > 0) {
            if (!audio_fss_.append(pcm, frame_size)) {
                handle_error();
                return false;
            }
        }
        fa.ft = Audio_Frame;
        fa.position = frame_position;
        fa.duration = frame_duration;
        fa.size = frame_size;
        fa.audio_stream = &audio_fss_;
        return true;
    }

    Decode_Status decode_video_frame(frame_args& fa) {
        int decode_len = avcodec_decode_video2(video_codec_context_,
                                               original_frame_,
                                               &frame_finished_,
                                               &packet_);
        if (decode_len > 0) {
            if (frame_finished_) {
                if (!handle_video_frame(fa)) {
                    handle_error();
                    return Fail;
                }
            }
            packet_size_ -= decode_len;
            return Success;
        } else if (!decode_len) {
            return No_More;
        }
        handle_error();
        return Fail;
    }

    Decode_Status decode_audio_frame(frame_args& fa) {
        int decode_len = avcodec_decode_audio4(audio_codec_context_,
                                               original_frame_,
                                               &frame_finished_,
                                               &packet_);
        if (decode_len > 0) {
            if (frame_finished_) {
                if (!handle_audio_frame(fa)) {
                    handle_error();
                    return Fail;
                }
            }
            packet_size_ -= decode_len;
            return Success;
        } else if (!decode_len) {
            return No_More;
        }
        handle_error();
        return Fail;
    }

    bool decode_video_packet(std::deque<frame_args>& pq) {
        while(packet_size_) {
            frame_args fa;
            Decode_Status ds = decode_video_frame(fa);
            if (ds == Success) {
                pq.push_back(fa);
            } else if (ds == No_More) {
                break;
            } else {
                handle_error();
                return false;
            }
        }
        av_free_packet(&packet_);
        return true;
    }

    bool decode_audio_packet(std::deque<frame_args>& pq) {
        while(packet_size_) {
            frame_args fa;
            Decode_Status ds = decode_audio_frame(fa);
            if (ds == Success) {
                pq.push_back(fa);
            } else if (ds == No_More) {
                break;
            } else {
                handle_error();
                return false;
            }
        }
        av_free_packet(&packet_);
        return true;
    }

    bool compressed_video_packet() {
        return packet_.stream_index == video_stream_;
    }

    bool compressed_audio_packet() {
        return packet_.stream_index == audio_stream_;
    }

private:
    void print_av_info() {
        av_dump_format(format_context_, 0, file_, 0);
    }

private:
    ff_decoder_base();
    ff_decoder_base(const ff_decoder_base&);
    ff_decoder_base& operator =(const ff_decoder_base&);
    const char *file_;
    AVFormatContext *format_context_ = NULL;
    int video_stream_ = -1;
    int audio_stream_ = -1;
    AVCodecContext *video_codec_context_ = NULL;
    AVCodec *video_codec_ = NULL;
    AVCodecContext *audio_codec_context_ = NULL;
    AVCodec *audio_codec_ = NULL;
    AVFrame *original_frame_ = NULL;
    AVFrame *dest_frame_ = NULL;
    AVPacket packet_;
    int frame_finished_ = 0.0;
    int num_bytes_ = 0.0;
    uint8_t *buffer_ = NULL;
    AVDictionary *dict_ = NULL;
    struct SwsContext *sws_context_ = NULL;
    double video_time_base_ = 0.0;
    double audio_time_base_ = 0.0;
    double fps_ = 0.0;
    SwrContext *swr_context_ = NULL;
    int err_code_ = 0;
    double duration_ = 0.0;
    enum AVSampleFormat in_sample_fmt_ = AV_SAMPLE_FMT_NONE;
    enum AVSampleFormat out_sample_fmt_ = ff_decode_deafult_out_sample_format;
    int in_sample_rate_ = 0;
    int out_sample_rate_ = 0;
    uint64_t in_ch_layout_ = 0;
    uint64_t out_ch_layout_ = ff_decode_default_out_channel_layout;
    int out_channel_nb_ = 0;
    AVPixelFormat dest_vft_ = ff_decode_default_output_pixel_format;
    unsigned int dest_width_ = 0;
    unsigned int dest_height_ = 0;
    uint8_t *dest_audio_frame_buf_;
    int packet_size_ = 0;

    unsigned int audio_fss_diff_;
    unsigned int audio_fss_capacity_;
    unsigned int video_fss_diff_;
    unsigned int video_fss_capacity_;
    ff_safe_stream<int16_t> audio_fss_;
    ff_safe_stream<uint8_t> video_fss_;
};
}

#endif // FF_DECODER_BASE_H
