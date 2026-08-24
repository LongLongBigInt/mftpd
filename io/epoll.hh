#pragma once

#include "../sys/error.hh"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

class efd {
    int fd_;
public:
    using value_type = eventfd_t;
    static constexpr value_type try_again = -1;

    efd(int initval, int flags = 0) {
        int _ = ::eventfd(initval, flags);
        if (_ == -1) THROW_LATEST;
        fd_ = _;
    }
    ~efd() { close(); }
    int native_handle() { return fd_; }
    value_type get() {
        value_type v;
        int _ = eventfd_read(fd_, &v);
        if (_ == -1) {
            if (errno != EAGAIN) {
                THROW_LATEST;
            }
            return try_again;
        }
        return v;
    }
    void set(value_type v) {
        int _ = eventfd_write(fd_, v);
        if (_ == -1) THROW_LATEST;
    }
    bool close() {
        return ::close(fd_) != -1;
    }
};

class epoll {
    int epfd, n = 0;
public:
    epoll(int flags = 0) {
        int _ = epoll_create1(flags);
        if (_ == -1) THROW_LATEST;
        epfd = _;
    }
    ~epoll() { close(epfd); }
    void dec() { --n; }
    void add(int handle, struct epoll_event e) {
        int _ = epoll_ctl(epfd, EPOLL_CTL_ADD, handle, &e);
        if (_ == -1) THROW_LATEST;
        ++n;
    }
    void mod(int handle, struct epoll_event e) {
        int _ = epoll_ctl(epfd, EPOLL_CTL_MOD, handle, &e);
        if (_ == -1) THROW_LATEST;
    }
    void del(int handle) {
        int _ = epoll_ctl(epfd, EPOLL_CTL_DEL, handle, nullptr);
        if (_ == -1) THROW_LATEST;
        --n;
    }
    std::vector<struct epoll_event> wait() {
        std::vector<struct epoll_event> events(n);
        int nevent = epoll_wait(epfd, events.data(), n, -1);
        if (nevent == -1) THROW_LATEST;
        events.resize(nevent);
        return events;
    }
    enum events {
        in = EPOLLIN,
        out = EPOLLOUT,
        error = EPOLLERR,
    };
};