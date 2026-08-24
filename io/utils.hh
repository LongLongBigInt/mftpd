#pragma once

#include "../sys/error.hh"

#include <fcntl.h>

int get_file_flags(int fd) {
    int _ = fcntl(fd, F_GETFL);
    if (_ == -1) THROW_LATEST;
    return _;
}

void set_file_flags(int fd, int val) {
    int _ = fcntl(fd, F_SETFL);
    if (_ == -1) THROW_LATEST;
}

void set_block(int fd) {

}

void set_nonblock(int fd) {
    
}