#pragma once

#include <yaml-cpp/yaml.h>

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

const char *default_home_path = "/";

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

    static fs::path get_dir(YAML::Node node) {
        if (!node) return {};
        fs::path p = node.Scalar();
        std::error_code ec;
        // The requested access to the file is not allowed, 
        // OR search permission is denied for one of the directories in the path prefix of pathname,
        // OR the file did not exist yet and write access to the parent directory is not allowed.
        if (fs::is_directory(p, ec)) {
            return p;
        }
        // warning: path non exist not a dir, ignore
        return {};
    }

    fs::path &default_home() {
        if (default_home_.empty()) {
            default_home_ = get_dir(root["default-home"]);
            if (default_home_.empty()) {
                default_home_ = fs::path(default_home_path);
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

};