#pragma once

#include "config.hh"
#include "io/epoll.hh"
#include "net/tcpsock.hh"
#include "net/sockaddr.hh"

#include <optional>

#define FTPD_MAX_MSG_LEN 4096
#define FTPD_MAX_RES_LEN 4096
#define FTPD_BACKLOG 100
#define FTPD_DEFAULT_CONFIG_PATH "/etc/ftpd/config.yaml"

namespace globals
{
    config cfg;
    std::optional<epoll> ep;
    sock<tcp_listening, ip> ctl;
}