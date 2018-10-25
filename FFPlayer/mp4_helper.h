#ifndef MP4_HELPER_H
#define MP4_HELPER_H

#include <assert.h>
#include <iostream>
#include <mp4v2/mp4v2.h>
#include <stdarg.h>

namespace FFPlayer {
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
}



#endif // MP4_HELPER_H
