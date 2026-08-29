#pragma once

#include "globals.hh"
#include "types.hh"
#include "message.hh"

#include <cerrno>
#include <sys/stat.h>

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

bool ensure_auth(connection &c) {
    if (c.s == connection_state::need_pass) {
        respond<ftpd_code::bad_sequence>(c.stream);
        return false;
    }
    if (c.s == connection_state::before_auth) {
        respond<ftpd_code::unauth, 
                unauth_variant::require_auth>(c.stream);
        return false;
    }
    return true;
}

bool ensure_idle(connection &c) {
    if (c.s == connection_state::before_transfer ||
        c.s == connection_state::in_transfer) {
        respond<ftpd_code::bad_sequence>(c.stream);
        return false;
    }
    return true;
}

bool ensure_transferable(connection &c) {
    if (!ensure_auth(c)) return false;
    if (!ensure_idle(c)) return false;

    if (c.m == transfer_mode::unset) {
        respond<ftpd_code::transfer_not_open,
                transfer_not_open_variant::mode_not_set>(c.stream);
        return false; 
    }

    return true;
}

// 数据传输流程
// 收到数据传输命令后：
// [state: idle]
// assert ensure_transferable()
// prepare_data_transfer()
// [state: before_transfer]
// await on_PORT_connected()/on_PASV_accepted()  <- abort_transfer_preparation()
// start_data_transfer()
// [state: in_transfer]
// await evloop
// complete_data_transfer()

void prepare_data_transfer(
    connection &c,
    data_commands cmd,
    const fs::path &path
) {
    c.dcmd = cmd;
    c.dpath = std::move(path);
    c.s = connection_state::before_transfer;

    switch (c.m) {     
    case transfer_mode::passive:
        G::ep.add(c.dacceptor, {
            epoll::in,
            encode_ptr(handle_type::data_acceptor, &c)
        });
        break;

    case transfer_mode::port:
        // TODO: 允许配置使用特定的ip连接
        c.dstream = sock<tcp, ip>::create_nonblock()
            .connect(c.daddr); // sock_base::connect()不会throw
        G::ep.add(c.dstream, {
            epoll::out,
            encode_ptr(handle_type::data_connector, &c)
        });
        break;
    }
}

// 取消由pasv/port启动的端口；对于idle以前的状态是noop
void abort_transfer_preparation(connection &c) {
    switch (c.m) {
        case transfer_mode::unset:
            break;

        case transfer_mode::passive:
            G::skips[&c] |= 1 << handle_type::data_acceptor;
            c.dacceptor.close();
            break;

        case transfer_mode::port:
            if (c.s == connection_state::before_transfer) {
                G::skips[&c] |= 1 << handle_type::data_connector;
                c.dstream.close();
            }
            break;
    }
}

// 同步结束传输，并发送消息
// 注意此路径可能会导致connection析构（如果处于ready_to_close状态）
void complete_data_transfer(connection &c, transfer_event e) {
    c.handler.reset();
    c.dstream.close();

    switch (e) {
        case transfer_event::none:
            respond<ftpd_code::transfer_ok>(c.stream);
            break;
        case transfer_event::error:
            respond<ftpd_code::transfer_fail>(c.stream);
            break;
        case transfer_event::aborted:
            respond<ftpd_code::user_abort>(c.stream);
            break;
    }

    if (c.s == connection_state::ready_to_close) {
        if (c.detached) {
            c.stream.detach();
        }
        delete &c;
    } else {
        c.m = transfer_mode::unset;
        c.s = connection_state::idle;
    }
}

bool ensure_permission(connection &c, const fs::path &p) {
    // TODO: 完成权限解析
    // 逐级往上找，如果pi是某个allows项则允许，是某个disallows项则禁止
    // 如果不属于任何匹配结果则禁止
    return true;
}

// 检测给定的目标路径
// 1. FTP 用户权限是否允许
// 2. 访问 stat() 是否出错（如果出错，可能路径不存在或程序缺少中间目录访问权限）
// 3. 与给定文件类型（not_found/dir/file）是否匹配
// 4. 文件是否忙
// 如果提供了st指针，还会把读到的数据给调用者，避免再次系统调用
// fs::status返回的信息实在太少，所以这里改成了平台的stat()
bool ensure_target(
    connection &c, 
    const fs::path &target_path,
    mode_t expected_type, // S_IF...
    struct stat *status_out_p = nullptr
) {
    if (!ensure_permission(c, target_path)) {
        return false;
    }
    struct stat buf, *st = status_out_p ? status_out_p : &buf;

    int _ = stat(target_path.c_str(), st);

    bool match = (_ == -1 && errno == ENOENT)
        ? expected_type == 0
        : (st->st_mode & S_IFMT) == expected_type;

    if (!match && errno) {
        respond<ftpd_code::action_fail, action_fail_variant::system_error>
            (c.stream, strerror(errno));
        return false;
    }
    // 现在要么没错误，要么匹配
    // 先看不匹配的场景
    if (!match) {
        switch (expected_type) {
        case 0:
            // MKD，STOR
            // 注意如果这个path中间不存在并不会进入这里，但在之后的处理会报错
            respond<ftpd_code::action_fail, 
                    action_fail_variant::already_exist>(c.stream);
            break;
        case S_IFDIR:
            // RMD
            respond<ftpd_code::action_fail,
                    action_fail_variant::not_a_dir>(c.stream);
            break;
        case S_IFREG:
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

struct data_handler {

    enum class handler_poll_result {
        complete, error, pending
    };

    virtual ~data_handler() = default;

    virtual handler_poll_result poll(connection &c) = 0;

    bool handle(connection &c) {
        switch (poll(c)) {
            case handler_poll_result::complete:
                complete_data_transfer(c, transfer_event::none);
                return true;

            case handler_poll_result::error:
                complete_data_transfer(c, transfer_event::error);
                return true;

            case handler_poll_result::pending:
                return false;
        }
    }

    bool handle_worker(connection &c) {
        switch (poll(c)) {
            case handler_poll_result::complete:
                break;
            
            case handler_poll_result::error: {
                // 发现问题，尝试设置错误
                // 期望是completed（默认），如果发现主线程在刚刚已经设置为别的值则放弃
                transfer_event expect = transfer_event::none;
                c.wf.compare_exchange_strong(expect, transfer_event::error);
                break;
            }

            case handler_poll_result::pending:
                return false;
        }

        c.ef.set(efd::unit);
        return true;
    }

};
