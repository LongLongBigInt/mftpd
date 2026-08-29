#pragma once

#include "globals.hh"
#include "message.hh"
#include "types.hh"
#include "internal.hh"

#include "io/utils.hh"
#include <sys/stat.h>

#include <filesystem>
#include <system_error>
#include <thread>

template <typename T, typename... Args>
void start_sync(connection &c, Args &&... args) {
    set_block(c.dstream);
    T handler(std::forward<Args>(args)...);
    while (!handler.handle(c)) {
        continue;
    }
}

template <typename T, typename... Args>
void start_worker(connection &c, Args... args) {
    set_block(c.dstream);
    c.ef = efd::create();
    G::ep.add(c.ef, { 
        epoll::in,
        encode_ptr(handle_type::worker_event, &c) 
    });

    auto worker = [&c](Args... args) {
        T handler(args...);
        while (c.wf.load() == transfer_event::none) {
            if (handler.handle_worker(c)) {
                // 正常结束或者退出了
                return;
            }
        }
        c.ef.set(efd::unit);
    };

    std::thread(worker, args...).detach();
}


size_t format_list(const fs::directory_entry &st, iobuf<char> buf) {
    // TODO: 权限 大小 上次修改时间 文件名 ...

    int sz = snprintf(buf.base, buf.len, "%s\r\n", st.path().filename().c_str());
    if (sz < 0 || sz >= buf.len) return 0;
    return sz;
}

class LIST_handler: public data_handler {
    char buf[32 * 1024];
    size_t nsend = 0, off = 0;
    fs::directory_iterator it, end;

public:
    LIST_handler(fs::directory_iterator start): it(start) {}

    handler_poll_result poll(connection &c) override {
        // 现在可写了，我们先准备发送剩余的数据
        // 如果暂时还没有要发送的数据，我们去解析和准备数据
        if (!off) {
            while (it != end) {
                size_t n = format_list(*it, {buf + off, std::end(buf)});
                // 缓冲区满了，我们先去发送了
                if (!n) break;
                // 还没满，我们继续
                off += n;
                std::error_code ec;
                it.increment(ec);
                if (ec) {
                    return handler_poll_result::error;
                }
            }
        }
        // 有缓冲区我们就去发送
        if (off) {
            ssize_t n = c.dstream.send_nothrow<char>({buf + nsend, buf + off});
            if (n == -1) {
                return errno == EAGAIN 
                    ? handler_poll_result::pending
                    : handler_poll_result::error;    
            }
            nsend += n;
            if (nsend == off) {
                nsend = off = 0;
            }
        }
        return it == end && off == 0
            ? handler_poll_result::complete 
            : handler_poll_result::pending;
    }
};

void do_LIST_transfer(connection &c) {
    std::error_code ec;
    fs::directory_iterator start(c.dpath, ec);

    if (ec) {
        complete_data_transfer(c, transfer_event::error);
        return;
    }

    size_t sz = c.dst.st_size;
    
    // 先关闭读端
    c.dstream.shutdown(SHUT_RD);

    // 超过32k，用单独的线程
    if (sz > 32 * 1024) {
        start_worker<LIST_handler>(c, start);
    }
    // 超过1k，在事件循环中处理
    else if (sz > 1024) {
        set_nonblock(c.dstream);
        c.handler = std::make_unique<LIST_handler>(start);
        G::ep.add(c.dstream, {
            epoll::out, 
            encode_ptr(handle_type::data_stream, &c) 
        });
    }
    // 直接在这里处理
    else {
        start_sync<LIST_handler>(c, start);
    }
}

void start_data_transfer(connection &c) {
    respond<ftpd_code::transfer_open>(
        c.stream, c.dpath.filename().c_str());
    c.ts = transfer_state::in_transfer;

    switch (c.dcmd) {
        case data_commands::list:
            do_LIST_transfer(c);
            break;
        case data_commands::retrieve:
        case data_commands::store:
            break;
    }
}