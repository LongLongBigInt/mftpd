#pragma once

#include <system_error>
#include <cerrno>

#define THROW_SYSTEM(x) \
throw std::system_error(x, std::system_category(), __func__)

#define THROW_LATEST THROW_SYSTEM(errno)

class errno_guard {
    int errno_;
public:
    errno_guard() { errno_ = errno; errno = 0; }
    ~errno_guard() { errno = errno_; }
};
