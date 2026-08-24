#pragma once

#include "net/tcpsock.hh"
#include "net/sockaddr.hh"

#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <optional>

#include "globals.hh"

enum handle_type {
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
    unset,
    port,
    passive,
};

enum data_commands {
    list,
    store,
    retrieve
};

enum transfer_event {
    complete,
    abort,
    error
};

using user_data_p = YAML::Node;

struct connection {
    sock<tcp_connected, ip> stream, dstream;
    sock<tcp_listening, ip> dacceptor;

    user_data_p u;
    connection_state s;
    fs::path wd;

    transfer_mode m;
    ip addr, daddr;

    struct {
        char buf[FTPD_MAX_MSG_LEN];
        int end;
        bool skip;
    } parsing_state;

    struct {
        data_commands command;
        fs::path target;
        void *handler;
    } dcmd;

    std::optional<efd> ef;

    connection(sock<tcp_connected, ip> &&stream, ip addr)
        : stream(std::move(stream)), addr(addr)
    {
        s = connection_state::before_auth;
        m = transfer_mode::unset;
        parsing_state.end = 0;
        parsing_state.skip = false;
    }
};

// 这里我们为了方便和性能，把类型数据直接编码在指针（至少 8 byte 对齐）上
union epoll_data encode_ptr(handle_type type, connection &c) {
    return { .u64 = (uintptr_t) &c | (uintptr_t) type };
}

std::tuple<handle_type, connection &> decode_ptr(union epoll_data data) {
    uintptr_t p = data.u64 & ~uintptr_t{0b11}, t = data.u64 & uintptr_t{0b11};
    return { (handle_type) t, *(connection *) p };
}
