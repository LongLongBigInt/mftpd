#include "globals.hh"
#include "types.hh"
#include "message.hh"
#include "internal.hh"
#include "commands.hh"
#include "transfer.hh"

#include <csignal>

void on_new_connection() {
    // 这一步在linux中不会抛出或阻塞，除非EMFILE
    auto [conn, addr] = G::ctl.accept(SOCK_NONBLOCK);

    if (!G::cfg.ip_allowed(addr)) {
        return; // 或者发送RST
    }

    DEBUG("New connection from %s", addr.to_string().c_str());

    auto &c = *new connection(std::move(conn), addr);
    respond<ftpd_code::welcome>(c.stream);

    G::ep.add(c.stream, { epoll::in, 
        encode_ptr(handle_type::control_stream, &c)});
}

bool parse_and_eval_message(connection &c) {
    auto &[buf, end, skip] = c.parsing_state;

    ssize_t n = c.stream.recv_nothrow<char>({buf + end, std::end(buf)});
    if (n <= 0) { // eof or error
        // 尽管不太可能是EAGAIN，但这里还是给它显式处理了（视为false继续）
        return !(n == -1 && errno == EAGAIN);
    }
    end += n;

    int sep = -1, begin = 0;
    for (int i = 0; i < end; ++i) {
        if (buf[i] == ' ' && sep == -1) {
            sep = i;
        }
        if (i > 0 && buf[i-1] == '\r' && buf[i] == '\n') {
            if (skip) {
                respond<ftpd_code::syntax_error, 
                        syntax_error_variant::message_too_long>
                        (c.stream);
                skip = false;
            } else {
                if (cmd_dispatch(c, begin, sep, i-1)) {
                    return true;
                }
            }
            begin = i + 1;
            sep = -1;
        }
    }

    if (begin == 0 && end == FTPD_MAX_MSG_LEN) {
        skip = true;
        end = 0;
    } else if (begin != 0) {
        end -= begin;
        memmove(buf, buf + begin, end);
    }

    return false;
}

void on_control_message(connection &c) {
    // 需要退出
    if (!parse_and_eval_message(c)) {
        return; 
    }
    
    if (c.ef.valid()) {
        // 如果工作线程在负责数据传输，委托它进行异步销毁
        // 如果是QUIT命令返回true导致should_close，此时是没有数据连接的
        // 因此这个路径只在控制连接收到eof/error时才触发
        // 我们这里先关闭数据连接
        c.stream.close();
        c.wf.store(transfer_event::destroy);
        c.dstream.shutdown(SHUT_RDWR);
    } else {
        // 其余清理直接由析构函数进行
        delete &c;
    }
}

void on_data_message(connection &c, uint32_t ev) {
    // 这里可能之前处理了abort命令，我们已经把dstream关闭了，这是同一批的残留
    // 这里我们看到dstream失效后直接忽略
    if (!c.dstream.valid()) return;
    c.handler->handle(c);
}

void on_worker_event(connection &c) {
    transfer_event ev = c.wf.load();

    // 注意在worker线程的ef.set()可能还没返回
    // 这时efdwrite()系统调用会持有这个ef的引用
    // 导致随即的ef.close()不会完整释放ef，ef仍一直处于可读的状态
    // 这里我们显式将ef移出epoll
    // ef会在worker调用完efdwrite()后自动释放
    G::ep.del(c.ef);

    if (ev == transfer_event::destroy) {
        delete &c;
        return;
    }

    c.ef.close();
    complete_data_transfer(c, ev);
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

    start_data_transfer(c);
}

void on_PORT_connected(connection &c) {
    int err = c.dstream.get_error();
    // 不论成功与否，connector应该从事件循环中被移除
    G::ep.del(c.dstream);

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
    c.ts = transfer_state::idle;
}

int main(int argc, char const *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    const char *conf_path = "config.yaml.test";
    G::cfg.load(conf_path);

    INFO("Config loaded from %s", conf_path);

    G::ctl = sock<tcp, ip>::create()
        .bind_reuse({ G::cfg.port(), G::cfg.host() })
        .listen(FTPD_BACKLOG);
        
    INFO("Service listening on %s", G::ctl.addr().to_string().c_str());

    G::ep = epoll::create();

    G::ep.add(G::ctl, { epoll::in, 
        encode_ptr(control_acceptor, nullptr) });

    while (true) {
        G::skips.clear();

        for (auto [ev, data]: G::ep.wait()) {
            auto [type, connp] = decode_ptr(data);

            auto it = G::skips.find(connp);
            if (it != G::skips.end() && (it->second & (1 << type))) {
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
                    on_control_message(*connp);
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
                    on_worker_event(*connp);
                }
                break;
            }
        }
    }

    return 0;
}
