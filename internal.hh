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
    switch (c.ss) {
        case session_state::need_pass:
            respond<ftpd_code::bad_sequence>(c.stream);
            return false;
        case session_state::before_auth:
            respond<ftpd_code::unauth, 
                unauth_variant::require_auth>(c.stream);
            return false;
        case session_state::auth:
        default:
            return true;
    }
}

bool ensure_idle(connection &c) {
    if (c.ts != transfer_state::idle) {
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
    c.ts = transfer_state::before_transfer;

    switch (c.m) {
        case transfer_mode::passive:
            G::ep.add(c.dacceptor, {
                epoll::in,
                encode_ptr(handle_type::data_acceptor, &c)
            });
            break;

        case transfer_mode::port:
            auto [ok, stream] = sock<tcp, ip>::create_nonblock()
                // 使用控制连接相同的ip
                .bind({0, c.laddr.addr})
                .connect_nothrow(c.daddr);

            // 这里只检查同步错误，异步错误留给后面sock.get_error()处理
            if (!ok && errno != EINPROGRESS) {
                respond<ftpd_code::transfer_not_open,
                        transfer_not_open_variant::socket_error>
                        (c.stream, strerror(errno));

                c.m = transfer_mode::unset;
                c.ts = transfer_state::idle;
                return;
            }

            c.dstream = std::move(stream);
            G::ep.add(c.dstream, {
                epoll::out,
                encode_ptr(handle_type::data_connector, &c)
            });
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
            respond<ftpd_code::transfer_finish,
                    transfer_finish_variant::transfer_ok>(c.stream);
            break;

        case transfer_event::network_err:
            respond<ftpd_code::transfer_fail,
                    transfer_fail_variant::network_error>(c.stream);
            break;

        case transfer_event::local_err:
            respond<ftpd_code::transfer_local_error>(c.stream);
            break;

        case transfer_event::aborted:
            respond<ftpd_code::transfer_fail, 
                    transfer_fail_variant::user_abort>(c.stream);
            break;
    }

    if (c.closing) {
        delete &c;
    } else {
        c.m = transfer_mode::unset;
        c.ts = transfer_state::idle;
    }
}

// 打断idle(set)/before_transfer状态，并设回idle(unset)
// 如果在before_transfer状态还会完成发送425结束状态机
void abort_transfer_preparation(connection &c) {
    switch (c.m) {
        case transfer_mode::unset:
            break;

        case transfer_mode::passive:
            G::skips[&c] |= 1 << handle_type::data_acceptor;
            c.dacceptor.close();
            break;

        case transfer_mode::port:
            if (c.ts == transfer_state::before_transfer) {
                G::skips[&c] |= 1 << handle_type::data_connector;
                c.dstream.close();
            }
            break;
    }
    
    c.m = transfer_mode::unset;
    c.ts = transfer_state::idle;
}

bool ensure_permission(connection &c, const fs::path &p) {
    // TODO
    return true;
}

enum target_type {
    file_only, dir_only, any_file, non_exist
};

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
    target_type expected_type, // S_IF...
    struct stat *status_out_p = nullptr
) {
    fs::path real_target = (c.home / c.wd).lexically_normal();
    if (!ensure_permission(c, real_target)) {
        return false;
    }
    struct stat buf, *st = status_out_p ? status_out_p : &buf;

    bool ok = stat(real_target.c_str(), st) == 0;
    
    if (ok) {
        if (expected_type == non_exist) {
            respond<ftpd_code::action_fail, 
                    action_fail_variant::already_exist>(c.stream);
            return false; 
        }
        if (expected_type == file_only && !S_ISREG(st->st_mode)) {
            respond<ftpd_code::action_fail, 
                    action_fail_variant::not_a_file>(c.stream);
            return false;
        }
        if (expected_type == dir_only && !S_ISDIR(st->st_mode)) {
            respond<ftpd_code::action_fail,
                    action_fail_variant::not_a_dir>(c.stream);
            return false;
        }
    }
    else if (!(errno == ENOENT && expected_type == non_exist)) {
        respond<ftpd_code::action_fail,
                action_fail_variant::system_error>(c.stream, strerror(errno));
        return false;
    }

    // TODO: 查看文件是否被占用
    return true;
}

struct transfer_handler {

    enum class poll_result {
        pending, complete, network_err, local_err
    };
    
    static transfer_event result_mapping(poll_result r) {
        switch (r) {
        case poll_result::complete: return none;
        case poll_result::network_err: return network_err;
        default:
        case poll_result::local_err: return local_err;
        }
    }

    virtual ~transfer_handler() = default;

    virtual poll_result poll(connection &c) = 0;

    bool handle(connection &c) {
        poll_result result = poll(c);
        if (result == poll_result::pending) {
            return false;
        }
        complete_data_transfer(c, result_mapping(result));
        return true;
    }

    bool handle_worker(connection &c) {
        transfer_event expect = transfer_event::none;

        // 如果主线程已经设置标志了，我们直接退出
        if (c.wf.load() != expect) {
            c.ef.set(efd::unit);
            return true;
        }

        // 否则我们进行poll()
        // 如果主线程有通知会设wf并打断
        switch (poll_result result = poll(c)) {
            case poll_result::complete:
                break;
            
            case poll_result::local_err:
            case poll_result::network_err: {
                // 发现问题，尝试设置错误
                // 期望是默认的none，如果发现主线程在刚刚已经设置为别的值则放弃
                c.wf.compare_exchange_strong(expect, result_mapping(result));
                break;
            }

            case poll_result::pending:
                return false;
        }

        c.ef.set(efd::unit);
        return true;
    }

};
