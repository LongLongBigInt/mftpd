#pragma once

#include "connection.hh"
#include "commands/login.hh"
#include "commands/fileop.hh"
#include "commands/datamode.hh"
#include <filesystem>

// RFC 规定的最小实现
// USER, QUIT, PORT, TYPE, MODE, STRU, RETR, STOR, NOOP

void cmd_dispatch(connection &c, int begin, int sep, int term) {
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
        if (require_arg(true)) do_remove(c, arg, fs::file_type::directory);
    }
    else if (cmd_is("DELE")) {
        if (require_arg(true)) do_remove(c, arg, fs::file_type::regular);
    }
    else if (cmd_is("PORT")) {
        if (require_arg(true)) do_PORT(c, arg);
    }
    else if (cmd_is("PASV")) {
        if (require_arg(false)) do_PASV(c);
    }
    else {
        respond<ftpd_code::syntax_error, 
            syntax_error_variant::unrecognized_cmd>(c.stream, cmd);
    }
}

bool handle_message(connection &c) {
    auto &[buf, end, skip] = c.parsing_state;

    size_t n = c.stream.recv<char>({buf + end, std::end(buf)});
    if (n == 0) {
        return true;
    }
    end += n;

    int sep = -1, begin = 0;
    for (int i = 0; i < end; ++i) {
        if (buf[i] == ' ' && sep == -1) {
            sep = i;
        }
        if (i > 0 && buf[i-1] == '\r' && buf[i] == '\n') {
            if (skip) {
                skip = false;
            } else {
                // TODO: 这里可能已经需要结束连接return了
                cmd_dispatch(c, begin, sep, i-1);
            }
            begin = i + 1;
            sep = -1;
        }
    }
    if (begin == 0 && end == FTPD_MAX_MSG_LEN) {
        respond<ftpd_code::syntax_error, 
            syntax_error_variant::message_too_long>(c.stream);
        skip = true;
        end = 0;
    } else if (begin != 0) {
        end -= begin;
        memmove(buf, buf + begin, end);
    }

    return false;
}