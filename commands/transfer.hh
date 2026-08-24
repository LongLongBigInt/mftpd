#pragma once

#include "helper.hh"

// 异步结束传输
void abort_data_transfer(connection &c) {
    if (c.ef) {
        // 如果是线程在工作，停止它
        // 关闭和减少计数由工作线程负责
        c.ef->set(transfer_event::abort);
    } else {
        // 否则是事件循环，得减少要移除的dstream
        globals::ep->dec();
    }
    c.dstream.close();
}

// 传输结束，回复并流转状态
void on_transfer_complete(connection &c, transfer_event e) {
    switch (e) {
        case transfer_event::complete:
            respond<ftpd_code::transfer_ok>(c.stream);
            break;
        case transfer_event::error:
            respond<ftpd_code::transfer_error>(c.stream);
            break;
        case transfer_event::abort:
            respond<ftpd_code::abort>(c.stream);
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
