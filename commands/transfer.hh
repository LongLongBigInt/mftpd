#pragma once

#include "../connection.hh"
#include "../message.hh"

#include <filesystem>

bool require_datamode_set(connection &c) {
    if (c.m == transfer_mode::unset) {
        respond<ftpd_code::open_dconn_error,
                open_dconn_error_variant::mode_not_set>(c.stream);
        return false; 
    }
    return true;
}

void prepare_data_transfer(connection &c, data_commands cmd, const fs::path &path) {
    c.dcmd.command = cmd;
    c.dcmd.target = path;

    switch (c.m) {     
    case transfer_mode::passive:
        globals::ep->add(c.dacceptor.native_handle(), {
            epoll::in,
            encode_ptr(handle_type::data_acceptor, c)
        });
        break;

    case transfer_mode::port:
        c.dstream = sock<tcp, ip>::create_nonblock()
            .bind_reuse({20})
            .connect(c.daddr); // sock_base::connect()不会throw
        globals::ep->add(c.dstream.native_handle(), {
            epoll::out,
            encode_ptr(handle_type::data_connector, c)
        });
        break;
    }
}

// 异步结束传输
void abort_data_transfer(connection &c) {
    if (c.ef) {
        // 如果是线程在工作，停止它
        // 关闭和减少计数由工作线程负责
        c.ef->set(transfer_event::aborted);
    } else {
        // 否则是事件循环，得减少要移除的dstream
        globals::ep->dec();
    }
    c.dstream.close();
}

// 传输结束，回复并流转状态
void on_transfer_complete(connection &c, transfer_event e) {
    switch (e) {
        case transfer_event::completed:
            respond<ftpd_code::transfer_ok>(c.stream);
            break;
        case transfer_event::error:
            respond<ftpd_code::transfer_error>(c.stream);
            break;
        case transfer_event::aborted:
            respond<ftpd_code::aborted>(c.stream);
            break;
    }
    c.m = transfer_mode::unset;
    c.s = connection_state::auth_idle;
}

// 同步结束传输，并发送消息
void complete_data_transfer(connection &c, transfer_event e, bool in_evloop) {
    c.dstream.close();
    if (in_evloop) globals::ep->dec();
    on_transfer_complete(c, e);
}
