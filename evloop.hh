#pragma once

#include "globals.hh"
#include "connection.hh"
#include "message.hh"
#include "cmdparse.hh"

#include <vector>

void on_new_connection() {
    auto [conn, addr] = globals::ctl.accept();

    // TODO: 验证是否接受连接
    printf("new connection from %s\n", addr.to_string().c_str());

    int handle = conn.native_handle();
    globals::ep->add(handle, {
        epoll::in,
        encode_ptr(
            handle_type::control_stream,
            *new connection(std::move(conn), addr)
        )
    });

    respond<ftpd_code::welcome>(conn);
}

void on_connection_event(connection &c, handle_type type, uint32_t ev) {
    printf("[event = 0x%x, type = %d, connfd = %d]\n", 
        ev, type, c.stream.native_handle());

    switch (type) {
        case handle_type::control_stream: {
            if (ev & epoll::in) {
                bool closed = handle_message(c);
                if (closed) delete &c;
            }
            break;
        }

        case handle_type::data_acceptor: {
            if (ev & epoll::in) {
                auto [conn, addr] = c.dacceptor.accept();
                // TODO: 验证是否接受连接
                // 如果成功，关闭自己，然后就可以继续了
                c.dacceptor.close();
                
            }
            break;
        }

        case handle_type::data_connector: {
            if (ev & epoll::out) {
                error_t err = c.dstream.get_error();
                if (err) {
                    respond<ftpd_code::open_dconn_error, 
                        open_dconn_error_variant::socket_error>(c.stream, std::strerror(err));
                    // PORT连接失败，这个socket可以关闭了，同时清除标志
                    c.dstream.close();
                    c.m = transfer_mode::unset;
                } else {
                    // 如果连接成功，我们发送成功报文
                    // 然后就可以按照客户端的要求进行传输了
                    respond<ftpd_code::open_dconn_success>(
                        c.stream, c.dcmd.target.c_str());
                    // ..
                }
            }
            break;
        }

        case handle_type::data_stream: {
            break;
        }
    }
}

void handle_events(const std::vector<struct epoll_event> &events) {
    for (auto [ev, data]: events) {
        // main acceptor
        if (!data.u64) {
            if (ev & epoll::in) {
                on_new_connection();
            }
        }
        else {
            auto [type, c] = decode_ptr(data);
            on_connection_event(c, type, ev);
        }
    }
}

void run_eventloop() {
    while (true) {
        handle_events(globals::ep->wait());
    }
}