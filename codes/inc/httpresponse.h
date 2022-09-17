#pragma once

#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <log.h>
#include <buffer.h>

class HttpResponse
{
public:
    HttpResponse();
    ~HttpResponse();

    void init(const std::string srcDir, const std::string &path, bool isKeepAlive, int code = -1);
    void makeResponse(Buffer &buff);
    void unmapFile();
    char *file();
    size_t fileLen() const;
    void errorContent(Buffer &buff, std::string message);
    int code() const;

private:
    void addStateLine(Buffer &buff);
    void addHeader(Buffer &buff);
    void addContent(Buffer &buff);
    void errorHtml();
    std::string getFileType();

    int code_;
    bool isKeepAlive_;
    char *mmFile_;
    struct stat mmFileStat_;
    std::string path_;
    std::string srcDir_;

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE_;
    static const std::unordered_map<int, std::string> CODE_STATUS_;
    static const std::unordered_map<int, std::string> CODE_PATH_;
};