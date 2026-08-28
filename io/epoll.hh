#pragma once

#include "handle.hh"
#include "../sys/error.hh"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

class efd: public iohandle {
public:
    using value_type = eventfd_t;
    static constexpr value_type try_again = 0;
    static constexpr value_type unit = 1;

    static efd create(int initval = 0, int flags = 0) {
        efd ef;
        int _ = ::eventfd(initval, flags);
        if (_ == -1) THROW_LATEST;
        ef.set_handle(_);
        return ef;
    }
    value_type get() {
        value_type v;
        int _ = eventfd_read(native_handle(), &v);
        if (_ == -1) {
            if (errno != EAGAIN) {
                THROW_LATEST;
            }
            return try_again;
        }
        return v;
    }
    void set(value_type v) {
        int _ = eventfd_write(native_handle(), v);
        if (_ == -1) THROW_LATEST;
    }
};

class epoll: public iohandle {
    int n = 0;
public:
    static epoll create(int flags = 0) {
        epoll ep;
        int _ = epoll_create1(flags);
        if (_ == -1) THROW_LATEST;
        ep.set_handle(_);
        return ep;
    }
    void dec() { --n; }
    void add(iohandle &ih, struct epoll_event e) {
        int _ = epoll_ctl(native_handle(), EPOLL_CTL_ADD, ih.native_handle(), &e);
        if (_ == -1) THROW_LATEST;
        ih.set_evloop(this);
        ++n;
    }
    void mod(iohandle &ih, struct epoll_event e) {
        int _ = epoll_ctl(native_handle(), EPOLL_CTL_MOD, ih.native_handle(), &e);
        if (_ == -1) THROW_LATEST;
    }
    void del(iohandle &ih) {
        int _ = epoll_ctl(native_handle(), EPOLL_CTL_DEL, ih.native_handle(), nullptr);
        if (_ == -1) THROW_LATEST;
        --n;
        ih.set_evloop(nullptr);
    }
    std::vector<struct epoll_event> wait() {
        std::vector<struct epoll_event> events(n);
        int nevent;
        do {
            nevent = epoll_wait(native_handle(), events.data(), n, -1);
        } while (nevent == -1 && errno == EINTR);
        if (nevent == -1) THROW_LATEST;
        events.resize(nevent);
        return events;
    }
    enum events {
        in = EPOLLIN,
        out = EPOLLOUT,
        error = EPOLLERR,
        hup = EPOLLHUP,
        rdhup = EPOLLRDHUP,
    };
};
