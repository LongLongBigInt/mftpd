#pragma once

#include "transfer.hh"

#include "../io/utils.hh"

#include <thread>

size_t format_list(const fs::directory_entry &st, iobuf<char> buf) {
    // 权限 大小 上次修改时间 文件名

    int sz = snprintf(buf.base, buf.len, "%s\n", st.path().filename().c_str());
    if (sz < 0 || sz >= buf.len) return 0;
    return sz;
}

class LIST_handler {
    char buf[32 * 1024];
    size_t nsend = 0, off = 0;
    fs::directory_iterator it, end;

public:
    LIST_handler(const fs::path &path): it(path) {}

    enum do_write_status { 
        pending, complete, error 
    };
    do_write_status do_write(connection &c) {
        // 现在可写了，我们先准备发送剩余的数据
        // 如果暂时还没有要发送的数据，我们去解析和准备数据
        if (!off) {
            while (it != end) {
                size_t n = format_list(*it, {buf + off, std::end(buf)});
                // 缓冲区满了，我们先去发送了
                if (!n) break;
                // 还没满，我们继续
                off += n;
                ++it;
            }
        }
        // 有缓冲区我们就去发送
        if (off) {
            ssize_t n = c.dstream.send_nothrow({buf + nsend, buf + off});
            if (n == -1) {
                if (errno == EAGAIN) goto ret;
                return error;    
            }
            nsend += n;
            if (nsend == off) {
                nsend = off = 0;
            }
        }
    ret:
        return it == end ? complete : pending;
    }

    bool handle(connection &c, bool in_evloop) {
        switch (do_write(c)) {
            case LIST_handler::complete:
                complete_data_transfer(c, transfer_event::complete, in_evloop);
                return true;
            case LIST_handler::error:
                complete_data_transfer(c, transfer_event::error, in_evloop);
                return true;
            case LIST_handler::pending:
                return false;
        }
    }

    bool handle_worker(connection &c) {
        switch (do_write(c)) {
            case LIST_handler::complete:
                c.ef->set(transfer_event::complete);
                return true;
            case LIST_handler::error:
                c.ef->set(transfer_event::error);
                return true;
            case LIST_handler::pending:
                return false;
        }
    }
};

void do_LIST_transfer(connection &c) {
    size_t sz = fs::file_size(c.dcmd.target);
    // 先关闭读端
    c.dstream.shutdown(SHUT_RD);

    // 超过32k，用单独的线程
    if (sz > 32 * 1024) {
        set_block(c.dstream.native_handle());
        c.ef = efd(0, EFD_NONBLOCK);
        globals::ep->add(c.ef->native_handle(), { 
            epoll::in,
            encode_ptr(handle_type::worker_event, c) 
        });

        auto worker = [&c] {
            LIST_handler lh(c.dcmd.target);
            while (true) {
                // 先看看是否有消息
                if (c.ef->get() == transfer_event::abort) {
                    c.ef.reset();
                    globals::ep->dec();
                    return;
                }
                if (lh.handle_worker(c)) {
                    // 正常结束或者退出了
                    return;
                }
            }
        };

        std::thread(worker).detach();
    }
    // 超过1k，在事件循环中处理
    else if (sz > 1024) {
        set_nonblock(c.dstream.native_handle());
        c.dcmd.handler = new LIST_handler(c.dcmd.target);
        globals::ep->add(c.dstream.native_handle(), {
            epoll::out, 
            encode_ptr(handle_type::data_stream, c) 
        });
    }
    // 直接在这里处理
    else {
        set_block(c.dstream.native_handle());
        LIST_handler lh(c.dcmd.target);
        while (!lh.handle(c, false)) {
            continue;
        }
    }
}