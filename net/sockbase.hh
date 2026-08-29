#pragma once

#include "../sys/error.hh"
#include "../io/handle.hh"
#include "../io/buf.hh"

#include <algorithm>
#include <cerrno>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <tuple>

class sock_base: public iohandle {

protected:
    void bind(auto addr) {
        auto res = addr.to_system();
        int _ = ::bind(native_handle(), (struct sockaddr *)&res, sizeof res);
        if (_ == -1) THROW_LATEST;
    }
    void listen(int backlog) {
        int _ = ::listen(native_handle(), backlog);
        if (_ == -1) THROW_LATEST; 
    }
    template <typename Addr> 
    std::tuple<int, Addr> accept(int flags) {
        typename Addr::system_type addr;
        socklen_t sz = sizeof addr;
        int fd = ::accept4(native_handle(), (struct sockaddr *)&addr, &sz, flags);
        if (fd == -1) THROW_LATEST;
        return {fd, Addr::from_system(addr)};
    }
    template <typename Addr> 
    void connect(Addr addr) {
        auto res = addr.to_system();
        int _ = ::connect(native_handle(), (struct sockaddr *)&res, sizeof res);
        // 为原型实现方便在这里hack一下
        if (_ == -1 && errno != EINPROGRESS) THROW_LATEST;
    }

    static constexpr int permitted_errors[] = {
        EAGAIN, EWOULDBLOCK, ECONNRESET, ETIMEDOUT, EINTR
    };

    template <typename T>
    ssize_t recv_nothrow(iobuf<T> buf, int flags = 0) {
        // 虽然是nothrow语义，这里只对容忍的错误放行；下同
        ssize_t n = ::recv(native_handle(), buf.base, buf.len, flags);
        if (n == -1) {
            for (int e: permitted_errors) {
                if (e == errno) return n;
            }
            THROW_LATEST;
        }
        return n;
    }

    template <typename T>
    ssize_t send_nothrow(iobuf<const T> buf, int flags = 0) {
        ssize_t n = ::send(native_handle(), buf.base, buf.len, flags);
        if (n == -1) {
            for (int e: permitted_errors) {
                if (e == errno) return n;
            }
            THROW_LATEST;
        }
        return n;
    }
    template <typename T>
    size_t recv(iobuf<T> buf, int flags = 0) {
        ssize_t _ = recv_nothrow(buf, flags);
        if (_ < 0) THROW_LATEST;
        return _;
    }
    template <typename T>
    size_t send(iobuf<const T> buf, int flags = 0) {
        ssize_t _ = send_nothrow(buf, flags);
        if (_ < 0) THROW_LATEST;
        return _;
    }

    template <typename T>
    T get_option(int level, int name) {
        T t;
        socklen_t s = sizeof t;
        int _ = getsockopt(native_handle(), level, name, &t, &s);
        if (_ == -1) THROW_LATEST;
        return t;
    }
    void set_option(int level, int name, auto value) {
        int _ = setsockopt(native_handle(), level, name, &value, sizeof value);
        if (_ == -1) THROW_LATEST;
    }

    template <typename Addr>
    Addr addr() {
        typename Addr::system_type addr;
        socklen_t sz = sizeof addr;
        int _ = getsockname(native_handle(), (struct sockaddr *) &addr, &sz);
        if (_ == -1) THROW_LATEST;
        return Addr::from_system(addr);
    }
    template <typename Addr>
    Addr peer_addr() {
        typename Addr::system_type addr;
        socklen_t sz = sizeof addr;
        int _ = getpeername(native_handle(), (struct sockaddr *) &addr, &sz);
        if (_ == -1) THROW_LATEST;
        return Addr::from_system(addr);
    }

    void shutdown(int how) {
        int _ = ::shutdown(native_handle(), how);
        if (_ == -1) THROW_LATEST;
    }

public:
    static sock_base create(int domain, int type, int proto = 0) {
        sock_base s;
        int _ = socket(domain, type, proto);
        if (_ == -1) THROW_LATEST;
        s.set_handle(_);
        return s;
    }

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
