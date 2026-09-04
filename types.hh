#pragma once

#include "globals.hh"
#include "config.hh"

#include <atomic>
#include <sys/stat.h>

enum handle_type {
    control_acceptor,
    data_acceptor,
    control_stream,
    data_stream,
    data_connector,
    worker_event
};

enum session_state {
    before_auth,
    need_pass,
    auth
};

enum transfer_state {
    idle,
    before_transfer,
    in_transfer
};

enum transfer_mode {
    unset, port, passive,
};

enum data_commands {
    list, store, retrieve
};

enum transfer_event {
    none, network_err, local_err, aborted, destroy
};

struct transfer_handler;

struct rate_limiter {
    in_addr_t ip;
    rate_limiter(in_addr_t ip): ip(ip) {
        if (++G::connections >= G::cfg.max_connections) {
            // 暂时停止接收新连接
            G::ep.del(G::ctl);
            G::skips[nullptr] = -1; 
        }
        // ip的拒绝在接收控制连接那里进行
        ++G::ip_connections[ip];
    }
    ~rate_limiter() {
        const int delta = G::cfg.max_connections > 100 ? 10 : 0;
        int gc = --G::connections;
        if (!G::ctl.evloop() && gc < G::cfg.max_connections - delta) {
            G::ep.add(G::ctl, { epoll::in, { .u64 = 0x0 } });
        }
        auto it = G::ip_connections.find(ip);
        if (--it->second == 0) {
            G::ip_connections.erase(it);
        }
    }
};

struct connection {
    // 控制连接
    sock<tcp_connected, ip> stream;
    ip addr, laddr;
    bool closing = false;

    // 数据连接
    sock<tcp_connected, ip> dstream;
    sock<tcp_listening, ip> dacceptor;
    ip daddr;

    // FTP 状态
    user_info u; /* 用户数据 */
    session_state ss = session_state::before_auth;
    transfer_state ts = transfer_state::idle;
    fs::path wd; /* 当前工作目录 */
    transfer_mode m = transfer_mode::unset; /* 当前数据传输模式 */
    data_commands dcmd; /* 当前数据传输命令 */
    fs::path dpath; /* 当前数据传输路径 */
    struct stat dst;
    std::unique_ptr<transfer_handler> handler;
    efd ef; /* 接收工作线程信息的eventfd */
    std::atomic<transfer_event> wf = transfer_event::none; /* 主线程给工作线程的标志 */

    // 报文解析状态
    struct {
        char buf[FTPD_MAX_MSG_LEN];
        int end = 0;
        bool skip = false;
    } parsing_state;

    rate_limiter rl;

    connection(sock<tcp_connected, ip> &&stream, ip addr)
        :stream(std::move(stream)), addr(addr), rl(addr.addr)
    {
        laddr = this->stream.addr();
        DEBUG("Connection %d: accepted peer %s on %s",
            this->stream.native_handle(), 
            addr.to_string().c_str(),
            laddr.to_string().c_str());
    }

    ~connection() {
        G::skips[this] = -1;
        DEBUG("Connection %d: closing", stream.native_handle());
    }
};
