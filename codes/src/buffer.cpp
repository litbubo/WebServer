#include <buffer.h>

Buffer::Buffer(size_t initBufferSize) : buffer_(initBufferSize), readPos_(0), writePos_(0)
{
    assert(initBufferSize > 0);
}

/*
 * 获取buffer中可读的字节数
 */
size_t Buffer::readableBytes() const
{
    return writePos_ - readPos_;
}

/*
 * 获取buffer中可写的字节数
 */
size_t Buffer::writableBytes() const
{
    return buffer_.size() - writePos_;
}

/*
 * 获取buffer中已经读的字节数（readpos位置）
 */
size_t Buffer::prependableBytes() const
{
    return readPos_;
}

/*
 * 获取buffer的首地址
 */
const char *Buffer::beginPtr() const
{
    return &*buffer_.begin();
}

char *Buffer::beginPtr()
{
    return &*buffer_.begin();
}

/*
 * 返回当前待读取数据位置的地址
 */
const char *Buffer::peek() const
{
    return this->beginPtr() + readPos_;
}

/*
 * 更新写数据指针
 */
void Buffer::hasWritten(size_t len)
{
    writePos_ += len;
}

/*
 * 移动读数据指针，表示读取数据
 */
void Buffer::retrieve(size_t len)
{
    assert(len <= this->readableBytes());
    readPos_ += len;
}

/*
 * 回收所有的buffer空间，表示数据全都被读走了
 */
void Buffer::retrieveAll()
{
    memset(&*buffer_.begin(), 0, buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

/*
 * 移动读取指针到end，表示到end的数据均已被读走
 */
void Buffer::retrieveUntil(const char *end)
{
    assert(this->peek() <= end);
    retrieve(end - this->peek());
}

/*
 *  读取所有的数据并转换为字符串
 */
std::string Buffer::retrieveAlltoString()
{
    std::string str = std::string(this->peek(), this->readableBytes());
    this->retrieveAll();
    return str;
}

/*
 * 获取可写空间的首地址
 */
const char *Buffer::beginWriteConst() const
{
    return this->beginPtr() + writePos_;
}

char *Buffer::beginWrite()
{
    return this->beginPtr() + writePos_;
}

/*
 * 向buffer中追加数据
 */
void Buffer::append(const char *str, size_t len)
{
    assert(str);
    this->ensurewritable(len);
    std::copy(str, str + len, this->beginWrite());
    this->hasWritten(len);
}

/*
 * 向buffer中追加数据
 */
void Buffer::append(const std::string &str)
{
    this->append(str.data(), str.length());
}

/*
 * 向buffer中追加数据
 */
void Buffer::append(const void *data, size_t len)
{
    this->append(static_cast<const char *>(data), len);
}

/*
 * 向buffer中追加数据
 */
void Buffer::append(const Buffer &buffer)
{
    this->append(buffer.peek(), buffer.readableBytes());
}

/*
 * 确保buffer可以写入len长度的内容，不足则自增长
 */
void Buffer::ensurewritable(size_t len)
{
    if (this->writableBytes() < len)
    {
        makeSpace(len);
    }
    assert(this->writableBytes() >= len);
}

/*
 * 调整buff空间，使其容纳的下新存入的内容
 */
void Buffer::makeSpace(size_t len)
{
    /* 如果可写的空间和已经读完的空间总和也小于需求len长度的空间，那就干脆直接重新设置vector大小 */
    if (this->writableBytes() + this->prependableBytes() < len)
    {
        buffer_.resize(writePos_ + len + 1);
    }
    /* 如果空闲空间大小足够，将vector中的未使用内容整体迁移至首部，留下空闲空间在后面待使用 */
    else
    {
        size_t readable = this->readableBytes();
        std::copy(this->beginPtr() + readPos_, this->beginPtr() + writePos_, this->beginPtr());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        /* 确保数据迁移正确 */
        assert(readable == this->readableBytes());
    }
}

/*
 * 从socket中读取数据存入到buff
 */
ssize_t Buffer::readFd(int fd, int *retError)
{
    /* 聚集读，尽量一次读完http请求报文，减少性能损耗 */
    char buff[1024 * 128];
    struct iovec iov[2] = {0};

    size_t writable = this->writableBytes();
    /* iov[0]指向vector iov[1]指向char buff */
    iov[0].iov_base = this->beginPtr() + writePos_;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    /* ET模式下，无数据可读数据返回-1，errno = EAGAIN */
    const ssize_t len = readv(fd, iov, sizeof(iov) / sizeof(struct iovec));
    if (len < 0)
    {
        *retError = errno;
    }
    else if (static_cast<size_t>(len) <= writable)
    {
        this->hasWritten(len);
    }
    else
    {
        writePos_ = buffer_.size();
        append(buff, len - writable);
    }
    return len;
}

/*
 * 向socket发送buff中的数据
 */
ssize_t Buffer::writeFd(int fd, int *retError)
{
    /* ET模式，写满缓冲区后，返回-1，errno = EAGAIN */
    const ssize_t len = write(fd, this->peek(), this->readableBytes());
    if (len < 0)
    {
        *retError = errno;
    }
    else
    {
        readPos_ += len;
    }
    return len;
}