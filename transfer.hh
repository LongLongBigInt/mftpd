#pragma once

#include "globals.hh"
#include "types.hh"
#include "internal.hh"

#include "io/utils.hh"

#include <filesystem>
#include <sys/stat.h>

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
    LIST_handler(connection &c): it(c.dpath) {}

    handler_poll_result operator()(connection &c) {
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
                return handler_poll_result::error;    
            }
            nsend += n;
            if (nsend == off) {
                nsend = off = 0;
            }
        }
    ret:
        return it == end 
            ? handler_poll_result::complete 
            : handler_poll_result::pending;
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
        data_handler<LIST_handler>::start_worker(c);
    }
    // 超过1k，在事件循环中处理
    else if (sz > 1024) {
        set_nonblock(c.dstream);
        c.handler = new data_handler<LIST_handler>(c);
        G::ep.add(c.dstream.native_handle(), {
            epoll::out, 
            encode_ptr(handle_type::data_stream, &c) 
        });
    }
    // 直接在这里处理
    else {
        data_handler<LIST_handler>::start_sync(c);
    }
}
