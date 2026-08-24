#pragma once

#include "helper.hh"

// RFC 959 提及
// Servers **may** allow a new USER command to be entered at any point 
// in order to change the access control and/or accounting information. 
// This has the effect of flushing any user, password, 
// and account information already supplied and beginning the login sequence again. 
// All transfer parameters are unchanged and any file transfer in progress is completed under the old access control parameters.
// 我们这里不实现这个，在已登录时拒绝切换用户
// 客户端应该使用 REIN + USER 显式切换
void do_USER(connection &c, const char *name) {
    // 本用户重新登录，直接回530
    if (c.u["name"].Scalar() == name) {
        respond<ftpd_code::logged_in>(c.stream);
        return;
    }
    // 否则，不支持重新登录
    if (c.s == connection_state::auth_idle || c.s == connection_state::auth_busy) {
        respond<ftpd_code::invalid_argument, invalid_argument_variant::relogin>(c.stream);
        return;
    }
    c.u = globals::cfg.find_user(name);
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
    c.s = connection_state::auth_idle;
    c.wd = config::get_dir(c.u["home"]);
    if (c.wd.empty()) {
        c.wd = globals::cfg.default_home();
    }
    respond<ftpd_code::logged_in>(c.stream);
}