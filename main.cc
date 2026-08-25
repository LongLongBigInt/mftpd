#include "globals.hh"
#include "types.hh"
#include "message.hh"
#include "internal.hh"
#include "commands.hh"
#include "list.hh"

void on_new_connection() {
    auto [conn, addr] = G::ctl.accept();

    if (!G::cfg.ip_allowed(addr)) {
        return; // 或者发送RST
    }
    printf("New connection from %s\n", addr.to_string().c_str());

    int handle = conn.native_handle();
    respond<ftpd_code::welcome>(conn);

    G::ep.add(handle, {
        epoll::in,
        encode_ptr(
            handle_type::control_stream,
            new connection(std::move(conn), addr)
        )
    });
}

enum pem_result {
    ok, should_close, message_oversize
};

pem_result parse_and_eval_message(connection &c) {
    auto &[buf, end, skip] = c.parsing_state;

    size_t n = c.stream.recv<char>({buf + end, std::end(buf)});
    if (n == 0) { // eof
        return should_close;
    }
    end += n;

    int sep = -1, begin = 0;
    for (int i = 0; i < end; ++i) {
        if (buf[i] == ' ' && sep == -1) {
            sep = i;
        }
        if (i > 0 && buf[i-1] == '\r' && buf[i] == '\n') {
            if (skip) {
                skip = false;
            } else {
                if (cmd_dispatch(c, begin, sep, i-1)) {
                    return should_close;
                }
            }
            begin = i + 1;
            sep = -1;
        }
    }
    if (begin == 0 && end == FTPD_MAX_MSG_LEN) {
        skip = true;
        end = 0;
        return message_oversize;
    } else if (begin != 0) {
        end -= begin;
        memmove(buf, buf + begin, end);
    }

    return ok;
}

void on_control_message(connection *cp) {
    switch (parse_and_eval_message(*cp)) {
        case ok:
            break;
        case should_close:
            delete cp; break;
        case message_oversize:
            respond<ftpd_code::syntax_error, 
                    syntax_error_variant::message_too_long>
                    (cp->stream);
            break;
    }
}

template <typename handler>
void invoke_data_handler(connection &c) {
    auto hp = (handler *) c.handler;
    if (hp->handle(c, true)) delete hp;
}

void on_data_message(connection &c, uint32_t ev) {
    switch (c.dcmd) {
        case data_commands::list:
            if (ev & (epoll::out | epoll::error | epoll::hup)) {
                invoke_data_handler<LIST_handler>(c);
            }
            break;
        case data_commands::retrieve:
        case data_commands::store:
            break;
    }
}

void on_worker_event(connection &c) {
    complete_data_transfer(c, (transfer_event) c.ef.get(), false);
    c.ef.close();
    G::ep.dec();
}

void start_data_transfer(connection &c) {
    respond<ftpd_code::transfer_open>(
        c.stream, c.dpath.filename().c_str());

    switch (c.dcmd) {
        case data_commands::list:
            do_LIST_transfer(c);
            break;
        case data_commands::retrieve:
        case data_commands::store:
            break;
    }
}

void on_PASV_accepted(connection &c) {
    auto [conn, _] = c.dacceptor.accept();
    G::ep.dec();

    int handle = conn.native_handle();
    c.dstream = std::move(conn);
    c.dacceptor.close();

    start_data_transfer(c);
}

void on_PORT_connected(connection &c) {
    int err = c.dstream.get_error();
    G::ep.del(c.dstream.native_handle());

    if (err) {
        c.dstream.close();
        respond<ftpd_code::transfer_not_open,
                open_dconn_error_variant::socket_error>
                (c.stream, strerror(err));
        c.m = transfer_mode::unset;
        c.s = connection_state::auth_idle;
        return;
    }

    start_data_transfer(c);
}

int main(int argc, char const *argv[])
{
    const char *conf_path = "config.yaml.test";
    G::cfg.load(conf_path);

    printf("Config loaded from %s\n", conf_path);

    G::ctl = sock<tcp, ip>::create()
        .bind_reuse({
            G::cfg.port(), 
            G::cfg.host()
        })
        .listen(FTPD_BACKLOG);
        
    printf("Service listening on %s\n", 
           G::ctl.addr().to_string().c_str());

    G::ep = epoll::create();

    G::ep.add(G::ctl.native_handle(), 
             { epoll::in, encode_ptr(control_acceptor, nullptr) });

    while (true) {
        for (auto [ev, data]: G::ep.wait()) {
            auto [type, connp] = decode_ptr(data);

            switch (type) {

            case handle_type::control_acceptor:
                if (ev & epoll::in) {
                    on_new_connection();
                }
                break;

            case handle_type::control_stream:
                if (ev & epoll::in) {
                    on_control_message(connp);
                }
                break;
            
            case handle_type::data_acceptor:
                if (ev & (epoll::in | epoll::error | epoll::hup)) {
                    on_PASV_accepted(*connp);
                }
                break;

            case handle_type::data_connector:
                if (ev & (epoll::out | epoll::error | epoll::hup)) {
                    on_PORT_connected(*connp);
                }
                break;

            case handle_type::data_stream:
                on_data_message(*connp, ev);
                break;
            
            case handle_type::worker_event:
                if (ev & epoll::in) {
                    on_worker_event(*connp);
                }
                break;
            }
        }
    }

    return 0;
}
