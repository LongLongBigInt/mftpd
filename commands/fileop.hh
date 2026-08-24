#pragma once

#include "helper.hh"
#include <cerrno>
#include <cstring>
#include <filesystem>

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
    if (!ensure_idle(c)) return;
    print_escaped_path<printdir_variant::pwd>(c, c.wd.lexically_normal());
}

void do_CWD(connection &c, const char *path) {
    if (!ensure_idle(c)) return;

    fs::path target = c.wd / path;
    if (!ensure_target(c, target, fs::file_type::directory)) {
        return;
    }

    c.wd = target;
    respond<ftpd_code::action_ok>(c.stream);
}

void do_MKD(connection &c, const char *path) {
    if (!ensure_idle(c)) return;
    // TODO: 判断权限

    std::error_code ec;
    fs::path target = c.wd / path;
    bool ok = fs::create_directory(target, ec);
    if (ec) {
        respond<ftpd_code::action_fail,
                action_fail_variant::system_error>(c.stream, strerror(ec.value()));
    } else if (!ok) {
        respond<ftpd_code::action_fail, 
                action_fail_variant::already_exist>(c.stream);
    } else {
        print_escaped_path<printdir_variant::mkd>(c, target);
    }
}

void do_remove(connection &c, const char *path, fs::file_type type) {
    if (!ensure_idle(c)) return;
    fs::path target = c.wd / path;

    if (!ensure_target(c, target, type)) {
        return;
    }

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
        print_escaped_path<printdir_variant::mkd>(c, target);
    }
}