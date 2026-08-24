#pragma once

#include "../connection.hh"
#include "../message.hh"

bool ensure_idle(connection &c) {
    switch (c.s) {
        case connection_state::before_auth:
            respond<ftpd_code::unauth, unauth_variant::require_auth>(c.stream);
            break;

        case connection_state::need_pass:
            respond<ftpd_code::bad_sequence>(c.stream);
            break;

        case connection_state::auth_idle:
            return true;

    // RFC 959
    // user-process sending another command before the completion reply would be in violation of protocol; 
    // but server-FTP processes should queue any commands that arrive while a preceding command is in progress.
    // 我们这里也不遵守这个queue行为，视为客户端违规
        case connection_state::auth_busy:
            respond<ftpd_code::busy>(c.stream);
            break;
    }
    return false;
}

// TODO: 添加过滤项
bool ensure_dir(connection &c, const fs::path &path) {
    std::error_code ec;
    fs::file_status st = fs::status(path, ec);
    if (ec) {
        respond<ftpd_code::action_fail, action_fail_variant::system_error>(
            c.stream, std::strerror(ec.value())
        );
        return false;
    }
    if (st.type() != fs::file_type::directory) {
        respond<ftpd_code::action_fail, action_fail_variant::not_a_dir>(
            c.stream, path.c_str()
        );
        return false;
    }
    // TODO: 检查用户权限
    return true;
}
