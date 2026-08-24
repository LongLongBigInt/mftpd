#pragma once

#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>

struct ip {
    in_port_t port;
    in_addr_t addr;

    using system_type = struct sockaddr_in;

    system_type to_system() {
        return {AF_INET, htons(port), {htonl(addr)}};
    }
    static ip from_system(system_type s) {
        return {ntohs(s.sin_port), ntohl(s.sin_addr.s_addr)};
    }
    std::string to_string() {
        char buf[INET_ADDRSTRLEN + 6];
        in_addr_t naddr = htonl(addr);
        inet_ntop(AF_INET, &naddr, buf, INET_ADDRSTRLEN);
        size_t len1 = strlen(buf);
        int len2 = sprintf(buf + len1 , ":%u", port);
        return {buf, len1 + len2};
    }

    enum: in_addr_t {
        any = 0x00000000,
        loopback = 0x7f000001,
    };
};
