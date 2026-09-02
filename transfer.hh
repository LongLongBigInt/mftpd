#pragma once

#include "globals.hh"
#include "message.hh"
#include "types.hh"
#include "internal.hh"

#include "io/utils.hh"
#include <cstddef>
#include <sys/stat.h>

#include <filesystem>
#include <system_error>
#include <thread>

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
        while (!handler.handle_worker(c)) {
            continue;
        }
    };

    std::thread(worker, args...).detach();
}


template <typename T>
size_t format_list(const fs::path &path, const struct stat &st, iobuf<T> buf) {
    // TODO: 权限 大小 上次修改时间 文件名 ...

    int sz = snprintf((char *) buf.base, buf.bytes(), "%s\r\n", path.filename().c_str());
    if (sz < 0 || sz >= buf.bytes()) return 0;
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
                struct stat st;
                // TODO: 使用 fstatat
                if (stat(it->path().c_str(), &st) == -1) {
                    return handler_poll_result::local_err;
                }
                size_t n = format_list<char>(
                    it->path(), st,
                    {buf + off, std::end(buf)}
                );
                // 缓冲区满了，我们先去发送了
                if (!n) break;
                // 还没满，我们继续
                off += n;
                std::error_code ec;
                it.increment(ec);
                if (ec) {
                    return handler_poll_result::local_err;
                }
            }
        }
        // 有缓冲区我们就去发送
        if (off) {
            ssize_t n = c.dstream.send_nothrow<char>({buf + nsend, buf + off});
            if (n == -1) {
                return errno == EAGAIN 
                    ? handler_poll_result::pending
                    : handler_poll_result::network_err;
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
    
    // 先关闭读端
    c.dstream.shutdown(SHUT_RD);

    // 如果不是目录（是单文件项），我们直接传输
    if (!S_ISDIR(c.dst.st_mode)) {
        set_block(c.dstream);
        char buf[1024 * 10];
        size_t n = format_list(c.dpath, c.dst, iobuf{buf});
        if (!n) {
            complete_data_transfer(c, transfer_event::local_err);
            return;
        }
        size_t left = c.dstream.send_exact<char>({buf, n});
        complete_data_transfer(c, left ? transfer_event::network_err : transfer_event::none);
        return;
    }

    std::error_code ec;
    fs::directory_iterator start(c.dpath, ec);

    if (ec) {
        complete_data_transfer(c, transfer_event::local_err);
        return;
    }

    // 超过32k，用单独的线程
    if (c.dst.st_size > 32 * 1024) {
        start_worker<LIST_handler>(c, start);
        return;
    }

    // 否则，在事件循环中处理
    set_nonblock(c.dstream);
    c.handler = std::make_unique<LIST_handler>(start);
    G::ep.add(c.dstream, {
        epoll::out, 
        encode_ptr(handle_type::data_stream, &c) 
    });
}

void start_data_transfer(connection &c) {
    respond<ftpd_code::transfer_open>(
        c.stream, c.dpath.filename().c_str());

    c.ts = transfer_state::in_transfer;
    c.wf.store(transfer_event::none);

    switch (c.dcmd) {
        case data_commands::list:
            do_LIST_transfer(c);
            break;
        case data_commands::retrieve:
        case data_commands::store:
            break;
    }
}