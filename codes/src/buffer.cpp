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
    return std::string(this->peek(), this->readableBytes());
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

void Buffer::append(const std::string &str)
{
    this->append(str.data(), str.length());
}

void Buffer::append(const void *data, size_t len)
{
    this->append(static_cast<const char *>(data), len);
}

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
 *
 */
void Buffer::makeSpace(size_t len)
{
    if (this->writableBytes() + this->prependableBytes() < len)
    {
        buffer_.resize(writePos_ + len + 1);
    }
    else
    {
        size_t readable = this->readableBytes();
        std::copy(this->beginPtr() + readPos_, this->beginPtr() + writePos_, this->beginPtr());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == this->readableBytes());
    }
}

/*
 *
 */
ssize_t Buffer::readFd(int fd, int *retError)
{
    return 0;
}

/*
 *
 */
ssize_t Buffer::writeFd(int fd, int *retError)
{
    return 0;
}