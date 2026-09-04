#pragma once

#include "globals.hh"
#include "net/sockbase.hh"
#include "types.hh"
#include "message.hh"
#include "internal.hh"

void do_USER(connection &c, const char *name) {
    if (c.ss == session_state::auth) {
        // 本用户重复USER视为已登录（防止某些客户端重试）
        // 否则不支持重新登录（连接中发送USER的语义有分歧：数据传输是否要断开？）
        // 建议用户使用语义更明确的 (ABOR)+REIN+USER
        if (c.u["name"].Scalar() == name) {
            respond<ftpd_code::logged_in>(c.stream);
        } else {
            respond<ftpd_code::invalid_argument,
                    invalid_argument_variant::relogin>(c.stream);
        }
        return;
    }
    c.u = G::cfg.find_user(name);
    c.ss = session_state::need_pass;
    respond<ftpd_code::require_pass>(c.stream, name);
}

void do_PASS(connection &c, const char *pass) {
    if (c.ss != session_state::need_pass) {
        respond<ftpd_code::bad_sequence>(c.stream);
        return;
    }

    if (!c.u) goto pass_fail;

    if (!c.u["pass"] || c.u["pass"].Scalar() == pass) {
        goto pass_success;
    }

pass_fail:
    c.ss = session_state::before_auth;
    respond<ftpd_code::unauth, unauth_variant::auth_fail>(c.stream);
    return;

pass_success:
    c.ss = session_state::auth;
    if (!config::get_dir(c.u["home"], c.home)) {
        c.home = G::cfg.default_home;
    }
    respond<ftpd_code::logged_in>(c.stream);

    DEBUG("Connection %d: User %s logged in",
        c.stream.native_handle(), c.u["name"].Scalar().c_str());
}

std::string _get_escaped_path(
    const char *formal_path, 
    const char *first_quote
) {
    std::string res(formal_path, first_quote);
    const char *cur = first_quote;
    while (*cur) {
        const char quote = '"';
        res.push_back(*cur);
        if (*cur == quote) {
            res.push_back(*cur);
        }
        ++cur;
    }
    return res;
}

#define get_escape_path(path, first_quote) \
(first_quote ? path : _get_escaped_path(path, first_quote).c_str())

void do_PWD(connection &c) {
    if (!ensure_auth(c)) return;

    const char *path = c.wd.c_str(), *quote = strchr(path, '"');
    respond<ftpd_code::printdir, printdir_variant::pwd>
        (c.stream, get_escape_path(path, quote));
}

void do_CWD(connection &c, const char *path) {
    if (!ensure_auth(c)) return;

    fs::path target = (c.wd / path).lexically_normal();
    if (!ensure_target(c, target, target_type::dir_only)) {
        return;
    }

    c.wd = std::move(target);
    respond<ftpd_code::action_ok>(c.stream);
}

void do_MKD(connection &c, const char *path) {
    if (!ensure_auth(c)) return;

    fs::path target = (c.wd / path).lexically_normal();
    if (!ensure_target(c, target, target_type::dir_only)) {
        return;
    }

    std::error_code ec;
    bool ok = fs::create_directory(target, ec);
    if (ec) {
        respond<ftpd_code::action_fail, action_fail_variant::system_error>
            (c.stream, strerror(ec.value()));
    } else if (!ok) {
        respond<ftpd_code::action_fail, action_fail_variant::already_exist>
            (c.stream);
    } else {
        const char *path = target.c_str(), *quote = strchr(path, '"');
        respond<ftpd_code::printdir, printdir_variant::mkd>
            (c.stream, get_escape_path(path, quote));
    }
}

void do_unlink(connection &c, const char *path, bool is_regular) {
    if (!ensure_auth(c)) return;

    fs::path target = c.wd / path;
    if (!ensure_target(c, target, is_regular ? file_only : dir_only)) {
        return;
    }

    std::error_code ec;
    bool ok = fs::remove(target, ec);
    if (ec) {
        respond<ftpd_code::action_fail, action_fail_variant::system_error>
            (c.stream, strerror(ec.value()));
    } else if (!ok) {
        respond<ftpd_code::action_fail, action_fail_variant::system_error>
            (c.stream, strerror(ENOENT));
    } else {
        respond<ftpd_code::action_ok>(c.stream);
    }
}

void do_TYPE(connection &c, const char *type) {
    if (!ensure_auth(c)) return;

    if (strcasecmp(type, "I") == 0 || strcasecmp(type, "A") == 0) {
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

void do_PORT(connection &c, const char *addrstr) {
    if (!ensure_auth(c)) return;
    // 连接时发送PORT/PASV行为未定义，这里530拒绝
    if (!ensure_idle(c)) return;

    ip addr = resolve_PORT_addr(addrstr);
    if (addr.port == 0) {
        respond<ftpd_code::invalid_argument>(c.stream);
        return;
    }

    // TODO: 检查ip是否允许，这里先限制为必须是控制连接同ip
    if (addr.addr != c.addr.addr) {
        respond<ftpd_code::invalid_argument>(c.stream);
        return;
    }

    abort_transfer_preparation(c);
    c.daddr = addr;
    c.m = transfer_mode::port;
    
    respond<ftpd_code::common_ok>(c.stream, "PORT");
}

void do_PASV(connection &c) {
    if (!ensure_auth(c)) return;
    if (!ensure_idle(c)) return;
    
    abort_transfer_preparation(c);

    // TODO: 固定的pasv-port/pasv-ip绑定可能抛异常
    c.dacceptor = sock<tcp, ip>::create()
        .bind_reuse({G::cfg.pasv_port, G::cfg.pasv_ip})
        .listen(FTPD_BACKLOG);
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
    if (!ensure_target(c, target, target_type::any_file, &c.dst)) {
        return;
    }

    prepare_data_transfer(c, data_commands::list, target);
}

void do_ABOR(connection &c) {
    if (c.ts != transfer_state::in_transfer) {
        // 如果传输还没有完全建立，尽力而为中断
        abort_transfer_preparation(c);
        respond<ftpd_code::transfer_finish, 
                transfer_finish_variant::transfer_abort>(c.stream);
        return;
    }

    // 已经在传输中了，我们根据ef判断是哪种情况
    if (c.ef.valid()) {
        // 为线程设置abort标志
        c.wf.store(transfer_event::aborted);
        // 同时shutdown()打断阻塞中的socket
        c.dstream.shutdown(SHUT_RDWR);
    } else {
        // 是事件循环，直接操作；先设置mask
        G::skips[&c] |= 1 << handle_type::data_stream;
        complete_data_transfer(c, transfer_event::aborted);
    }
}

void do_REIN(connection &c) {
    // 根据规范，REIN保持in_transfer状态的数据连接
    // 对于before_transfer，这里我们就正常销毁
    if (c.ts != transfer_state::in_transfer) {   
        if (c.ts == transfer_state::before_transfer) {
            respond<ftpd_code::transfer_not_open,
                    transfer_not_open_variant::abort_by_user>(c.stream);
        }
        abort_transfer_preparation(c);
    }
    c.ss = session_state::before_auth;
    
    // 接待新用户
    respond<ftpd_code::welcome>(c.stream, G::cfg.welcome_message);
}

bool do_QUIT(connection &c) {
    if (c.ts == transfer_state::before_transfer) {
        respond<ftpd_code::transfer_not_open,
                transfer_not_open_variant::abort_by_user>(c.stream);
    }

    respond<ftpd_code::bye>(c.stream, G::cfg.bye_message);
    
    // 如果此时没有数据传输，可以直接close掉
    if (c.ts != transfer_state::in_transfer) {
        return true;
    }

    // 否则关闭读端，设置为准备关闭的标志
    c.stream.shutdown(SHUT_RD);
    G::ep.del(c.stream); // 避免触发EOF
    c.closing = true;
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
    
    DEBUG("Connection %d: handling ('%s', '%s')",
        c.stream.native_handle(), 
        cmd_len ? cmd : "[NULL]", 
        arg_len ? arg : "[NULL]");

    auto cmd_is = [&](const char *c) {
        return strcasecmp(c, cmd) == 0;
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
        if (require_arg(true)) do_unlink(c, arg, false);
    }
    else if (cmd_is("DELE")) {
        if (require_arg(true)) do_unlink(c, arg, true);
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
