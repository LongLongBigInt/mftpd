#pragma once

#include "globals.hh"
#include "types.hh"
#include "internal.hh"

#include "io/utils.hh"

#include <sys/stat.h>
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
            ssize_t n = c.dstream.send_nothrow<char>({buf + nsend, buf + off});
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
                complete_data_transfer(c, transfer_event::completed, in_evloop);
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
                c.ef.set(efd::unit);
                return true;
            case LIST_handler::error:
                // 发现问题，尝试设置错误
                // 期望是completed（默认），如果发现主线程在刚刚已经设置为别的值则放弃
                transfer_event expect = transfer_event::completed;
                c.wf.compare_exchange_strong(expect, transfer_event::error);
                c.ef.set(efd::unit);
                return true;
            case LIST_handler::pending:
                return false;
        }
    }
};

void do_LIST_transfer(connection &c) {
    struct stat st;
    if (::stat(c.dpath.c_str(), &st) == -1) {
        THROW_LATEST;
    }
    size_t sz = st.st_size;
    // 先关闭读端
    c.dstream.shutdown(SHUT_RD);

    // 超过32k，用单独的线程
    if (sz > 32 * 1024) {
        set_block(c.dstream);
        c.ef = efd::create();
        G::ep.add(c.ef.native_handle(), { 
            epoll::in,
            encode_ptr(handle_type::worker_event, &c) 
        });

        auto worker = [&c] {
            LIST_handler lh(c.dpath);
            while (true) {
                if (worker_check_flag(c)) {
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
        set_nonblock(c.dstream);
        c.handler = new LIST_handler(c.dpath);
        G::ep.add(c.dstream.native_handle(), {
            epoll::out, 
            encode_ptr(handle_type::data_stream, &c) 
        });
    }
    // 直接在这里处理
    else {
        set_block(c.dstream);
        LIST_handler lh(c.dpath);
        while (!lh.handle(c, false)) {
            continue;
        }
    }
}
