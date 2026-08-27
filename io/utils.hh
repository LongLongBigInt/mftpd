#pragma once

#include "../sys/error.hh"
#include "handle.hh"

#include <fcntl.h>

int get_file_flags(iohandle &h) {
    int _ = fcntl(h.native_handle(), F_GETFL);
    if (_ == -1) THROW_LATEST;
    return _;
}

void set_file_flags(iohandle &h, int val) {
    int _ = fcntl(h.native_handle(), F_SETFL, val);
    if (_ == -1) THROW_LATEST;
}

void set_block(iohandle &h) {
    int flags = get_file_flags(h);
    if (flags & O_NONBLOCK) {
        set_file_flags(h, flags & ~O_NONBLOCK);
    }
}

void set_nonblock(iohandle &h) {
    int flags = get_file_flags(h);
    if (!(flags & O_NONBLOCK)) {
        set_file_flags(h, flags | O_NONBLOCK);
    }
}
