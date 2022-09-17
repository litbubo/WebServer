#include <httpresponse.h>
#include <cassert>

/*
 * 静态变量，返回类型键值对
 */
const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE_ = {
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/msword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css "},
    {".js", "text/javascript "},
};

/*
 * 静态变量，状态码键值对
 */
const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS_ = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
};

/*
 * 静态变量，错误码与页面对应关系
 */
const std::unordered_map<int, std::string> HttpResponse::CODE_PATH_ = {
    {400, "/400.html"},
    {403, "/403.html"},
    {404, "/404.html"},
};

HttpResponse::HttpResponse() : code_(-1), isKeepAlive_(false), mmFile_(nullptr), mmFileStat_{0}, path_(""), srcDir_("")
{
}
HttpResponse::~HttpResponse()
{
    this->unmapFile();
}

void HttpResponse::init(const std::string srcDir, const std::string &path, bool isKeepAlive, int code)
{
    assert(srcDir != "");
    if (mmFile_)
    {
        unmapFile();
    }
    srcDir_ = srcDir;
    path_ = path;
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
}

void HttpResponse::makeResponse(Buffer &buff)
{
    if (stat(std::string(srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode))
    {
        code_ = 404;
    }
    else if (!(mmFileStat_.st_mode & S_IROTH))
    {
        code_ = 403;
    }
    else if (code_ == -1)
    {
        code_ = 200;
    }
    else
    {
    }
    this->errorHtml();
    this->addStateLine(buff);
    this->addHeader(buff);
    this->addContent(buff);
}

void HttpResponse::unmapFile()
{
    if (mmFile_)
    {
        munmap(mmFile_, mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

char *HttpResponse::file()
{
    return mmFile_;
}
size_t HttpResponse::fileLen() const
{
    return mmFileStat_.st_size;
}

void HttpResponse::errorContent(Buffer &buff, std::string message)
{
    std::string body;
    std::string status;
    body += R"(<html><title>Error</title>)";
    body += R"(<body bgcolor="FFFFFF">)";
    if (CODE_STATUS_.count(code_) == 1)
    {
        status = CODE_STATUS_.find(code_)->second;
    }
    else
    {
        status = "Bad Request";
    }
    body += std::to_string(code_) + " : " + status + "\n";
    body += "<p>" + message + "</p>";
    body += R"(<hr><em>WebServer</em></body></html>)";

    /*这里多了一个 \r\n 不是错误，这多的一个 \r\n 表示返回头后的必须的空行*/
    buff.append("Content-length: " + std::to_string(body.size()) + "\r\n");
    buff.append("\r\n");
    buff.append(body);
}

int HttpResponse::code() const
{
    return code_;
}

/*
 * 200 400 403 404
 */
void HttpResponse::addStateLine(Buffer &buff)
{
    std::string status;
    if (CODE_STATUS_.count(code_) == 1)
    {
        status = CODE_STATUS_.find(code_)->second;
    }
    else
    {
        code_ = 400;
        status = CODE_STATUS_.find(code_)->second;
    }
    buff.append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::addHeader(Buffer &buff)
{
    buff.append("Connection: ");
    if (isKeepAlive_)
    {
        buff.append("keep-alive\r\n");
        buff.append("keep-alive: max=6, timeout=120\r\n");
    }
    else
    {
        buff.append("close\r\n");
    }
    buff.append("Content-type: " + this->getFileType() + "\r\n");
}

void HttpResponse::addContent(Buffer &buff)
{
    int srcfd = open(std::string(srcDir_ + path_).data(), O_RDONLY);
    if (srcfd < 0)
    {
        this->errorContent(buff, "File error");
        return;
    }
    LOG_DEBUG("file path %s%s", srcDir_.data(), path_.data());
    auto mmRet = mmap(nullptr, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcfd, 0);
    if (mmRet == MAP_FAILED)
    {
        this->errorContent(buff, "File mmap error");
        return;
    }
    mmFile_ = static_cast<char *>(mmRet);
    close(srcfd);
    buff.append("Content-length: " + std::to_string(mmFileStat_.st_size) + "\r\n");
    buff.append("\r\n");
}

void HttpResponse::errorHtml()
{
    /* 403 404 400*/
    if (CODE_PATH_.count(code_) == 1)
    {
        path_ = CODE_PATH_.find(code_)->second;
        stat(std::string(srcDir_ + path_).data(), &mmFileStat_);
    }
}

std::string HttpResponse::getFileType()
{
    auto idx = path_.find_last_of(".");
    if (idx == std::string::npos)
    {
        return "text/plain";
    }
    std::string suffix = path_.substr(idx);
    if (SUFFIX_TYPE_.count(suffix) == 1)
    {
        return SUFFIX_TYPE_.find(suffix)->second;
    }
    return "text/plain";
}