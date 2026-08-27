#include "globals.hh"
#include "types.hh"
#include "message.hh"
#include "internal.hh"
#include "commands.hh"
#include "list.hh"

#include <unordered_set>

void on_new_connection() {
    // 这一步在linux中不会抛出或阻塞，除非EMFILE
    auto [conn, addr] = G::ctl.accept();

    if (!G::cfg.ip_allowed(addr)) {
        return; // 或者发送RST
    }

    DEBUG("New connection from %s", addr.to_string().c_str());

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

enum PEM_result {
    ok, should_close, message_oversize
};

PEM_result parse_and_eval_message(connection &c) {
    auto &[buf, end, skip] = c.parsing_state;

    ssize_t n = c.stream.recv_nothrow<char>({buf + end, std::end(buf)});
    if (n <= 0) { // eof or error
        return should_close;
    }
    // ready_to_close状态下读端已关闭，可能存在残留消息，需要忽略
    if (c.s == connection_state::ready_to_close) {
        return ok;
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

void on_control_message(connection &c, std::unordered_set<connection *> &skips) {
    switch (parse_and_eval_message(c)) {
        case ok:
            break;

        case should_close:
            if (c.ef.valid()) {
                // 如果工作线程在负责数据传输，委托它进行异步销毁
                // 如果是QUIT命令返回true导致should_close，此时是没有数据连接的
                // 因此这个路径只在控制连接收到eof/error时才触发
                // 这里无条件设置c.wf = destroy，即使覆盖了工作线程设置的error也没关系
                // 因为error已经没意义了
                c.wf.store(transfer_event::destroy);
            } else {
                // 防止同一批次稍后的连接或者worker有消息，我们在这里mask一下
                skips.insert(&c);
                // 不管当前状态是什么（dacceptor或者dconnector怎么样），析构函数总能正确处理
                delete &c;
            }
            break;

        case message_oversize:
            respond<ftpd_code::syntax_error, 
                    syntax_error_variant::message_too_long>
                    (c.stream);
            break;
    }
}

template <typename handler>
void invoke_data_handler(connection &c) {
    auto hp = (handler *) c.handler;
    if (hp->handle(c, true)) delete hp;
}

void on_data_message(connection &c, uint32_t ev) {
    // 这里可能之前处理了abort命令，我们已经把dstream关闭了，这是同一批的残留
    // 这里我们看到dstream失效后直接忽略
    if (!c.dstream.valid()) return;

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

void on_worker_event(connection &c, std::unordered_set<connection *> &skips) {
    transfer_event ev = c.wf.load();

    if (ev == transfer_event::destroy) {
        skips.insert(&c);
        delete &c;
        return;
    }

    complete_data_transfer(c, ev, false);

    c.ef.close();
    G::ep.dec();
}

void on_PASV_accepted(connection &c) {
    auto [conn, addr] = c.dacceptor.accept();

    // TODO: 检验 addr 是否接受
    // 这里暂时写成与控制连接的ip是否一致
    if (addr.addr != c.addr.addr) {
        return;
    }

    // 注意这里新连接还不添加到事件循环中，下同
    // 是否添加取决于handler的策略
    c.dstream = std::move(conn);
    c.daddr = addr;

    c.dacceptor.close();
    G::ep.dec();

    start_data_transfer(c);
}

void on_PORT_connected(connection &c) {
    int err = c.dstream.get_error();
    // 不论成功与否，connector应该从事件循环中被移除
    G::ep.del(c.dstream.native_handle());

    if (!err) {
        start_data_transfer(c);
        return;
    }

    // 连接失败
    c.dstream.close();
    respond<ftpd_code::transfer_not_open,
            transfer_not_open_variant::socket_error>
            (c.stream, strerror(err));

    c.m = transfer_mode::unset;
    c.s = connection_state::idle;
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
        std::unordered_set<connection *> skips;

        for (auto [ev, data]: G::ep.wait()) {
            auto [type, connp] = decode_ptr(data);

            if (!skips.empty() && skips.count(connp)) {
                continue;
            }

            switch (type) {

            case handle_type::control_acceptor:
                if (ev & epoll::in) {
                    on_new_connection();
                }
                break;

            case handle_type::control_stream:
                if (ev & epoll::in) {
                    on_control_message(*connp, skips);
                }
                break;
            
            case handle_type::data_acceptor:
                if (ev & epoll::in) {
                    on_PASV_accepted(*connp);
                }
                break;

            case handle_type::data_connector:
                if (ev & epoll::out) {
                    on_PORT_connected(*connp);
                }
                break;

            case handle_type::data_stream:
                on_data_message(*connp, ev);
                break;
            
            case handle_type::worker_event:
                if (ev & epoll::in) {
                    on_worker_event(*connp, skips);
                }
                break;
            }
        }
    }

    return 0;
}
