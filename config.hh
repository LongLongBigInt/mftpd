#pragma once

#include "net/sockaddr.hh"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

#define FTPD_MAX_MSG_LEN 4096
#define FTPD_MAX_RES_LEN 4096
#define FTPD_BACKLOG 100
#define FTPD_DEFAULT_HOME_PATH "/"
#define FTPD_DEFAULT_CONFIG_PATH "/etc/ftpd/config.yaml"
#define FTPD_DEFAULT_HOST ip::loopback
#define FTPD_DEFAULT_CONTROL_PORT 21
#define FTPD_DEFAULT_MAX_CONNECTIONS 4096
#define FTPD_DEFAULT_MAX_CONNECTIONS_PER_IP 0
#define FTPD_DEFAULT_IDLE_TIMEOUT 300

#define FTPD_DEFAULT_WELCOME_MESSAGE \
    "Service ready for new user."
#define FTPD_DEFAULT_BYE_MESSAGE \
    "Bye."

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

bool str2ip(const char *str, in_addr_t &addr) {
    in_addr a;
    bool ok = inet_aton(str, &a) == 1;
    if (!ok) return false;
    addr = ntohl(a.s_addr);
    return true;
}

bool parse_CIDR(char *CIDR_str, in_addr_t &addr, in_addr_t &mask) {
    char *slash = (char *) strchr(CIDR_str, '/');
    if (slash) *slash = '\0';
    if (!str2ip(CIDR_str, addr)) return false;
    int prefix = slash ? atoi(slash + 1) : 32;
    if (prefix <= 0 || prefix > 32) return false;
    mask = -1u << (32 - prefix);
    return true;
}

struct config
{
    in_addr_t host = 
        FTPD_DEFAULT_HOST;

    in_port_t port = 
        FTPD_DEFAULT_CONTROL_PORT;

    in_addr_t pasv_ip;

    in_port_t pasv_port = 0;

    int max_connections = 
        FTPD_DEFAULT_MAX_CONNECTIONS;

    int max_connections_per_ip = 
        FTPD_DEFAULT_MAX_CONNECTIONS_PER_IP;

    int idle_timeout = 
        FTPD_DEFAULT_IDLE_TIMEOUT;

    fs::path default_home = 
        FTPD_DEFAULT_HOME_PATH;

    const char *welcome_message = 
        FTPD_DEFAULT_WELCOME_MESSAGE;

    const char *bye_message = 
        FTPD_DEFAULT_BYE_MESSAGE;

    void load(const char *path) {
        root = YAML::LoadFile(path);

        // 加载配置字段
        load_ip_value("host", host);
        load_value("port", port);
        load_value("welcome-message", welcome_message);
        load_value("bye-message", bye_message);
        if (!load_ip_value("pasv-ip", pasv_ip)) pasv_ip = host;
        load_value("pasv-port", pasv_port);
        load_value("max-connections", max_connections);
        load_value("max-connections-per-ip", max_connections_per_ip);
        load_value("idle-timeout", idle_timeout);
        load_value("default-home", default_home);
        load_allowed_ip();
    }

    YAML::Node find_user(const char *name) {
        for (YAML::Node node: root["users"]) {
            if (node["name"].Scalar() == name) {
                return node;
            }
        }
        return YAML::Node(YAML::NodeType::Undefined);
    }

    bool ip_allowed(ip addr) {
        if (allowed_ips.empty()) return true;
        for (auto [base, mask]: allowed_ips) {
            if ((addr.addr & mask) == base) return true;
        }
        return false;
    }

    bool ip_acceptable(ip addr) {
        return max_connections_per_ip != 0 && 
            G::ip_connections[addr.addr] >= max_connections_per_ip;
    }

    static bool get_dir(YAML::Node node, fs::path &path) {
        if (!node) return false;
        std::error_code ec;
        bool ok = fs::is_directory(node.Scalar(), ec);
        if (!ok) {
            return false;
        }
        path = node.Scalar();
        return true;
    }

private:
    YAML::Node root;
    std::vector<std::pair<in_addr_t, in_addr_t>> allowed_ips;

    bool load_value_(const char *key, auto &dest, auto &&try_load) {
        YAML::Node n = root[key];
        if (!n) return true;
        if (try_load(n, dest)) return true;
        WARNING("config: bad '%s' value '%s'", key, n.Scalar().c_str());
        return false;
    }

    template <typename T>
    bool load_value(const char *key, T &val) {
        return load_value_(key, val, YAML::convert<T>::decode);
    }

    bool load_value(const char *key, const char * &dest) {
        return load_value_(key, dest, [](auto &&...) { return true; });
    }

    bool load_value(const char *key, fs::path &path) {
        return load_value_(key, path, get_dir);
    }

    bool load_ip_value(const char *key, in_addr_t &addr) {
        return load_value_(key, addr, [](YAML::Node node, in_addr_t &addr) {
            return str2ip(node.Scalar().c_str(), addr);
        });
    }

    void load_allowed_ip() {
        allowed_ips.clear();
        for (YAML::Node node: root["allowed-ips"]) {
            in_addr_t addr, mask;
            if (parse_CIDR((char *) node.Scalar().c_str(), addr, mask)) {
                allowed_ips.push_back({addr, mask});
            } else {
                WARNING("config: bad 'allowed-ips' entry '%s'",
                        node.Scalar().c_str());
            }
        }
    }
};
