#pragma once

#include "io/epoll.hh"
#include "net/tcpsock.hh"
#include "net/sockaddr.hh"

#include "config.hh"
#include <cstdint>
#include <unordered_map>

#define FTPD_MAX_MSG_LEN 4096
#define FTPD_MAX_RES_LEN 4096
#define FTPD_BACKLOG 100

#define FTPD_DEFAULT_CONFIG_PATH "/etc/ftpd/config.yaml"

#define DEBUG(FMT, ...) printf("[DEBUG] " FMT "\n", __VA_ARGS__)

#define INFO(FMT, ...) printf("[INFO] " FMT "\n", __VA_ARGS__)

namespace G
{
    config cfg;
    epoll ep;
    std::unordered_map<void *, uint32_t> skips;
    sock<tcp_listening, ip> ctl;
}
