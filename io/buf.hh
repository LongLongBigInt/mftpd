#pragma once

#include <cstddef>

template <typename T>
struct iobuf {
    T *base, *end;
    size_t len() { return end - base; }
    size_t bytes() { return (char *) end - (char *) base; }
    iobuf(T &obj): base(&obj), end(&obj + 1) {}
    iobuf(T *base, size_t len): base(base), end(base + len) {}
    iobuf(T *begin, T *end): base(begin), end(end) {}
};
