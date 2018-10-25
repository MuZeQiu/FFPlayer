#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <atomic>
#include <mutex>
#include <boost/stacktrace.hpp>
#include <boost/filesystem.hpp>

namespace FFPlayer {
class error_handler
{
public:
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
private:
    static void signal_handler(int signum) {
        ::signal(signum, SIG_DFL);
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
}


#endif // ERROR_HANDLER_H
