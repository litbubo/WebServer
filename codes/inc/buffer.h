#pragma once

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <vector>

class Buffer
{
public:
    Buffer(size_t initBufferSize = 1024);
    ~Buffer() = default;

    size_t readableBytes() const;
    size_t writableBytes() const;
    size_t prependableBytes() const;

    const char *beginPtr() const;
    char *beginPtr();
    void makeSpace(size_t len);

    const char *peek() const;
    void ensurewritable(size_t len);
    void hasWritten(size_t len);

    void retrieve(size_t len);
    void retrieveUntil(const char *end);
    void retrieveAll();
    std::string retrieveAlltoString();

    const char *beginWriteConst() const;
    char *beginWrite();

    void append(const std::string &str);
    void append(const char *str, size_t len);
    void append(const void *data, size_t len);
    void append(const Buffer &buffer);

    ssize_t readFd(int fd, int *retError);
    ssize_t writeFd(int fd, int *retError);

private:
    std::vector<char> buffer_;
    std::atomic<std::size_t> readPos_;
    std::atomic<std::size_t> writePos_;
};
