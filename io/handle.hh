#pragma once

#include <unistd.h>

class iohandle {
    int fd_;
protected:
    void set_handle(int handle) {
        fd_ = handle;
    }

public:
    static constexpr int invalid_handle = -1;
    iohandle(): fd_(invalid_handle) {}
    iohandle(int handle): fd_(handle) {};
    iohandle(iohandle &&other): fd_(other.native_handle()) {
        other.detach();
    }
    ~iohandle() { if (valid()) close(); }
    iohandle &operator=(iohandle &&other) {
        if (fd_ == other.fd_) return *this;
        if (valid()) close();
        fd_ = other.fd_;
        other.detach();
        return *this;
    }
    bool valid() { return fd_ != invalid_handle; }
    bool close() {
        bool ok = ::close(fd_);
        detach();
        return ok;
    }
    void detach() { fd_ = invalid_handle; }
    int native_handle() { return fd_; }
};
