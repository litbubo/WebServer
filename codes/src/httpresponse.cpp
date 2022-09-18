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

/*
 * 构造函数，初始化变量
 */
HttpResponse::HttpResponse() : code_(-1), isKeepAlive_(false), mmFile_(nullptr), mmFileStat_{0}, path_(""), srcDir_("")
{
}

/*
 * 析构函数，取消文件映射区
 */
HttpResponse::~HttpResponse()
{
    this->unmapFile();
}

/*
 * 初始函数，设置资源目录，设置要返回的文件路径和长连接，以及返回码
 */
void HttpResponse::init(const std::string srcDir, const std::string &path, bool isKeepAlive, int code)
{
    assert(srcDir != "");
    if (mmFile_)
    {
        /* 先解除文件映射区 */
        unmapFile();
    }
    srcDir_ = srcDir;
    path_ = path;
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
}

/*
 * 制作返回http报文，存入buff
 */
void HttpResponse::makeResponse(Buffer &buff)
{
    /* 如果该文件获取不到文件信息或者是个文件夹，则返回404 找不到文件 */
    if (stat(std::string(srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode))
    {
        code_ = 404;
    }
    /* 无权限，返回403 */
    else if (!(mmFileStat_.st_mode & S_IROTH))
    {
        code_ = 403;
    }
    /* 正常 */
    else if (code_ == -1)
    {
        code_ = 200;
    }
    else
    {
    }
    /* 如果是400 403 404 则设置文件mmFileStat_为相应的html文件 */
    this->errorHtml();
    /* 制作状态行，返回头，和返回载荷长度 */
    this->addStateLine(buff);
    this->addHeader(buff);
    this->addContent(buff);
}

 
/*
 * 取消文件映射
 */
void HttpResponse::unmapFile()
{
    if (mmFile_)
    {
        munmap(mmFile_, mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

/*
 * 返回映射区首地址
 */
char *HttpResponse::file()
{
    return mmFile_;
}

/*
 * 返回映射区长度（文件长度）
 */
size_t HttpResponse::fileLen() const
{
    return mmFileStat_.st_size;
}

/*
 * 生成错误页面
 */
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

/*
 * 返回错误码
 */
int HttpResponse::code() const
{
    return code_;
}

/*
 * 200 400 403 404，向buff添加状态行
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

/*
 * 向buff添加状态头
 */
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

/*
 * 向buff添加http返回报文的载荷长度，
 * 同时进行文件私有映射
 */
void HttpResponse::addContent(Buffer &buff)
{
    /* 只读打开文件 */
    int srcfd = open(std::string(srcDir_ + path_).data(), O_RDONLY);
    if (srcfd < 0)
    {
        this->errorContent(buff, "File error");
        return;
    }
    LOG_DEBUG("file path %s%s", srcDir_.data(), path_.data());
    /* 创建文件私有映射区 */
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

/*
 * 设置mmFileStat_对应的400 403 404文件信息
 */
void HttpResponse::errorHtml()
{
    /* 403 404 400*/
    if (CODE_PATH_.count(code_) == 1)
    {
        path_ = CODE_PATH_.find(code_)->second;
        stat(std::string(srcDir_ + path_).data(), &mmFileStat_);
    }
}

/*
 * 返回http文件类型 
 */
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