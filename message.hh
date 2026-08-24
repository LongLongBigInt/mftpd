#pragma once

#include <tuple>
#include <cctype>
#include <netinet/in.h>

#include "net/sockaddr.hh"
#include "net/tcpsock.hh"

enum ftpd_code {
    open_dconn_success = 150,
    common_ok = 200,
    welcome = 220,
    bye = 221,
    pasv = 227,
    logged_in = 230,
    action_ok = 250,
    printdir = 257,
    require_pass = 331,
    open_dconn_error = 425,
    busy = 450,
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

enum open_dconn_error_variant {
    mode_not_set,
    socket_error
};

template <ftpd_code code, int variant = 0>
const char *description_of;

#define FTPD_DESC template <> \
constexpr const char *description_of

FTPD_DESC<open_dconn_success> = "Opening data connection for %s.";
FTPD_DESC<common_ok> = "Command %s OK.";
FTPD_DESC<welcome> = "Service ready for new user.";
FTPD_DESC<bye> = "Bye.";
FTPD_DESC<logged_in> = "User logged in.";
FTPD_DESC<action_ok> = "Requested file action okay, completed.";
FTPD_DESC<pasv> = "Entering Passive Mode (%hhu,%hhu,%hhu,%hhu,%hhu,%hhu).";
FTPD_DESC<printdir, pwd> = "\"%s\" is the current directory.";
FTPD_DESC<printdir, mkd> = "\"%s\" directory created.";
FTPD_DESC<require_pass> = "Password required for %s.";
FTPD_DESC<open_dconn_error, mode_not_set> = "Can't open data connection, send PASV/PORT first.";
FTPD_DESC<open_dconn_error, socket_error> = "Can't open data connection: %s.";
FTPD_DESC<busy> = "Requested file action not taken.";
FTPD_DESC<invalid_argument> = "Syntax error in parameters or arguments.";
FTPD_DESC<invalid_argument, relogin> = "Reauthentication not supported.";
FTPD_DESC<bad_sequence> = "Bad sequence of commands.";
FTPD_DESC<syntax_error, unrecognized_cmd> = "Syntax error, command %s unrecognized.";
FTPD_DESC<syntax_error, message_too_long> = "Syntax error, command too long.";
FTPD_DESC<syntax_error, empty_cmd> = "Syntax error, command is empty.";
FTPD_DESC<unauth, auth_fail> = "Authentication failed.";
FTPD_DESC<unauth, require_auth> = "Not logged in.";
FTPD_DESC<action_fail, system_error> = "%s.";
FTPD_DESC<action_fail, not_allowed> = "Accessing the path is not allowed.";
FTPD_DESC<action_fail, not_a_dir> = "The requested path is not a directory.";
FTPD_DESC<action_fail, not_a_file> = "The requested path is not a file.";
FTPD_DESC<action_fail, already_exist> = "Path or file already exists";

#undef FTPD_DESC

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

using stream = sock<tcp_connected, ip>;

template <ftpd_code code, int variant = 0>
void respond(stream &stream, auto... args) {
    char buf[FTPD_MAX_RES_LEN];
    size_t off = 0, send = 0;
    off += sprintf(buf + off, "%d ", code);
    off += snprintf(buf + off, sizeof(buf)-off-2, description_of<code, variant>, args...);
    buf[off++] = '\r';
    buf[off++] = '\n';
    while (send < off) {
        send += stream.send<char>({buf + send, buf + off});
    }
}

// template <ftpd_code code, int variant>
// constexpr const char *get_description() {
//     switch (code)
//     {
//     case common_ok:
//         return "Common Okay.";
//     case welcome:
//         return "Service ready for new user.";
//     case bye:
//         return "Bye.";
//     case logged_in:
//         return "User logged in.";
//     case action_ok:
//         return "Requested file action okay, completed.";
//     case pwd:
//         return "\"%s\" is current directory.";
//     case require_pass:
//         return "Password required for %s.";
//     case busy:
//         return "Requested file action not taken.";
//     case syntax_error:
//         switch (variant)
//         {
//         case unrecognized_cmd:
//             return "Syntax error, command %s unrecognized.";
//         case message_too_long:
//             return "Syntax error, command too long.";
//         case empty_cmd:
//             return "Syntax error, command is empty.";
//         }
//     case invalid_argument:
//         return "Syntax error in parameters or arguments.";
//     case bad_sequence:
//         return "Bad sequence of commands.";
//     case unauth:
//         switch (variant)
//         {
//         case auth_fail:
//             return "Authentication failed.";
//         case require_auth:
//             return "Not logged in.";
//         }
//     case action_fail:
//         switch (variant)
//         {
//         case system_error:
//             return "%s.";
//         case not_allowed:
//             return "Accessing path %s is not allowed.";
//         case not_a_dir:
//             return "The requested path %s is not a directory.";
//         }
//     }
// }