#pragma once

#include "helper.hh"

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
    if (!ensure_dir(c, target)) return;

    c.wd = target;
    respond<ftpd_code::action_ok>(c.stream);
}

void do_MKD(connection &c, const char *path) {
    if (!ensure_idle(c)) return;

    fs::path target = (c.wd / path).lexically_normal();
    // if (!ensure_target(c, target, NONE)) return;

    std::error_code ec;
    bool ok = fs::create_directory(target, ec);
    if (!ok) {
        
    }

    print_escaped_path<printdir_variant::mkd>(c, target);
}

void do_RMD(connection &c, const char *path) {
    if (!ensure_idle(c)) return;

    
}

// 删除指定的文件（不包括目录）
void do_DELE(connection &c, const char *path) {
    if (!ensure_idle(c)) return;

    fs::path target = c.wd / path;
    if (!ensure_dir(c, target)) return;

    std::error_code ec;
    fs::remove(target, ec);

    respond<ftpd_code::action_ok>(c.stream);
}

