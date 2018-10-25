#ifndef FF_DATA_SIZE_H
#define FF_DATA_SIZE_H

#include <assert.h>
#include <iostream>
extern "C" {
#include <libavformat/avformat.h>
}

namespace FFPlayer {
class ff_data_size {
public:
    static unsigned int get_video_buffer_size(unsigned int width,
                                              unsigned int height,
                                              AVPixelFormat pixel_format,
                                              unsigned int capacity) {
        assert(width);
        assert(height);
        assert(capacity);
        assert(pixel_format != AV_PIX_FMT_NONE);
        unsigned int actual_bs = 0;
        unsigned int bs = 2;
        if (pixel_format == AV_PIX_FMT_RGB24) {
            actual_bs = \
                    width*height*3*capacity;
        } else if (pixel_format == AV_PIX_FMT_YUV420P) {
            actual_bs = \
                    width*height*3*capacity/2;
        } else {
            std::cout << "This pixel format isn't supported." << std::endl;
            assert(false);
        }
        while(true) {
            if (bs >= actual_bs) return bs;
            bs *= 2;
        }
        return bs;
    }

    static unsigned int get_audio_buffer_size(unsigned int sample_rate,
                                              uint64_t channel_layout,
                                              enum AVSampleFormat sample_format,
                                              unsigned int capacity) {
        assert(sample_rate);
        assert(capacity);
        assert(sample_format != AV_SAMPLE_FMT_NONE);

        unsigned int sample_format_bytes_nb = 0;
        if (sample_format == AV_SAMPLE_FMT_U8) sample_format_bytes_nb = 1;
        else if (sample_format == AV_SAMPLE_FMT_S16) sample_format_bytes_nb = 2;
        else if (sample_format == AV_SAMPLE_FMT_S32) sample_format_bytes_nb = 4;
        else if (sample_format == AV_SAMPLE_FMT_FLT) sample_format_bytes_nb = 4;
        else {
            std::cout << "This sample format isn't supported." << std::endl;
            assert(false);
        }
        unsigned int channel_nb = 0;
        if (channel_layout == AV_CH_LAYOUT_MONO) channel_nb = 1;
        else if (channel_layout == AV_CH_LAYOUT_STEREO) channel_nb = 2;
        else {
            std::cout << "This channel layout isn't supported." << std::endl;
            assert(false);
        }
        unsigned int actual_bs = sample_rate*channel_nb*sample_format_bytes_nb*capacity;
        unsigned int bs = 2;
        while(true) {
            if (bs >= actual_bs) return bs;
            bs *= 2;
        }
        return actual_bs;
    }
};

}


#endif // FF_DATA_SIZE_H
