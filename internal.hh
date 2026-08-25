#pragma once

#include "types.hh"

enum transfer_event {
    completed = 1, error, aborted
};

union epoll_data encode_ptr(handle_type type, connection *c) {
    if (type == control_acceptor) return { .u64 = 0 };
    return { .u64 = (uintptr_t) c | (uintptr_t) type };
}

std::tuple<handle_type, connection *>
decode_ptr(union epoll_data data) {
    if (!data.u64) return { control_acceptor, nullptr };
    uintptr_t p = data.u64 & ~uintptr_t{0b111},
              t = data.u64 & uintptr_t{0b111};
    return { (handle_type) t, (connection *) p };
}

bool ensure_idle(connection &c) {
    switch (c.s) {
    case connection_state::before_auth:
        respond<ftpd_code::unauth, 
                unauth_variant::require_auth>(c.stream);
        break;

    case connection_state::need_pass:
        respond<ftpd_code::bad_sequence>(c.stream);
        break;

    case connection_state::auth_idle:
        return true;

    case connection_state::auth_busy:
        respond<ftpd_code::busy>(c.stream);
        break;
    }
    return false;
}

bool require_datamode_set(connection &c) {
    if (c.m == transfer_mode::unset) {
        respond<ftpd_code::transfer_not_open,
                open_dconn_error_variant::mode_not_set>(c.stream);
        return false; 
    }
    return true;
}

// 数据传输命令前调用
// 确保 ensure_idle(c) && require_datamode_set(c)
void prepare_data_transfer(
    connection &c,
    data_commands cmd,
    const fs::path &path
) {
    c.dcmd = cmd;
    c.dpath = path;
    c.s = connection_state::auth_busy;

    switch (c.m) {     
    case transfer_mode::passive:
        G::ep.add(c.dacceptor.native_handle(), {
            epoll::in,
            encode_ptr(handle_type::data_acceptor, &c)
        });
        break;

    case transfer_mode::port:
        c.dstream = sock<tcp, ip>::create_nonblock()
            .connect(c.daddr); // sock_base::connect()不会throw
        G::ep.add(c.dstream.native_handle(), {
            epoll::out,
            encode_ptr(handle_type::data_connector, &c)
        });
        break;
    }
}

// 异步结束传输
void abort_data_transfer(connection &c) {
    if (c.ef.valid()) {
        // 如果是线程在工作，停止它
        // 关闭和减少计数由工作线程负责
        c.ef.set(transfer_event::aborted);
    } else {
        // 否则是事件循环，得减少要移除的dstream
        G::ep.dec();
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
        respond<ftpd_code::transfer_fail>(c.stream);
        break;
    case transfer_event::aborted:
        respond<ftpd_code::user_abort>(c.stream);
        break;
    }
    c.m = transfer_mode::unset;
    c.s = connection_state::auth_idle;
}

// 同步结束传输，并发送消息
void complete_data_transfer(
    connection &c, 
    transfer_event e, 
    bool in_evloop
) {
    c.dstream.close();
    if (in_evloop) G::ep.dec();
    on_transfer_complete(c, e);
}

bool require_permission(connection &c, const fs::path &p) {
    // TODO: 完成权限解析
    // 逐级往上找，如果pi是某个allows项则允许，是某个disallows项则禁止
    // 如果不属于任何匹配结果则禁止
    // 实际上更复杂，先不考虑了
    return true;
}

// 检测给定的目标路径
// 1. FTP 用户权限是否允许
// 2. 访问 stat() 是否出错（如果出错，可能路径不存在或程序缺少中间目录访问权限）
// 3. 与给定文件类型（not_found/dir/file）是否匹配
// 4. 文件是否忙
// 如果提供了st指针，还会把读到的数据给调用者，避免再次系统调用
bool ensure_target(
    connection &c, 
    const fs::path &target_path,
    fs::file_type expected_type,
    fs::file_status *status_out_p = nullptr,
    bool follow_sym = true
) {
    if (!require_permission(c, target_path)) {
        return false;
    }

    std::error_code ec;
    fs::file_status status_buf,
        *st = status_out_p ? status_out_p : &status_buf;

    *st = follow_sym
        ? fs::status(target_path, ec)
        : fs::symlink_status(target_path, ec);
    bool match = expected_type == st->type();
    if (!match && ec) {
        respond<ftpd_code::action_fail, action_fail_variant::system_error>
            (c.stream, strerror(ec.value()));
        return false;
    }
    // 现在要么没错误，要么匹配
    // 先看不匹配的场景
    if (!match) {
        switch (expected_type) {
        case fs::file_type::not_found:
            // MKD，STOR
            // 注意如果这个path中间不存在并不会进入这里，但在之后的处理会报错
            respond<ftpd_code::action_fail, 
                    action_fail_variant::already_exist>(c.stream);
            break;
        case fs::file_type::directory:
            // RMD
            respond<ftpd_code::action_fail,
                    action_fail_variant::not_a_dir>(c.stream);
            break;
        case fs::file_type::regular:
            // DELE, RETR
            respond<ftpd_code::action_fail, 
                    action_fail_variant::not_a_file>(c.stream);
            break;
        }
        return false;
    }
    // TODO: 查看文件是否被占用
    return true;
}