#pragma once

#include <unistd.h>

// 消除循环依赖error
template <typename E>
class _Iohandle {
    int fd_;
    E *evloop_ = nullptr;

    void assign(_Iohandle &other) {
        fd_ = other.fd_;
        evloop_ = other.evloop_;
        other.detach();
    }

protected:
    void set_handle(int handle) {
        fd_ = handle;
    }

public:
    static constexpr int invalid_handle = -1;
    _Iohandle(): fd_(invalid_handle) {}
    _Iohandle(int handle): fd_(handle) {};
    _Iohandle(_Iohandle &&other) {
        assign(other);
    }
    ~_Iohandle() { if (valid()) close(); }
    _Iohandle &operator=(_Iohandle &&other) {
        if (fd_ == other.fd_) return *this;
        if (valid()) close();
        assign(other);
        return *this;
    }
    bool valid() { return fd_ != invalid_handle; }
    bool close() {
        bool ok = ::close(fd_);
        detach();
        if (ok && evloop_) evloop_->dec();
        return ok;
    }
    void detach() { fd_ = invalid_handle; }
    int native_handle() { return fd_; }
    void set_evloop(E *evloop) { evloop_ = evloop; }
    E *evloop() { return evloop_; }
};

class epoll;

using iohandle = _Iohandle<epoll>;
