#pragma once

#include "globals.hh"
#include <netinet/in.h>

#include "net/sockaddr.hh"
#include "net/tcpsock.hh"

enum ftpd_code {
    transfer_open = 150,
    common_ok = 200,
    welcome = 220,
    bye = 221,
    transfer_ok = 226,
    pasv = 227,
    logged_in = 230,
    action_ok = 250,
    printdir = 257,
    require_pass = 331,
    transfer_not_open = 425,
    transfer_fail = 426,
    busy = 450,
    user_abort = 451,
    syntax_error = 500,
    invalid_argument = 501,
    bad_sequence = 503,
    unauth = 530,
    action_fail = 550
};

enum printdir_variant {
    pwd,
    mkd,
};

enum invalid_argument_variant {
    common,
    relogin,
};

enum syntax_error_variant {
    unrecognized_cmd,
    message_too_long,
    empty_cmd
};

enum unauth_variant {
    auth_fail,
    require_auth
};

enum action_fail_variant {
    system_error,
    not_allowed,
    not_a_dir,
    not_a_file,
    already_exist
};

enum transfer_not_open_variant {
    mode_not_set,
    socket_error
};

template <ftpd_code code, int variant = 0>
const char *description_of;

#define FTPD_DESC \
template <> constexpr const char *description_of

// 问候

FTPD_DESC<welcome> = 
    "Service ready for new user.";

FTPD_DESC<bye> = 
    "Bye.";

// 语法错误

FTPD_DESC<syntax_error, unrecognized_cmd> = 
    "Syntax error, command %s unrecognized.";

FTPD_DESC<syntax_error, message_too_long> = 
    "Syntax error, command too long.";

FTPD_DESC<syntax_error, empty_cmd> = 
    "Syntax error, command is empty.";

FTPD_DESC<invalid_argument> = 
    "Syntax error in parameters or arguments.";

FTPD_DESC<bad_sequence> =
    "Bad sequence of commands.";

// 认证

FTPD_DESC<logged_in> = 
    "User logged in.";

FTPD_DESC<require_pass> = 
    "Password required for %s.";

FTPD_DESC<unauth, auth_fail> = 
    "Authentication failed.";

FTPD_DESC<unauth, require_auth> = 
    "Not logged in.";

FTPD_DESC<invalid_argument, relogin> =
    "Reauthentication not supported.";

// 常规操作

FTPD_DESC<common_ok> = 
    "Command %s OK.";

FTPD_DESC<action_ok> = 
    "Requested file action okay, completed.";

FTPD_DESC<busy> =
    "Requested file action not taken.";

FTPD_DESC<action_fail, system_error> = 
    "File action fail: %s.";

FTPD_DESC<action_fail, not_allowed> = 
    "Accessing the path is not allowed.";

FTPD_DESC<action_fail, not_a_dir> = 
    "The requested path is not a directory.";

FTPD_DESC<action_fail, not_a_file> =
    "The requested path is not a file.";

FTPD_DESC<action_fail, already_exist> =
    "Path or file already exists";

// 数据传输

FTPD_DESC<transfer_open> =
    "Opening data connection for %s.";

FTPD_DESC<transfer_ok> =
    "Transfer complete.";

FTPD_DESC<transfer_fail> =
    "Connection closed; transfer aborted.";

FTPD_DESC<user_abort> =
    "Transfer aborted.";

FTPD_DESC<transfer_not_open, mode_not_set> = 
    "Can't open data connection, send PASV/PORT first.";

FTPD_DESC<transfer_not_open, socket_error> = 
    "Can't open data connection: %s.";

// 命令相关

FTPD_DESC<pasv> = 
    "Entering Passive Mode (%hhu,%hhu,%hhu,%hhu,%hhu,%hhu).";

FTPD_DESC<printdir, pwd> = 
    "\"%s\" is the current directory.";

FTPD_DESC<printdir, mkd> = 
    "\"%s\" directory created.";

template <ftpd_code code, int variant = 0, typename... Args>
void respond(sock<tcp_connected, ip> &stream, Args... args) {
    char buf[FTPD_MAX_RES_LEN];
    size_t off = 0, send = 0;
    off += sprintf(buf + off, "%d ", code);
    int len = snprintf(buf + off, sizeof(buf)-off-2, description_of<code, variant>, args...);
    off = std::min(off + len, sizeof(buf) - 2);
    buf[off++] = '\r';
    buf[off++] = '\n';
    while (send < off) {
        ssize_t n = stream.send_nothrow<char>({buf + send, buf + off});
        if (n <= 0) {
            // 出现错误
            // 这里我们不处理，留给epoll::in的分支去做
            return;
        } 
        send += n;
    }
}
