#include "mainwindow.h"
#include <QApplication>
#include <QDesktopWidget>
#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

#include <iostream>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <deque>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <chrono>
#include <tuple>
#include <functional>
#include <numeric>
#include <map>
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>

#include <boost/stacktrace.hpp>
#include <boost/filesystem.hpp>

#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include <portaudio.h>
#include <mp4v2/mp4v2.h>

#define Enable_Stack_Trace
//#define Test_Audio

static constexpr enum AVSampleFormat ff_decode_deafult_out_sample_format = AV_SAMPLE_FMT_S16;
static constexpr AVPixelFormat ff_decode_default_output_pixel_format = AV_PIX_FMT_RGB24;
static constexpr uint64_t ff_decode_default_out_channel_layout = AV_CH_LAYOUT_MONO;
static constexpr PaSampleFormat ff_audio_default_output_sample_format = paInt16;
static constexpr unsigned int ff_audio_default_output_channel_nb = 1;
static constexpr unsigned int ff_audio_deafult_output_frames_per_buffer_nb = 512;
static constexpr unsigned int ff_audio_default_output_bytes_nb = 2*ff_audio_deafult_output_frames_per_buffer_nb;
static constexpr uint16_t ff_audio_default_output_fake_data[ff_audio_deafult_output_frames_per_buffer_nb] = {0};
static constexpr unsigned int ff_player_queue_default_max_size = 50;
static constexpr unsigned int ff_player_task_pool_default_max_size = 50;
static constexpr unsigned int ff_player_timer_default_loop_microseconds = 5;

static inline double calculate_pcm_duration(double sample_rate,
                                            double channel_nb,
                                            double sample_format_bytes_nb,
                                            double pcm_bytes_len) {
    return pcm_bytes_len/(sample_rate*channel_nb*sample_format_bytes_nb);
}

struct error_handler {
    static error_handler * create_error_handler(const char *stack_trace_file = "./backtrace.dump") {
        std::unique_lock<std::mutex> lock(m_);
        if (!eh_) {
            eh_ = new error_handler;
            stack_trace_file_ = stack_trace_file;
            eh_->register_signal();
            eh_->check_backtrace();
            return eh_;
        }
        return eh_;
    }
    static const char *get_stack_trace_file() {
        std::unique_lock<std::mutex> lock(m_);
        return stack_trace_file_;
    }
    static void print_stack_trace() {
    #ifdef Enable_Stack_Trace
        std::cout << boost::stacktrace::stacktrace() << std::endl;
    #endif
    }

    static void get_func_address() {
        printf("get_func_address:     %p\n",&error_handler::get_func_address);
        printf("create_error_handler: %p\n",&error_handler::create_error_handler);
        printf("get_stack_trace_file: %p\n",&error_handler::get_stack_trace_file);
        printf("print_stack_trace:    %p\n",&error_handler::print_stack_trace);
        printf("signal_handler:       %p\n",&error_handler::signal_handler);
        printf("register_signal:      %p\n",&error_handler::register_signal);
        printf("check_backtrace:      %p\n",&error_handler::check_backtrace);
    }

private:
    static void signal_handler(int signum) {
        ::signal(signum, SIG_DFL);
        //boost::stacktrace::safe_dump_to(stack_trace_file_);
        boost::stacktrace::safe_dump_to(0, boost::stacktrace::detail::max_frames_dump, stack_trace_file_);
        ::raise(SIGABRT);
    }
    void register_signal() {
        ::signal(SIGSEGV, &error_handler::signal_handler);
        ::signal(SIGABRT, &error_handler::signal_handler);
        ::signal(SIGFPE , &error_handler::signal_handler);
        ::signal(SIGILL , &error_handler::signal_handler);
        ::signal(SIGPIPE, &error_handler::signal_handler);
        ::signal(SIGBUS , &error_handler::signal_handler);
    }
    void check_backtrace() {
        if (boost::filesystem::exists(stack_trace_file_)) {
            std::ifstream ifs(stack_trace_file_);
            boost::stacktrace::stacktrace st = boost::stacktrace::stacktrace::from_dump(ifs);
            std::cout << "Previous run crashed:\n" << st << std::endl;
            ifs.close();
            boost::filesystem::remove(stack_trace_file_);
        }
    }
private:
    error_handler() {}
    ~error_handler() {}
    static error_handler *eh_;
    static std::mutex m_;
    static const char *stack_trace_file_;
};

class mp4_helper {
public:
    explicit mp4_helper(const char *file):
        file_(file) {
        assert(file_);
        mfh_ = MP4Read(file_);
    }
    const char *get_file() {
        return MP4GetFilename(mfh_);
    }
    void set_log_callback(void (*log_callback)(MP4LogLevel loglevel,
                                               const char* fmt,
                                               va_list ap)) {
        log_callback_ = log_callback;
    }
    void enable_log() {
        MP4LogSetLevel(MP4_LOG_VERBOSE4);
        if (log_callback_) MP4SetLogCallback(log_callback_);
        else
            MP4SetLogCallback([](MP4LogLevel loglevel,
                              const char* fmt,
                              va_list     ap){
                char buf[1024];
                int ret = vsprintf(buf, fmt, ap);
                std::cout << buf << std::endl;
            });
    }
    void disable_log() {
        MP4SetLogCallback(NULL);
    }
    void get_info(std::string &info) {
        char *fi = MP4FileInfo(file_);
        if (fi) {
            info.append(fi);
            free(fi);
        }
    }
    void dump() {
        MP4Dump(mfh_, true);
    }
    double get_duration() {
        MP4Duration duration = MP4GetDuration(mfh_);
        uint32_t ts = MP4GetTimeScale(mfh_);
        assert(ts != 0);
        return (double)duration/(double)ts;
    }
    bool optimize() {
        return MP4Optimize(file_);
    }
    ~mp4_helper(){}
private:
    const char *file_;
    MP4FileHandle mfh_;
    void (*log_callback_)(MP4LogLevel loglevel,
                         const char* fmt,
                         va_list ap);
};

error_handler *error_handler::eh_ = nullptr;
const char *error_handler::stack_trace_file_ = "";
std::mutex error_handler::m_;

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

template<typename T>
class ff_queue_base {
public:
    ff_queue_base(const ff_queue_base&) = delete;
    ff_queue_base& operator=(const ff_queue_base&) = delete;

    explicit ff_queue_base(unsigned int max_size):
        max_size_(max_size) {
        assert(max_size != 0);
    }

    ~ff_queue_base() {}

    inline virtual unsigned int get_max_size() {
        return max_size_;
    }

    inline virtual unsigned int get_size() {
        return queue_.size();
    }

    inline virtual bool get_empty() {
        return queue_.empty();
    }

    inline virtual bool enqueue(T&& t) {
        if(max_size_ <= queue_.size()) {
            return false;
        }
        queue_.push_back(t);
        return true;
    }

    inline virtual bool enqueue(T& t) {
        if(max_size_ <= queue_.size()) {
            return false;
        }
        queue_.push_back(t);
        return true;
    }

    inline virtual bool dequeue(T& t) {
        if (queue_.empty()) {
            return false;
        }
        t = queue_.front();
        queue_.pop_front();
        return true;
    }

    inline virtual bool popqueue(T& t) {
        if (queue_.empty()) {
            return false;
        }
        t = queue_.back();
        queue_.pop_back();
        return true;
    }

    inline virtual bool enpacket(std::deque<T>& que) {
        if (que.size()+queue_.size() >= max_size_) {
            return false;
        }
        for (int i = 0; i < que.size(); i++) {
            T t = que.at(i);
            queue_.push_back(t);
        }
        return true;
    }

    inline virtual void clear() {
        queue_.clear();
    }

    inline virtual void reset(unsigned int max_size) {
        queue_.clear();
        max_size_ = max_size;
    }

    inline virtual void sort(std::function<bool(const T&, const T&)> compare) {
        std::sort(queue_.begin(), queue_.end(), compare);
    }

    virtual bool enpacket_with_sort(std::deque<T>& que,
                            std::function<bool(const T&, const T&)> compare) {
        if (que.size()+queue_.size() >= max_size_) {
            return false;
        }
        for (int i = 0; i < que.size(); i++) {
            T t = que.at(i);
            queue_.push_back(t);
        }
        std::sort(queue_.begin(), queue_.end(), compare);
        return true;
    }

private:
    ff_queue_base();
    unsigned int max_size_;
    std::deque<T> queue_;
};

template<typename T>
class ff_safe_queue: public ff_queue_base<T> {
public:
    explicit ff_safe_queue(unsigned int max_size):
        ff_queue_base<T>(max_size),
        canceled_(false) {}

    ~ff_safe_queue() {}

    virtual unsigned int get_max_size() {
        std::unique_lock<std::mutex> lock(m_);
        return ff_queue_base<T>::get_max_size();
    }

    virtual unsigned int get_size() override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_queue_base<T>::get_size();
    }

    virtual bool get_empty() override {
        std::unique_lock<std::mutex> lock(m_);
        return ff_queue_base<T>::get_empty();
    }

    virtual bool enqueue(T&& t) override {
        std::unique_lock<std::mutex> lock(m_);
        while(!canceled_.load() && !ff_queue_base<T>::enqueue(t)) {
            //std::cout << "safe queue enqueue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    virtual bool enqueue(T& t) override {
        std::unique_lock<std::mutex> lock(m_);
        while(!canceled_.load() && !ff_queue_base<T>::enqueue(t)) {
            //std::cout << "safe queue enqueue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    virtual bool dequeue(T& t) override {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::dequeue(t)) {
            //std::cout << "safe queue dequeue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    inline virtual bool popqueue(T& t) {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::popqueue(t)) {
            //std::cout << "safe queue popqueue in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    virtual bool enpacket(std::deque<T>& que) override {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::enpacket(que)) {
            //std::cout << "safe queue enpacket in blocking: " << ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        cv_.notify_all();
        return !canceled_.load();
    }

    inline bool is_canceled() {
        std::unique_lock<std::mutex> lock(m_);
        return canceled_.load();
    }

    void cancel() {
        std::unique_lock<std::mutex> lock(m_);
        canceled_.store(true);
        cv_.notify_all();
    }

    virtual void clear() override {
        std::unique_lock<std::mutex> lock(m_);
        ff_queue_base<T>::clear();
        cv_.notify_all();
    }

    virtual void reset(unsigned int max_size) override {
        std::unique_lock<std::mutex> lock(m_);
        ff_queue_base<T>::reset(max_size);
        canceled_.store(false);
        cv_.notify_all();
    }

    virtual void sort(std::function<bool(const T&, const T&)> compare) override {
        std::unique_lock<std::mutex> lock(m_);
        ff_queue_base<T>::sort(compare);
    }

    virtual bool enpacket_with_sort(std::deque<T>& que,
                                    std::function<bool(const T&, const T&)> compare) override {
        std::unique_lock<std::mutex> lock(m_);
        while (!canceled_.load() && !ff_queue_base<T>::enpacket(que)) {
            //std::cout << "safe queue enpacket_with_sort in blocking: " <<\
                      ff_queue_base<T>::get_size() << std::endl;
            cv_.wait(lock);
        }
        ff_queue_base<T>::sort(compare);
        cv_.notify_all();
        return !canceled_.load();
    }

    static void get_func_address() {
        printf("get_max_size:     %p\n",&ff_safe_queue::get_max_size);
        printf("get_size: %p\n",&ff_safe_queue::get_size);
        printf("get_empty: %p\n",&ff_safe_queue::get_empty);
        printf("enqueue:    %p\n",&ff_safe_queue::enqueue);
        printf("dequeue:       %p\n",&ff_safe_queue::dequeue);
        printf("popqueue:      %p\n",&ff_safe_queue::popqueue);
        printf("enpacket:      %p\n",&ff_safe_queue::enpacket);
        printf("is_canceled:      %p\n",&ff_safe_queue::is_canceled);
        printf("cancel:      %p\n",&ff_safe_queue::cancel);
        printf("clear:      %p\n",&ff_safe_queue::clear);
        printf("reset:      %p\n",&ff_safe_queue::reset);
        printf("sort:      %p\n",&ff_safe_queue::sort);
        printf("enpacket_with_sort:      %p\n",&ff_safe_queue::enpacket_with_sort);
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool canceled_;
};

class task_pool_sync {
public:
    explicit task_pool_sync(unsigned int max_size):
        started_(false),
        queue_(max_size) {}

    inline void start() {
        std::unique_lock<std::mutex> lock(m_);
        if (!started_.load()) {
            started_.store(true);
            std::thread t([this](){
                std::function<void(void)> task;
                while(started_.load()) {
                    if (queue_.dequeue(task)) task();
                    std::this_thread::sleep_for(std::chrono::microseconds(5));
                }
                if (closing_cb_) closing_cb_(this);
            });
            t.detach();
        }
    }

    inline bool add_task(std::function<void(void)>&& task) {
        std::unique_lock<std::mutex> lock(m_);
        if (started_.load()) {
            return queue_.enqueue(task);
        }
        return false;
    }

    inline bool add_task(std::function<void(void)>& task) {
        std::unique_lock<std::mutex> lock(m_);
        if (started_.load()) {
            return queue_.enqueue(task);
        }
        return false;
    }

    inline void close(std::function<void(void*)>&& closing_cb) {
        std::unique_lock<std::mutex> lock(m_);
        closing_cb_ = closing_cb;
        started_.store(false);
    }

    inline void close(std::function<void(void*)>& closing_cb) {
        std::unique_lock<std::mutex> lock(m_);
        closing_cb_ = closing_cb;
        started_.store(false);
    }

    inline void clear() {
        std::unique_lock<std::mutex> lock(m_);
        queue_.clear();
    }

    inline void reset() {
        std::unique_lock<std::mutex> lock(m_);
        queue_.clear();
        started_.store(false);
    }

    inline bool idle() {
        std::unique_lock<std::mutex> lock(m_);
        return started_.load() && queue_.get_empty();
    }

    inline unsigned int size() {
        std::unique_lock<std::mutex> lock(m_);
        return queue_.get_size();
    }

    inline bool is_started() {
        std::unique_lock<std::mutex> lock(m_);
        return started_.load();
    }

    ~task_pool_sync() {}

private:
    task_pool_sync();
    task_pool_sync(const task_pool_sync&);
    task_pool_sync& operator =(const task_pool_sync&);
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool started_;
    ff_queue_base<std::function<void(void)>> queue_;
    std::function<void(void*)> closing_cb_;
};

class ff_pixel_format_transformer {
public:
    static void rgb888_to_qig(const uint8_t *rgb888,
                              const unsigned int width,
                              const unsigned int height,
                              QPixmap &qmp) {
        qmp = QPixmap::fromImage(QImage(rgb888, width, height, QImage::Format_RGB888));
    }
};

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
                                       /*AVSEEK_FLAG_BACKWARD*/AVSEEK_FLAG_FRAME);
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
                                       /*AVSEEK_FLAG_BACKWARD*/AVSEEK_FLAG_FRAME);
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
#ifdef Test_Audio
            static bool is_open = false;
            if (!is_open) {
                is_open = true;
                fs_.open("/home/muze/文档/qtpro/file1.pcm", std::ios_base::out|std::ios_base::binary|std::ios_base::trunc);
                if (fs_.is_open()) {
                    //assert(false);
                }
            }
            fs_.write((const char*)dest_audio_frame_buf_, out_buffer_size);
            fs_.flush();
#endif
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
#ifdef Test_Audio
    std::fstream fs_;
#endif
};

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

    bool is_canceled() {
        return cancel_.load();
    }

    static void get_func_address() {
        printf("start:     %p\n",&ff_asyn_decoder::start);
        printf("asyn_decode: %p\n",&ff_asyn_decoder::asyn_decode);
        printf("clear_buffer: %p\n",&ff_asyn_decoder::clear_buffer);
        printf("cancel:    %p\n",&ff_asyn_decoder::cancel);
        printf("reset:       %p\n",&ff_asyn_decoder::reset);
        printf("set_seek_cb:      %p\n",&ff_asyn_decoder::set_seek_cb);
        printf("set_end_decode_cb:      %p\n",&ff_asyn_decoder::set_end_decode_cb);
        printf("is_end:      %p\n",&ff_asyn_decoder::is_end);
        printf("is_canceled:      %p\n",&ff_asyn_decoder::is_canceled);
    }

private:
    std::thread dec_thr_;
    std::shared_ptr<ff_safe_queue<ff_decoder_base::frame_args>>& av_queue_;
    std::atomic_bool cancel_;
    std::function<bool()> seek_cb_;
    std::function<void()> end_decode_cb_;
    std::atomic_bool ended_;
};

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

    static void get_func_address() {
        printf("start:     %p\n",&ff_asyn_timer::start);
        printf("get_interval: %p\n",&ff_asyn_timer::get_interval);
        printf("cancel: %p\n",&ff_asyn_timer::cancel);
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

class ff_player_event {
public:
    virtual void player_pause() {};
    virtual void player_start() {};
    virtual void player_next(const char*next_file = "/home/muze/下载/4.mp4") {};
    virtual void player_last(const char*last_file = "/home/muze/下载/剑网3·侠肝义胆沈剑心-01.mp4") {};
    virtual void player_slide(int) {};
    virtual void slider_release() {};
    virtual void player_resize() {};
    virtual void player_close() {};
};

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

class ff_player;
class ff_player_face: private QLabel {
    friend ff_player;
public:
    explicit ff_player_face(ff_player_base *fpb):
        QLabel(Q_NULLPTR, Qt::Window),
        fpb_(fpb),
        app_(fpb->get_app()),
        screen_width_(app_.desktop()->availableGeometry().width()),
        screen_height_(app_.desktop()->availableGeometry().height()),
        dest_width_(fpb->get_dest_width()),
        dest_height_(fpb->get_dest_height()+100),
        content_face_(this, 0, 0, dest_width_, dest_height_-100),
        player_but_(this, (dest_width_-60)/2, dest_height_-70, 60, 60),
        player_slider_(this, 0, dest_height_-100, dest_width_, 20),
        player_next_but_(this, (dest_width_-60)/2+90, dest_height_-70, 60, 60),
        player_last_but_(this, (dest_width_-60)/2-90, dest_height_-70, 60, 60) {
        assert(dest_width_ != screen_width_);
        assert(dest_height_ != screen_height_);
        resize(dest_width_, dest_height_);
        set_action();
        show();
    }

    ~ff_player_face(){}
    void set_action() {
        content_face_.dbl_click_cb_ = [this](QWidget *widget){
            if (player_but_.text() == "Start") {
                fpb_->player_start();
                player_but_.setText("Pause");
            } else {
                fpb_->player_pause();
                player_but_.setText("Start");
            }
        };
        player_but_.press_cb_ = [this](QWidget *widget){
            if (player_but_.text() == "Start") {
                fpb_->player_start();
                player_but_.setText("Pause");
            } else {
                fpb_->player_pause();
                player_but_.setText("Start");
            }
        };
        player_but_.double_press_cb_ = [this](QWidget *widget){
            if (player_but_.text() == "Start") {
                fpb_->player_start();
                player_but_.setText("Pause");
            } else {
                fpb_->player_pause();
                player_but_.setText("Start");
            }
        };
        player_slider_.value_change_cb_ = [this](QWidget *widget, int pos){
            fpb_->player_slide(pos);
        };
        player_slider_.slider_release_cb_ = [this](QWidget *widget) {
            fpb_->slider_release();
        };
        player_next_but_.press_cb_ = [this](QWidget *widget){
            fpb_->player_next();
        };
        player_next_but_.double_press_cb_ = [this](QWidget *widget){
            fpb_->player_next();
        };
        player_last_but_.press_cb_ = [this](QWidget *widget){
            fpb_->player_last();
        };
        player_last_but_.double_press_cb_ = [this](QWidget *widget){
            fpb_->player_last();
        };
    }

protected:
      bool event(QEvent *e) override {
        if (e->type() == QEvent::Type::Resize) {
            fpb_->player_resize();
            dest_width_ = width();
            dest_height_ = height();
            content_face_.setGeometry(0, 0, dest_width_, dest_height_-100);
            player_but_.setGeometry((dest_width_-60)/2, dest_height_-70, 60, 60);
            player_slider_.setGeometry(0, dest_height_-100, dest_width_, 20);
            player_next_but_.setGeometry((dest_width_-60)/2+90, dest_height_-70, 60, 60);
            player_last_but_.setGeometry((dest_width_-60)/2-90, dest_height_-70, 60, 60);
        }
        if (e->type() == QEvent::Type::Close) {
            fpb_->player_close();
            exit(0);
        }
        return QLabel::event(e);
    }

private:
    class ff_player_content_face: public QLabel {
    public:
        friend ff_player_face;
        ff_player_content_face(QWidget *parent,
                               unsigned int x,
                               unsigned int y,
                               unsigned int width,
                               unsigned int height):
            QLabel(parent) {
            assert(parent);
            setGeometry(x, y, width, height);
            setScaledContents(true);
        }
        ~ff_player_content_face(){};
        void set_style_sheet(const unsigned int width,\
                             const std::tuple<unsigned int, unsigned int, unsigned int>& rgb,\
                             char* border_style = "solid") {
            assert(std::get<0>(rgb)<256&&std::get<1>(rgb)<256&&std::get<2>(rgb)<256);
            QString style_sheet = "border-width:";
            style_sheet += QString::number(width);
            style_sheet += QString("px;");
            style_sheet += QString("border-color:rgb");
            style_sheet += QString("(")+QString::number(std::get<0>(rgb))+QString(",")+\
                    QString::number(std::get<1>(rgb))+QString(",")+\
                    QString::number(std::get<2>(rgb))+QString(");");
            style_sheet += QString("border-style:") + QString(border_style) + QString(";");
            this->setStyleSheet(style_sheet);
        }
    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(dbl_click_cb_);
                dbl_click_cb_(this);
            }
            return QLabel::event(e);
        }
    private:
        std::function<void (QWidget *)> dbl_click_cb_;
    };
    class ff_player_but: public QPushButton {
    public:
        friend ff_player_face;
        ff_player_but(QWidget *parent,
                               unsigned int x,
                               unsigned int y,
                               unsigned int width,
                               unsigned int height):QPushButton(parent) {
            assert(parent);
            this->setGeometry(170, 330, 60, 60);
            this->setText("Pause");
        }
        ~ff_player_but(){};

    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonPress) {
                assert(press_cb_);
                press_cb_(this);
            }
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(double_press_cb_);
                double_press_cb_(this);
            }
            return QPushButton::event(e);
        }
    private:
        std::function<void (QWidget *)> press_cb_;
        std::function<void (QWidget *)> double_press_cb_;
    };
    class ff_player_next_but: public QPushButton {
    public:
        friend ff_player_face;
        ff_player_next_but(QWidget *parent,
                           unsigned int x,
                           unsigned int y,
                           unsigned int width,
                           unsigned int height):QPushButton(parent) {
            assert(parent);
            this->setGeometry(x, y, width, height);
            this->setText("Next");
        }
        ~ff_player_next_but(){};

    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonPress) {
                assert(press_cb_);
                press_cb_(this);
            }
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(double_press_cb_);
                double_press_cb_(this);
            }
            return QPushButton::event(e);
        }
    private:
        std::function<void (QWidget *)> press_cb_;
        std::function<void (QWidget *)> double_press_cb_;
    };
    class ff_player_last_but: public QPushButton {
    public:
        friend ff_player_face;
        ff_player_last_but(QWidget *parent,
                           unsigned int x,
                           unsigned int y,
                           unsigned int width,
                           unsigned int height):QPushButton(parent) {
            assert(parent);
            setGeometry(x, y, width, height);
            setText("Last");

        }
        ~ff_player_last_but(){};

    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonPress) {
                assert(press_cb_);
                press_cb_(this);
            }
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(double_press_cb_);
                double_press_cb_(this);
            }
            return QPushButton::event(e);
        }
    private:
        std::function<void (QWidget *)> press_cb_;
        std::function<void (QWidget *)> double_press_cb_;
    };
    class ff_player_slider: public QSlider {
        friend ff_player_face;

    public:
        ff_player_slider(QWidget *parent,
                         unsigned int x,
                         unsigned int y,
                         unsigned int width,
                         unsigned int height):
            QSlider(Qt::Orientation::Horizontal, parent){
            assert(parent);
            setGeometry(x, y, width, height);
            setMinimum(1);
            setMaximum(100);
            setSliderPosition(1);
        }
        ~ff_player_slider(){}

    protected:
        bool event(QEvent *e) {
            if (e->type() == QEvent::MouseButtonRelease) {
                if (slider_release_cb_) slider_release_cb_(this);
            }
            return QSlider::event(e);
        }

        virtual void sliderChange(SliderChange change) override {
            if (change == SliderChange::SliderValueChange) {
                if (isSliderDown()) {
                    if (value_change_cb_) value_change_cb_(this, sliderPosition());
                }
            }
            QSlider::sliderChange(change);
        }
    private:
        std::function<void (QWidget *, int)> value_change_cb_;
        std::function<void (QWidget *)> slider_release_cb_;
    };
    void get_screen_size() {
        QDesktopWidget* desktop_widget = QApplication::desktop();
        QRect desk_rect = QApplication::desktop()->availableGeometry();
        screen_width_ = desk_rect.width();
        screen_height_ = desk_rect.height();
    }

    ff_player_base *fpb_;
    QApplication& app_;
    ff_player_content_face content_face_;
    ff_player_but player_but_;
    ff_player_next_but player_next_but_;
    ff_player_last_but player_last_but_;
    ff_player_slider player_slider_;
    unsigned int screen_width_;
    unsigned int screen_height_;
    unsigned int dest_width_;
    unsigned int dest_height_;
};


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

std::mutex ff_blocking_audio_player::m_;
ff_blocking_audio_player *ff_blocking_audio_player::audio_player_ = NULL;

class ff_nonblocking_audio_player
{
public:
    typedef int (*play_cb_func)(const void *,
                                void *,
                                unsigned long,
                                const PaStreamCallbackTimeInfo*,
                                PaStreamCallbackFlags,
                                void *);
    typedef void (*play_finished_cb_func)(void *);
private:
    static std::mutex m_;
public:
    static ff_nonblocking_audio_player *audio_player_;
    static ff_nonblocking_audio_player* get_audio_player() {
        std::unique_lock<std::mutex> lock(m_);
        if (audio_player_) return audio_player_;
        audio_player_ = new ff_nonblocking_audio_player;
        return audio_player_;
    }

    int get_channel_nb() const {
        return channel_nb_;
    }
    double get_sample_rate() const {
        return sample_rate_;
    }
    PaSampleFormat get_sample_format() const {
        return sample_format_;
    }
    unsigned int get_frames_per_buffer() const {
        return frames_per_buffer_;
    }
    void *get_raw_data() const {
        return raw_data_;
    }
    play_cb_func get_play_cb() const {
        return play_cb_;
    }
    play_finished_cb_func get_play_finished_cb() const {
        return play_finished_cb_;
    }

    void set_channel_nb(const int channel_nb) {
        channel_nb_ = channel_nb;
    }
    void set_sample_rate(const double sample_rate) {
        sample_rate_ = sample_rate;
    }
    void set_sample_format(const PaSampleFormat sample_format) {
        sample_format_ = sample_format;
    }
    void set_frames_per_buffer(const unsigned int frames_per_buffer) {
        frames_per_buffer_ = frames_per_buffer;
    }
    void set_raw_data(void *raw_data) {
        raw_data_ = raw_data;
    }
    void set_play_cb(play_cb_func play_cb) {
        play_cb_ = play_cb;
    }
    void set_play_finished_cb(play_finished_cb_func play_finished_cb) {
        play_finished_cb_ = play_finished_cb;
    }

    bool prepare() {
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
                    play_cb_,
                    raw_data_);
        if (errored()) {
            handle_err();
            return false;
        }

        if (play_finished_cb_) {
            err_ = Pa_SetStreamFinishedCallback(stream_, play_finished_cb_);
            if (errored()) {
                handle_err();
                return false;
            }
        }

        return true;
    }

    bool play() {
        err_ = Pa_StartStream(stream_);
        if (errored()) {
            handle_err();
            return false;
        }
        return true;
    }

    bool stop() {
        err_ = Pa_StopStream(stream_);
        if (errored()) {
            handle_err();
            return false;
        }
        return true;
    }

    bool abort() {
        err_ = Pa_AbortStream(stream_);
        if (errored()) {
            handle_err();
            return false;
        }
        return true;
    }

    enum Stopping_Status {
        Stopping_Status_Error = 2,
        Stopping_Status_Stopped = 4,
        Stopping_Status_Running = 8
    };

    enum Stopping_Status stopped() {
        err_ = Pa_IsStreamStopped(stream_);
        if (err_ < 0) {
            handle_err();
            return Stopping_Status_Error;
        }
        if (err_ == 0) {
            return Stopping_Status_Running;
        }
        return Stopping_Status_Stopped;
    }

    enum Active_Status {
        Active_Status_Error = 16,
        Active_Status_Active = 32,
        Active_Status_Unplay = 64
    };

    enum Active_Status actived() {
        err_ = Pa_IsStreamActive(stream_);
        if (err_ < 0) {
            handle_err();
            return Active_Status_Error;
        }
        if (err_ == 0) {
            return Active_Status_Unplay;
        }
        return Active_Status_Active;
    }

    bool close() {
        err_ = Pa_CloseStream(stream_);
        if (errored()) {
            handle_err();
            return false;
        }
        Pa_Terminate();
        return true;
    }

private:
    bool errored() {
        return (err_ != paNoError);
    }

    void handle_err() {
        Pa_Terminate();
    }

private:
    ff_nonblocking_audio_player() {}
    ~ff_nonblocking_audio_player() {}

private:
    PaError err_ = 0;
    PaStream *stream_ = NULL;
    int channel_nb_ = 0;
    double sample_rate_ = 0.0;
    PaSampleFormat sample_format_ = 0.0;
    unsigned int frames_per_buffer_ = 0;
    void *raw_data_ = NULL;
    PaStreamParameters output_parameters_;
    static play_cb_func play_cb_;
    static play_finished_cb_func play_finished_cb_;

};

std::mutex ff_nonblocking_audio_player::m_;
ff_nonblocking_audio_player *ff_nonblocking_audio_player::audio_player_ = NULL;
ff_nonblocking_audio_player::play_cb_func ff_nonblocking_audio_player::play_cb_ = NULL;
ff_nonblocking_audio_player::play_finished_cb_func ff_nonblocking_audio_player::play_finished_cb_ = NULL;

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
               true,[this](void *arg){return timer_task(arg);}
               /*[this](void *arg) {
        timer_interval_.fetch_add(ff_player_timer_default_loop_microseconds);
        if (tem_fa_.ft == ff_decoder_base::Unknow_Frame) queue_->dequeue(tem_fa_);
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
        return (void*)0;}*/
    ),
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
                if (decoder_.seek_video(decoder_.get_duration()/(double)(face_.player_slider_.maximum())\
                                        *(double)seek_pos_.load())) {
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
    };
    virtual void player_start() override {
        timer_.start();
    };
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
    };
    virtual void player_slide(int pos) override {
        seek_pos_.store(pos);
    };
    virtual void slider_release() override {
        seek_starting_.store(true);
    };
    virtual void player_resize() override {
        resized_.store(true);
    };
    virtual void player_close() override {};

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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    error_handler::create_error_handler();
    std::cout << std::this_thread::get_id() << std::endl;
    ff_player player(app, "/home/muze/下载/X战警：天启.BD1280高清中英双字版.mp4");
    player.play();
    return app.exec();
}
