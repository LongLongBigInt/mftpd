#pragma once

#include "../sys/error.hh"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <utility>
#include <tuple>

template <typename T>
struct iobuf {
    T *base;
    size_t len;
    iobuf(T &obj): base(&obj), len(sizeof(T)) {}
    iobuf(T *base, size_t len): base(base), len(len) {}
    iobuf(T *begin, T *end): base(begin), len((end - begin) * sizeof(T)) {}
};

class sock_base {
    int fd_;

protected:
    sock_base(int handle): fd_(handle) {};
    void bind(auto addr) {
        auto res = addr.to_system();
        int _ = ::bind(fd_, (struct sockaddr *)&res, sizeof res);
        if (_ == -1) THROW_LATEST;
    }
    void listen(int backlog) {
        int _ = ::listen(fd_, backlog);
        if (_ == -1) THROW_LATEST; 
    }
    template <typename Addr> 
    std::tuple<int, Addr> accept(int flags) {
        typename Addr::system_type addr;
        socklen_t sz = sizeof addr;
        int fd = ::accept4(fd_, (struct sockaddr *)&addr, &sz, flags);
        if (fd == -1) THROW_LATEST;
        return {fd, Addr::from_system(addr)};
    }
    template <typename Addr> 
    void connect(Addr addr) {
        auto res = addr.to_system();
        int _ = ::connect(fd_, (struct sockaddr *)&res, sizeof res);
        // 为原型实现方便在这里hack一下
        if (_ == -1 && errno != EINPROGRESS) THROW_LATEST;
    }
    template <typename T>
    size_t recv(iobuf<T> buf, int flags) {
        ssize_t _ = ::recv(fd_, buf.base, buf.len, flags);
        if (_ < 0) THROW_LATEST;
        return _;
    }
    template <typename T>
    size_t send(iobuf<const T> buf, int flags) {
        ssize_t _ = ::send(fd_, buf.base, buf.len, flags);
        if (_ < 0) THROW_LATEST;
        return _;
    }
    template <typename T>
    T get_option(int level, int name) {
        T t;
        socklen_t s;
        int _ = getsockopt(fd_, level, name, &t, &s);
        if (_ == -1) THROW_LATEST;
        return t;
    }
    void set_option(int level, int name, auto value) {
        int _ = setsockopt(fd_, level, name, &value, sizeof value);
        if (_ == -1) THROW_LATEST;
    }
    template <typename Addr>
    Addr addr() {
        typename Addr::system_type addr;
        socklen_t sz = sizeof addr;
        int _ = getsockname(fd_, (struct sockaddr *) &addr, &sz);
        if (_ == -1) THROW_LATEST;
        return Addr::from_system(addr);
    }
    template <typename Addr>
    Addr peer_addr() {
        typename Addr::system_type addr;
        socklen_t sz = sizeof addr;
        int _ = getpeername(fd_, (struct sockaddr *) &addr, &sz);
        if (_ == -1) THROW_LATEST;
        return Addr::from_system(addr);
    }

public:
    static constexpr int invalid_handle = -1;
    static sock_base create(int domain, int type, int proto = 0) {
        int _ = socket(domain, type, proto);
        if (_ == -1) THROW_LATEST;
        return {_};
    }
    sock_base(const sock_base &other) = delete;
    sock_base(sock_base &&other): fd_(other.native_handle()) {
        other.detach();
    }
    ~sock_base() {
        if (fd_ != invalid_handle) close();
    }
    sock_base &operator=(sock_base &&other) {
        if (this->fd_ == other.fd_) return *this;
        if (this->fd_ != invalid_handle) close();
        this->fd_ = other.fd_;
        other.detach();
        return *this;
    }
    bool close() {
        bool ok = ::close(fd_);
        detach();
        return ok;
    }
    void detach() { fd_ = invalid_handle; }
    int native_handle() { return fd_; }
    error_t get_error() {
        return get_option<int>(SOL_SOCKET, SO_ERROR);
    }
};

enum proto_variant {
    tcp,
    tcp_bound,
    tcp_listening,
    tcp_connected,
};

template <proto_variant P, typename Addr>
class sock;