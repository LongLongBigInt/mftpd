#pragma once

#include <cstddef>

template <typename T>
struct iobuf {
    T *base;
    size_t len;
    iobuf(T &obj): base(&obj), len(sizeof(T)) {}
    iobuf(T *base, size_t len): base(base), len(len) {}
    iobuf(T *begin, T *end): base(begin), len((end - begin) * sizeof(T)) {}
};
