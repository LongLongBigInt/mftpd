#pragma once

#include "globals.hh"

enum handle_type {
    control_acceptor,
    data_acceptor,
    control_stream,
    data_stream,
    data_connector,
    worker_event
};

enum connection_state {
    before_auth,
    need_pass,
    auth_idle,
    auth_busy,
    ready_to_close,
};

enum transfer_mode {
    unset, port, passive,
};

enum data_commands {
    list, store, retrieve
};

using user_data_p = YAML::Node;

struct connection {
    // 控制连接
    sock<tcp_connected, ip> stream;
    ip addr;

    // 数据连接
    sock<tcp_connected, ip> dstream;
    sock<tcp_listening, ip> dacceptor;
    ip daddr;

    // FTP 状态
    user_data_p u; /* 用户数据 */
    connection_state s = before_auth; /* FTP 状态 */
    fs::path wd; /* 当前工作目录 */
    transfer_mode m = unset; /* 当前数据传输模式 */
    data_commands dcmd; /* 当前数据传输命令 */
    fs::path dpath; /* 当前数据传输路径 */
    void *handler; /* 数据传输处理器 */
    efd ef; /* 与用户 */

    // 报文解析状态
    struct {
        char buf[FTPD_MAX_MSG_LEN];
        int end = 0;
        bool skip = false;
    } parsing_state;

    connection(sock<tcp_connected, ip> &&stream, ip addr)
        :stream(std::move(stream)), addr(addr) {}

};