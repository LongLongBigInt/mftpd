#pragma once

#include "../connection.hh"
#include "../message.hh"

#include <cstring>
#include <filesystem>

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

bool require_permission(connection &c, const fs::path &p) {
    // TODO: 完成权限解析
    // 逐级往上找，如果pi是某个allows项则允许，是某个disallows项则禁止
    // 如果不属于任何匹配结果则禁止
    // 实际上更复杂，先不考虑了
    return true;
}

// 检测给定的目标路径
// 1. FTP 用户权限是否允许
// 2. 访问 stat() 是否出错（如果出错，可能路径不存在或程序缺少中间目录访问权限）
// 3. 与给定文件类型（not_found/dir/file）是否匹配
// 4. 文件是否忙
// 如果提供了st指针，还会把读到的数据给调用者，避免再次系统调用
bool ensure_target(
    connection &c, 
    const fs::path &target_path,
    fs::file_type expected_type,
    fs::file_status *status_out_p = nullptr,
    bool follow_sym = true
) {
    if (!require_permission(c, target_path)) {
        return false;
    }

    std::error_code ec;
    fs::file_status status_buf,
        *st = status_out_p ? status_out_p : &status_buf;

    *st = follow_sym
        ? fs::symlink_status(target_path, ec)
        : fs::status(target_path, ec);
    bool match = expected_type == st->type();
    if (!match && ec) {
        respond<ftpd_code::action_fail, action_fail_variant::system_error>
            (c.stream, strerror(ec.value()));
        return false;
    }
    // 现在要么没错误，要么匹配
    // 先看不匹配的场景
    if (!match) {
        switch (expected_type) {
        case fs::file_type::not_found:
            // MKD，STOR
            // 注意如果这个path中间不存在并不会进入这里，但在之后的处理会报错
            respond<ftpd_code::action_fail, 
                    action_fail_variant::already_exist>(c.stream);
            break;
        case fs::file_type::directory:
            // RMD
            respond<ftpd_code::action_fail,
                    action_fail_variant::not_a_dir>(c.stream);
            break;
        case fs::file_type::regular:
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