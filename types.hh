#pragma once

#include "globals.hh"

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

using user_data_p = YAML::Node;

struct data_handler;

struct connection {
    // 控制连接
    sock<tcp_connected, ip> stream;
    ip addr;
    bool closing = false;

    // 数据连接
    sock<tcp_connected, ip> dstream;
    sock<tcp_listening, ip> dacceptor;
    ip daddr;

    // FTP 状态
    user_data_p u; /* 用户数据 */
    session_state ss = session_state::before_auth;
    transfer_state ts = transfer_state::idle;
    fs::path wd; /* 当前工作目录 */
    transfer_mode m = unset; /* 当前数据传输模式 */
    data_commands dcmd; /* 当前数据传输命令 */
    fs::path dpath; /* 当前数据传输路径 */
    struct stat dst;
    std::unique_ptr<data_handler> handler;
    efd ef; /* 接收工作线程信息的eventfd */
    std::atomic<transfer_event> wf; /* 主线程给工作线程的标志 */

    // 报文解析状态
    struct {
        char buf[FTPD_MAX_MSG_LEN];
        int end = 0;
        bool skip = false;
    } parsing_state;

    connection(sock<tcp_connected, ip> &&stream, ip addr)
        :stream(std::move(stream)), addr(addr) {}

    ~connection() { G::skips[this] = -1; }
};
