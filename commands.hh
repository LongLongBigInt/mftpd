#pragma once

#include "globals.hh"
#include "net/sockbase.hh"
#include "types.hh"
#include "message.hh"
#include "internal.hh"

void do_USER(connection &c, const char *name) {
    if (c.s > connection_state::need_pass) {
        // 本用户重复USER视为已登录，否则不支持重新登录
        if (c.u["name"].Scalar() == name) {
            respond<ftpd_code::logged_in>(c.stream);
            return;
        }
        respond<ftpd_code::invalid_argument,
                invalid_argument_variant::relogin>(c.stream);
        return;
    }
    c.u = G::cfg.find_user(name);
    c.s = connection_state::need_pass;
    respond<ftpd_code::require_pass>(c.stream, name);
}

void do_PASS(connection &c, const char *pass) {
    if (c.s != connection_state::need_pass) {
        respond<ftpd_code::bad_sequence>(c.stream);
        return;
    }

    if (!c.u) goto pass_fail;

    if (!c.u["pass"] || c.u["pass"].Scalar() == pass) {
        goto pass_success;
    }

pass_fail:
    c.s = connection_state::before_auth;
    respond<ftpd_code::unauth, unauth_variant::auth_fail>(c.stream);
    return;

pass_success:
    c.s = connection_state::idle;
    c.wd = config::get_dir(c.u["home"]);
    if (c.wd.empty()) {
        c.wd = G::cfg.default_home();
    }
    respond<ftpd_code::logged_in>(c.stream);
}


template <printdir_variant v>
void print_escaped_path(connection &c, const fs::path &formal_path) {
    const char *str = formal_path.c_str();

    auto escape = [](char *dest, const char *src) {
        const char quote = '"';
        int i = 0, j = 0;
        while (src[i]) {
            if ((dest[j++] = src[i++]) == quote) {
                dest[j++] = quote;
            } 
        }
        dest[j] = '\0';
    };

    if (strchr(str, '"')) {
        std::vector<char> buf(2 * strlen(str));
        escape(buf.data(), str);
        respond<ftpd_code::printdir, v>(c.stream, buf.data());
    } else {
        respond<ftpd_code::printdir, v>(c.stream, str);
    }
}

void do_PWD(connection &c) {
    if (!ensure_auth(c)) return;

    print_escaped_path<printdir_variant::pwd>
        (c, c.wd.lexically_normal());
}

void do_CWD(connection &c, const char *path) {
    if (!ensure_auth(c)) return;

    fs::path target = c.wd / path;
    if (!ensure_target(c, target, fs::file_type::directory)) {
        return;
    }

    // TODO: 如果超长，lexically_normal整理一下
    c.wd = std::move(target);
    respond<ftpd_code::action_ok>(c.stream);
}

void do_MKD(connection &c, const char *path) {
    if (!ensure_auth(c)) return;

    fs::path target = c.wd / path;

    // 这里省一次ensure_target，直接调用require_permission验证
    if (!ensure_permission(c, target)) return;

    std::error_code ec;
    bool ok = fs::create_directory(target, ec);
    if (ec) {
        respond<ftpd_code::action_fail,
                action_fail_variant::system_error>(c.stream, strerror(ec.value()));
    } else if (!ok) {
        // 目录已存在ec会被清空，会进入这里；fs::remove()也类似
        respond<ftpd_code::action_fail, 
                action_fail_variant::already_exist>(c.stream);
    } else {
        print_escaped_path<printdir_variant::mkd>(c, target);
    }
}

void do_unlink(connection &c, const char *path, fs::file_type type) {
    if (!ensure_auth(c)) return;

    fs::path target = c.wd / path;

    if (!ensure_target(c, target, type)) return;

    std::error_code ec;
    bool ok = fs::remove(target, ec);
    if (ec) {
        respond<ftpd_code::action_fail,
                action_fail_variant::system_error>(c.stream, strerror(ec.value()));
    } else if (!ok) {
        // FIX: 跨平台
        respond<ftpd_code::action_fail,
                action_fail_variant::system_error>(c.stream, strerror(ENOENT));
    } else {
        respond<ftpd_code::action_ok>(c.stream);
    }
}

void do_TYPE(connection &c, const char *type) {
    if (!ensure_auth(c)) return;

    if (strcasecmp(type, "I") == 0) {
        respond<ftpd_code::common_ok>(c.stream, "TYPE");
        return;
    }

    respond<ftpd_code::invalid_argument>(c.stream);
}

ip resolve_PORT_addr(const char *addr_str /* 1,2,3,4,5,6 */) {
    auto read_u8 = [](const char *str) -> std::tuple<const char *, uint8_t> {
        unsigned int value = 0;
        bool has_value = false;
        for (; isdigit(*str); ++str) {
            has_value = true;
            value = value * 10 + (*str - '0');
            if (value > 255) return {};
        }
        if (!has_value) return {};
        return {str, value};
    };

    ip addr;
    auto &[port, ad] = addr;
    const char sep = ',';
    // 这里我们使用网络序（大端），稍后转化为平台序
    for (int i = 0; i < 6; ++i) {
        auto [nxt, val] = read_u8(addr_str);
        auto next_ok = [&] { return i == 5 ? *nxt == '\0' : *nxt == sep; };

        if (!nxt || !next_ok()) {
            port = 0; // port = 0 为无效
            return addr;
        }
        (i < 4 ? ((char *) &ad)[i] : ((char *) &port)[i-4]) = (char) val;
        addr_str = nxt + 1;
    }
    ad = ntohl(ad);
    port = ntohs(port);
    return addr;
}

void do_PORT(connection &c, const char *dest_str) {
    if (!ensure_auth(c)) return;
    // 连接时发送PORT/PASV行为未定义，这里530拒绝
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
    if (!ensure_auth(c)) return;
    if (!ensure_idle(c)) return;

    // TODO: 在限制的端口范围中挑选一个
    // 这里我们先直接使用系统给定的端口
    // 如果先前已经有了，我们直接使用operator=换掉
    // 根据RFC规范，这里创建出来在回复之前要直接listen()
    c.dacceptor = sock<tcp, ip>::create()
        .bind_reuse({0, G::cfg.host()})
        .listen(10);
    c.daddr = c.dacceptor.addr();
    c.m = transfer_mode::passive;

    in_port_t port = htons(c.daddr.port);
    in_addr_t addr = htonl(c.daddr.addr);
    char *p = (char *)&port, *a = (char *)&addr;
    respond<ftpd_code::pasv>(c.stream, 
        a[0], a[1], a[2], a[3], p[0], p[1]);
}

void do_LIST(connection &c, const char *path) {
    if (!ensure_transferable(c)) {
        return;
    }

    const fs::path &target = path ? c.wd / path : c.wd;
    if (!ensure_target(c, target, fs::file_type::directory)) {
        return;
    }

    prepare_data_transfer(c, data_commands::list, target);
}

void do_ABOR(connection &c) {
    if (c.s < connection_state::in_transfer) {   
        abort_transfer_preparation(c);

        c.m = transfer_mode::unset;
        if (c.s > connection_state::idle) {
            c.s = connection_state::idle;
        }

        respond<ftpd_code::common_ok>(c.stream, "ABORT");
        return;
    }

    // 已经在传输中了，我们根据ef判断是哪种情况
    if (c.ef.valid()) {
        // 为线程设置abort标志
        c.wf.store(transfer_event::aborted);
    } else {
        // 是事件循环，直接操作
        complete_data_transfer(c, transfer_event::aborted, true);
    }
    return;
}

void do_REIN(connection &c) {
    if (c.s < connection_state::in_transfer) {   
        abort_transfer_preparation(c);
        c.s = connection_state::before_auth;
        c.m = transfer_mode::unset;
    }

    // 根据规范，如果数据传输还在，不应该中断它
    // 这里我们创建一个新的connection用于新用户
    // 然后像QUIT一样，打上ready_to_close标签
    // 当数据传输完毕后，complete_data_transfer会检查这个标记并释放它
    else if (c.s == connection_state::in_transfer) {
        int fd = c.stream.native_handle();
        auto &dup = *(sock<tcp_connected, ip> *) &fd; // small hack
        G::ep.mod(c.stream.native_handle(), {
            epoll::in, 
            encode_ptr(
                handle_type::control_stream,
                new connection(std::move(dup), c.addr)
            )
        });
        c.s = connection_state::ready_to_close;
        c.detached = true;
    }

    // 接待新用户
    respond<ftpd_code::welcome>(c.stream);
}

bool do_QUIT(connection &c) {
    respond<ftpd_code::bye>(c.stream);
    // 如果此时没有数据传输，可以直接close掉
    if (c.s != connection_state::in_transfer) {
        // 调用方随即会调用delete &c，这也将自动释放可能正在连接的pasv/port socket
        return true;
    }
    // 否则关闭读端，设置为准备关闭的标志
    c.stream.shutdown(SHUT_RD);
    c.s = connection_state::ready_to_close;
    return false;
}

bool cmd_dispatch(connection &c, int begin, int sep, int term) {
    int total_len = term - begin,
        arg_len = sep == -1 ? 0 : term - sep - 1,
        cmd_len = sep == -1 ? total_len : sep - begin;

    char *cmd = c.parsing_state.buf + begin,
        *arg = cmd + cmd_len + 1;

    // 为了方便，我们在此处直接设置NUL
    // 让后续命令Handler能接收到标准C字符串
    cmd[cmd_len] = arg[arg_len] = '\0'; // 如果没有arg_len相当于设置cmd[cmd_len+1]即\n上

    auto cmd_is = [&](const char *c) {
        return cmd_len == strlen(c) && strcasecmp(c, cmd) == 0;
    };

    auto require_arg = [arg_len, &c](bool required) {
        bool ok = required ^ (arg_len == 0);
        if (!ok) {
            respond<ftpd_code::invalid_argument>(c.stream);
        }
        return ok;
    };

    if (cmd_len == 0) {
        respond<ftpd_code::syntax_error,
            syntax_error_variant::empty_cmd>(c.stream);
    }
    else if (cmd_is("NOOP")) {
        respond<ftpd_code::common_ok>(c.stream, "NOOP");
    }
    else if (cmd_is("USER")) {
        if (require_arg(true)) do_USER(c, arg);
    }
    else if (cmd_is("PASS")) {
        if (require_arg(true)) do_PASS(c, arg);
    }
    else if (cmd_is("PWD")) {
        if (require_arg(false)) do_PWD(c);
    }
    else if (cmd_is("CWD")) {
        if (require_arg(true)) do_CWD(c, arg);
    }
    else if (cmd_is("MKD")) {
        if (require_arg(true)) do_MKD(c, arg);
    }
    else if (cmd_is("RMD")) {
        if (require_arg(true)) do_unlink(c, arg, fs::file_type::directory);
    }
    else if (cmd_is("DELE")) {
        if (require_arg(true)) do_unlink(c, arg, fs::file_type::regular);
    }
    else if (cmd_is("PORT")) {
        if (require_arg(true)) do_PORT(c, arg);
    }
    else if (cmd_is("PASV")) {
        if (require_arg(false)) do_PASV(c);
    }
    else if (cmd_is("TYPE")) {
        if (require_arg(true)) do_TYPE(c, arg);
    }
    else if (cmd_is("LIST")) {
        do_LIST(c, arg_len ? arg : nullptr);
    }
    else if (cmd_is("ABOR")) {
        if (require_arg(false)) do_ABOR(c);
    }
    else if (cmd_is("REIN")) {
        if (require_arg(false)) do_REIN(c);
    }
    else if (cmd_is("QUIT")) {
        if (require_arg(false)) return do_QUIT(c);
    }
    else {
        respond<ftpd_code::syntax_error, 
                syntax_error_variant::unrecognized_cmd>(c.stream, cmd);
    }

    return false;
}
