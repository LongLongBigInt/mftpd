#pragma once

#include "io/epoll.hh"
#include "net/tcpsock.hh"
#include "net/sockaddr.hh"

#include "config.hh"

#define FTPD_MAX_MSG_LEN 4096
#define FTPD_MAX_RES_LEN 4096
#define FTPD_BACKLOG 100

#define FTPD_DEFAULT_CONFIG_PATH "/etc/ftpd/config.yaml"

#define DEBUG(FMT, ...) \
printf(FMT "\n", __VA_ARGS__)

namespace G
{
    config cfg;
    epoll ep;
    sock<tcp_listening, ip> ctl;
}
