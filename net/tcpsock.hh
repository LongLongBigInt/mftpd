#pragma once
#include "sockbase.hh"

template <typename Addr>
class sock<proto_variant::tcp, Addr>: public sock_base {
    sock(sock_base &&base): sock_base(std::move(base)) {}
public:
    sock() = default;
    static sock create() {
        return sock_base::create(AF_INET, SOCK_STREAM);
    }
    static sock create_nonblock() {
        return sock_base::create(AF_INET, SOCK_STREAM | SOCK_NONBLOCK);
    }
    void reuse_addr() {
        sock_base::set_option(SOL_SOCKET, SO_REUSEADDR, 1);
    }
    sock<proto_variant::tcp_bound, Addr> bind(Addr addr) && {
        sock_base::bind(addr);
        return std::move(*this);
    }
    auto bind_reuse(Addr addr) && {
        reuse_addr();
        return std::move(*this).bind(addr);
    }
    sock<proto_variant::tcp_connected, Addr> connect(Addr addr) && {
        sock_base::connect(addr);
        return std::move(*this);
    }
};

template <typename Addr>
class sock<proto_variant::tcp_bound, Addr>: public sock_base {
    friend class sock<proto_variant::tcp, Addr>;
    // only allow in sock<tcp>::bind
    sock(sock<proto_variant::tcp, Addr> &&bound): sock_base((sock_base &&)bound) {}
public:
    sock() = default;
    sock<proto_variant::tcp_listening, Addr> listen(int backlog) && {
        sock_base::listen(backlog);
        return std::move(*this);
    }
    sock<proto_variant::tcp_connected, Addr> connect(Addr addr) && {
        sock_base::connect(addr);
        return std::move(*this);
    }
    Addr addr() {
        return sock_base::addr<Addr>();
    }
};

template <typename Addr>
class sock<proto_variant::tcp_listening, Addr>: public sock_base {
    friend class sock<proto_variant::tcp_bound, Addr>;
    // only allow in sock<tcp_bound>::listen
    sock(sock<proto_variant::tcp_bound, Addr> &&listening): sock_base((sock_base &&)listening) {}
public:
    sock() = default;
    std::tuple<sock<proto_variant::tcp_connected, Addr>, Addr> accept(int flags = 0) {
        auto [fd, addr] = sock_base::accept<Addr>(flags);
        return {sock<proto_variant::tcp_connected, Addr>{fd}, addr};
    }
    Addr addr() {
        return sock_base::addr<Addr>();
    }
};

template <typename Addr>
class sock<proto_variant::tcp_connected, Addr>: public sock_base {
    friend class sock<proto_variant::tcp, Addr>;
    friend class sock<proto_variant::tcp_bound, Addr>;
    friend class sock<proto_variant::tcp_listening, Addr>;

    // allow in sock::connect
    template <proto_variant T>
    sock(sock<T, Addr> &&connected): sock_base((sock_base &&)connected) {}

public:
    sock() = default;
    
    // allow in sock<tcp_listening>::accept
    // 同时方便do_REIN
    sock(int fd) { set_handle(fd); }

    using sock_base::send;
    using sock_base::send_nothrow;
    using sock_base::recv;
    using sock_base::recv_nothrow;
    using sock_base::shutdown;

    Addr addr() {
        return sock_base::addr<Addr>();
    }
    Addr peer_addr() {
        return sock_base::peer_addr<Addr>();
    }
};
