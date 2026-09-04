#pragma once

#include "io/epoll.hh"
#include "net/tcpsock.hh"
#include "net/sockaddr.hh"

#include <unordered_map>
#include <cstdio>
#include <filesystem>

#define FTPD_ENABLE_LOG

#define FTPD_MAX_MSG_LEN 4096
#define FTPD_MAX_RES_LEN 4096
#define FTPD_BACKLOG 100
#define FTPD_DEFAULT_CONFIG_PATH "/etc/ftpd/config.yaml"

#ifdef FTPD_ENABLE_LOG
    #define DEBUG(FMT, ...) printf("[DEBUG] " FMT "\n", __VA_ARGS__)
    #define INFO(FMT, ...) printf("[INFO] " FMT "\n", __VA_ARGS__)
    #define WARNING(FMT, ...) fprintf(stderr, "[WARNING] " FMT "\n", __VA_ARGS__)
#else
    #define DEBUG(...)
    #define INFO(...)
    #define WARNING(...)
#endif

namespace fs = std::filesystem;

namespace G
{
    epoll ep;
    std::unordered_map<void *, uint32_t> skips;
    sock<tcp_listening, ip> ctl;
    int connections;
    std::unordered_map<in_addr_t, int> ip_connections;
}
