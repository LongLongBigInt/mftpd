#include "net/tcpsock.hh"
#include "net/sockaddr.hh"

#include "globals.hh"
#include "evloop.hh"

int main(int argc, char const *argv[])
{
    const char *conf_path = "config.yaml.template";
    globals::cfg.load(conf_path);

    printf("config loaded from %s\n", conf_path);

    const in_port_t controlling_port = 2121;
    globals::ctl = sock<tcp, ip>::create()
        .bind_reuse({controlling_port, ip::loopback})
        .listen(FTPD_BACKLOG);
        
    printf("service listening port %d\n", controlling_port);

    globals::ep.emplace();
    globals::ep->add(
        globals::ctl.native_handle(), 
        { epoll::in, { .u64 = 0x0 } }
    );

    run_eventloop();

    return 0;
}
