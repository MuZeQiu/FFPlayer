#include "error_handler.h"

namespace FFPlayer {
error_handler *error_handler::eh_ = nullptr;
std::mutex error_handler::m_;
const char *error_handler::stack_trace_file_ = "";
}

