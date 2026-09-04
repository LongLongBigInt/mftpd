#pragma once

#include "io/epoll.hh"
#include "net/tcpsock.hh"
#include "net/sockaddr.hh"

#include "config.hh"

#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace G
{
    config cfg;
    epoll ep;
    std::unordered_map<void *, uint32_t> skips;
    sock<tcp_listening, ip> ctl;
    int connections;
    std::unordered_map<in_addr_t, int> ip_connections;
}
