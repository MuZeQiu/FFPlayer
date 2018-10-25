#ifndef FF_BLOCKING_AUDIO_PLAYER_H
#define FF_BLOCKING_AUDIO_PLAYER_H

#include <iostream>
#include <portaudio.h>
#include <mutex>

namespace FFPlayer {
class ff_blocking_audio_player {
public:
    static std::mutex m_;

    static ff_blocking_audio_player *audio_player_;

    static ff_blocking_audio_player* get_audio_player() {
        std::unique_lock<std::mutex> lock(m_);
        if (audio_player_) return audio_player_;
        audio_player_ = new ff_blocking_audio_player;
        return audio_player_;
    }

    inline void set_channel_nb(const int channel_nb) {
        channel_nb_ = channel_nb;
    }

    inline void set_sample_rate(const double sample_rate) {
        sample_rate_ = sample_rate;
    }

    inline void set_sample_format(const PaSampleFormat sample_format) {
        sample_format_ = sample_format;
    }

    inline void set_frames_per_buffer(const unsigned int frames_per_buffer) {
        frames_per_buffer_ = frames_per_buffer;
    }

    inline int get_channel_nb() const {
        return channel_nb_;
    }

    inline double get_sample_rate() const {
        return sample_rate_;
    }

    inline PaSampleFormat get_sample_format() const {
        return sample_format_;
    }

    inline unsigned int get_frames_per_buffer() const {
        return frames_per_buffer_;
    }

    inline bool prepare() {
        std::unique_lock<std::mutex> lock(m_);
        err_ = Pa_Initialize();
        if (errored()) {
            handle_err();
            return false;
        }
        output_parameters_.device = Pa_GetDefaultOutputDevice();
        output_parameters_.channelCount = channel_nb_;
        output_parameters_.sampleFormat = sample_format_;
        output_parameters_.suggestedLatency = Pa_GetDeviceInfo(output_parameters_.device)->defaultLowOutputLatency;
        output_parameters_.hostApiSpecificStreamInfo = NULL;
        err_ = Pa_OpenStream(
                    &stream_,
                    NULL,
                    &output_parameters_,
                    sample_rate_,
                    frames_per_buffer_,
                    paClipOff,
                    NULL,
                    NULL);
        if (errored()) {
            handle_err();
            return false;
        }
        return true;
    }

    inline bool start() {
        std::unique_lock<std::mutex> lock(m_);
        err_ = Pa_StartStream(stream_);
        if (errored()) {
            handle_err();
            return false;
        }
        return true;
    }

    inline signed long write_available() {
        std::unique_lock<std::mutex> lock(m_);
        return Pa_GetStreamWriteAvailable(stream_);
    }

    inline bool play(void *raw_data) {
        std::unique_lock<std::mutex> lock(m_);
        Pa_WriteStream(stream_, raw_data, frames_per_buffer_);
        if (err_ == paOutputUnderflowed) return true;
        if (errored()) {
            handle_err();
            return false;
        }
        return true;
    }

    inline bool stop() {
        std::unique_lock<std::mutex> lock(m_);
        err_ = Pa_StopStream(stream_);
        if (errored()) {
            handle_err();
            return false;
        }
        return true;
    }

    inline bool close() {
        std::unique_lock<std::mutex> lock(m_);
        err_ = Pa_CloseStream(stream_);
        if (errored()) {
            handle_err();
            return false;
        }
        Pa_Terminate();
        return true;
    }

private:
    inline bool errored() {
        return (err_ != paNoError);
    }

    inline void handle_err() {
        std::cout << Pa_GetErrorText(err_) << std::endl;
        Pa_Terminate();
    }

private:
    ff_blocking_audio_player() {}
    ~ff_blocking_audio_player() {}

private:
    PaError err_ = 0;
    PaStream *stream_ = NULL;
    int channel_nb_ = 0;
    double sample_rate_ = 0.0;
    PaSampleFormat sample_format_ = 0.0;
    unsigned int frames_per_buffer_ = 0;
    PaStreamParameters output_parameters_;
};
}

#endif // FF_BLOCKING_AUDIO_PLAYER_H
