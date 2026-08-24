#pragma once

#include "helper.hh"

void do_PORT(connection &c, const char *dest_str) {
    if (!ensure_idle(c)) return;
    
    if (c.m == transfer_mode::passive) {
        c.dacceptor.close();
    }

    ip addr = resolve_PORT_addr(dest_str);
    if (addr.port == 0) {
        respond<ftpd_code::invalid_argument>(c.stream);
        return;
    }
    // TODO: 检查ip是否允许
    c.daddr = addr;
    c.m = transfer_mode::port;
    respond<ftpd_code::common_ok>(c.stream, "PORT");
}

void do_PASV(connection &c) {
    if (!ensure_idle(c)) return;

    // TODO: 在限制的端口范围中挑选一个，并且绑定的ip也允许限制(而不是0.0.0.0)
    // 这里我们先直接使用系统给定的端口
    // 如果先前已经有了，我们直接使用operator=换掉
    // 根据RFC规范，这里创建出来在回复之前要直接listen()
    c.dacceptor = sock<tcp, ip>::create().bind({0, ip::any}).listen(10);
    c.daddr = c.dacceptor.addr();
    c.m = transfer_mode::passive;
    in_port_t port = ntohs(c.daddr.port);
    in_addr_t addr = ntohl(c.daddr.addr);
    char *p = (char *)&port, *a = (char *)&addr;
    respond<ftpd_code::pasv>(c.stream, a[0], a[1], a[2], a[3], p[0], p[1]);
}

bool prepare_data_transfer(connection &c) {
    // assert ensure_idle()
    switch (c.m) {
    case transfer_mode::unset:
        respond<ftpd_code::open_dconn_error,
            open_dconn_error_variant::mode_not_set>(c.stream);
        return false;

    case transfer_mode::passive:
        // 我们现在已经有一个listening的fd了，可以准备accept
        // 直接加入事件循环
        // 这时epoll_wait可能立即完成（因为客户端通常已经connect()了），但我们不管
        globals::ep->add(c.dacceptor.native_handle(), {
            epoll::in,
            encode_ptr(handle_type::data_acceptor, c)
        });
        return true;

    case transfer_mode::port:
        // 我们尝试连接客户端提供的端口
        c.dstream = sock<tcp, ip>::create_nonblock()
            .bind_reuse({20})
            .connect(c.daddr); // sock_base::connect()不会throw
        globals::ep->add(c.dstream.native_handle(), {
            epoll::out,
            encode_ptr(handle_type::data_connector, c)
        });
    }
}