#pragma once

#include "net/sockaddr.hh"
#include <filesystem>

#include <yaml-cpp/yaml.h>

#define FTPD_DEFAULT_HOME_PATH "/"
#define FTPD_DEFAULT_CONTROL_PORT 21

namespace fs = std::filesystem;

class config {
    YAML::Node root;
    fs::path default_home_;

public:
    void load(const char *path) {
        root = YAML::LoadFile(path);
    }
    
    YAML::Node users() {
        return root["users"];
    }

    in_addr_t host() {
        YAML::Node n = root["host"];
        if (!n) return ip::loopback;
        in_addr addr;
        int ok = inet_aton(n.Scalar().c_str(), &addr);
        if (!ok) return ip::loopback;
        return ntohl(addr.s_addr);
    }

    in_port_t port() {
        YAML::Node n = root["port"];
        if (!n) return FTPD_DEFAULT_CONTROL_PORT;
        return n.as<in_port_t>();
    }

    static fs::path get_dir(YAML::Node node) {
        if (!node) return {};
        fs::path p = node.Scalar();
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            return p;
        }
        // 路径不存在，或者不是目录
        return {};
    }

    fs::path &default_home() {
        if (default_home_.empty()) {
            default_home_ = get_dir(root["default-home"]);
            if (default_home_.empty()) {
                default_home_ = fs::path("/");
            }
        }
        return default_home_;
    }

    YAML::Node find_user(const char *name) {
        for (YAML::Node node: users()) {
            if (node["name"].Scalar() == name) {
                return node;
            }
        }
        return YAML::Node(YAML::NodeType::Undefined);
    }

    bool ip_allowed(ip addr) {
        return true;
    }

};