#include <httprequest.h>

#include <regex>
#include <log.h>
#include <mysql/mysql.h>
#include <sqlconnRAII.hpp>
#include <sqlconnpool.h>

/*
 * 保存默认界面名字的静态变量，所有对以下界面的请求都会加上 .html 后缀
 */
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML_{
    "/index",
    "/register",
    "/login",
    "/welcome",
    "/video",
    "/picture",
};

/*
 * 静态变量，保存默认的HTML标签
 */
const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG_{
    {"/register.html", 0},
    {"/login.html", 1},
};

HttpRequest::HttpRequest()
{
    this->init();
}

/*
 * 构造函数
 */
void HttpRequest::init()
{
    method_ = "";
    path_ = "";
    version_ = "";
    body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

//  请求报文示例
//  GET /login HTTP/1.1\r\n
//  Host: 192.168.188.136:1316\r\n
//  Connection: keep-alive\r\n
//  Upgrade-Insecure-Requests: 1\r\n
//  User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36\r\n
//  Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9\r\n
//  Accept-Encoding: gzip, deflate\r\n
//  Accept-Language: zh-CN,zh;q=0.9\r\n
//  \r\n

/*
 * 状态机解析http报文，成功解析返回GET_REQUEST
 * 请求不完整或者无请求到来返回NO_REQUEST，上层会重新注册EPOLLIN事件
 * 其他错误返回相应错误码
 */
HttpRequest::HTTP_CODE HttpRequest::parse(Buffer &buff)
{
    /* 如果解析的buff为空，返回上层重新读取socket */
    if (buff.readableBytes() <= 0)
        return NO_REQUEST;

    const char CRLF[] = "\r\n";
    while (buff.readableBytes() && state_ != FINISH)
    {
        /* 用作取出一行字符串，line_end指向\r\n前 */
        const char *line_end = std::search(buff.peek(), buff.beginWriteConst(), CRLF, CRLF + 2);
        /* 如果直接指向buff中可读内容的结尾，同时状态还在HEADER，说明接受报文不完整，需要跳出重新接收报文 */
        if (line_end == buff.beginWriteConst() && state_ == HEADER)
            break;

        /* line为一行报文，不包括\r\n */
        std::string line = std::string(buff.peek(), line_end);
        switch (state_)
        {
        case REQUEST_LINE:
            /* 解析请求行 */
            if (this->parseRequestLine(line) == false)
            {
                /* 解析失败，返回损坏的请求 */
                return BAD_REQUEST;
            }
            /* 成功则解析请求的文件路径 */
            this->parsePath();
            break;
        case HEADER:
            /* 解析请求头 */
            this->parseHeader(line);
            /* 如果状态解析到BODY说明请求头解析完成，方法同时为GET，无需解析BODY，直接返回即可 */
            if (state_ == BODY && method_ == "GET")
            {
                /* 清空buff，返回获取到请求 */
                state_ = FINISH;
                buff.retrieveAll();
                return GET_REQUEST;
            }
            break;
        case BODY:
            /* 解析请求体失败，说明没接收完，返回NO_REQUEST上层继续读取 */
            if (this->parseBody(line) == false)
            {
                return NO_REQUEST;
            }
            buff.retrieveAll();
            return GET_REQUEST;
            break;
        default:
            return INTERNAL_ERROR;
            break;
        }
        /* 回收buff中内容，回收一行报文和"\r\n"，同时移动peek指针 */
        buff.retrieveUntil(line_end + 2);
    }
    LOG_DEBUG("%s, %s, %s", method_.data(), path_.data(), version_.data());
    return NO_REQUEST;
}

/*
 * 返回状态机状态
 */
HttpRequest::PARSE_STATE HttpRequest::state() const
{
    return state_;
}

/*
 * 返回请求路径
 */
std::string HttpRequest::path() const
{
    return path_;
}

/*
 * 返回请求路径
 */
std::string &HttpRequest::path()
{
    return path_;
}

/*
 * 返回请求方法
 */
std::string HttpRequest::method() const
{
    return method_;
}

/*
 * 返回http版本
 */
std::string HttpRequest::version() const
{
    return version_;
}

/*
 * 按照指定key获取对应内容
 */
std::string HttpRequest::getPost(const std::string &key) const
{
    assert(key != "");
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

/*
 * 按照指定key获取对应内容
 */
std::string HttpRequest::getPost(const char *key) const
{
    assert(key != nullptr);
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

/*
 * 返回是否长连接
 */
bool HttpRequest::iskeepAlive() const
{
    if (header_.count("Connection") == 1)
    {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

/*
 * 十六进制字符转换为int
 */
int HttpRequest::convertHex(char ch)
{
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    return ch - '0';
}

/*
 * 用户验证
 */
bool HttpRequest::userVerify(const std::string &name, const std::string &pwd, bool isLogin)
{
    if (name == "" || pwd == "")
        return false;
    LOG_INFO("Verify name == %s, pwd == %s", name.data(), pwd.data());

    /* RAII获取连接 */
    MYSQL *sql = nullptr;
    SqlConnRAII sqlConn(&sql, SqlConnPool::instance());
    assert(sql != nullptr);

    /* 按照用户名查询数据库 */
    MYSQL_RES *res = nullptr;
    char SQL_Qurey[1024] = {0};
    snprintf(SQL_Qurey, sizeof(SQL_Qurey) - 1, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.data());
    LOG_DEBUG("%s", SQL_Qurey);

    if (mysql_query(sql, SQL_Qurey))
    {
        return false;
    }
    res = mysql_store_result(sql);

    /* 从结果中取出一行遍历 */
    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        LOG_DEBUG("MYSQL name == %s, pwd == %s", row[0], row[1]);
        /* 是登录则验证是否账号密码正确 */
        if (isLogin)
        {
            std::string password(row[1]);
            if (password == pwd)
            {
                mysql_free_result(res);
                LOG_INFO("Login Success !!");
                return true;
            }
            else
            {
                mysql_free_result(res);
                LOG_INFO("Password error !!");
                return false;
            }
        }
        /* 不是登录则用户名已经被占用 */
        else
        {
            mysql_free_result(res);
            LOG_WARN("User has been exits !!");
            return false;
        }
    }

    /* 走到这里说明该用户名不在数据库中，若是注册行为则插入数据库 */
    if (isLogin == false)
    {
        LOG_DEBUG("regirster!");
        memset(SQL_Qurey, 0, sizeof(SQL_Qurey));
        snprintf(SQL_Qurey, sizeof(SQL_Qurey) - 1, "INSERT INTO user(username, password) VALUES('%s','%s')", name.data(), pwd.data());
        if (mysql_query(sql, SQL_Qurey))
        {
            LOG_DEBUG("INSET error");
            return false;
        }
        LOG_INFO("regirster success !!");
        return true;
    }
    /* 登录行为，用户不存在 */
    LOG_INFO("user not exits !!");
    return false;
}

/*
 * 请求头示例
 * POST / HTTP1.1
 * GET /1.jpg HTTP/1.1
 */
bool HttpRequest::parseRequestLine(const std::string &line)
{
    /*指定匹配规则，^表示行开始，$表示行尾，[^ ]表示匹配非空格,括号()括住的代表我们需要得到的字符串，最后会送入submatch中*/
    std::regex pattern("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch subMatch;
    if (std::regex_match(line, subMatch, pattern))
    {
        /* subMatch[0]为字符串原串 */
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADER;
        return true;
    }
    LOG_ERROR("Request line error ! %s", line.data());
    return false;
}

//  Host: 192.168.188.136:1316\r\n
//  Connection: keep-alive\r\n
//  Upgrade-Insecure-Requests: 1\r\n
//  User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36\r\n
//  Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9\r\n
//  Accept-Encoding: gzip, deflate\r\n
//  Accept-Language: zh-CN,zh;q=0.9\r\n
//  \r\n

/*
 * 解析请求头，按键值对存入header
 */
void HttpRequest::parseHeader(const std::string &line)
{
    std::regex pattern("^([^:]*): ?(.*)$");
    std::smatch subMatch;
    if (std::regex_match(line, subMatch, pattern))
    {
        header_[subMatch[1]] = subMatch[2];
    }
    else
    {
        state_ = BODY;
    }
}

/*
 * 解析post请求体
 */
bool HttpRequest::parseBody(const std::string &line)
{
    body_ = line;
    if (this->parsePost() == false)
    {
        return false;
    }
    state_ = FINISH;
    LOG_DEBUG("Body: %s, len: %d", line.data(), line.size());
    return true;
}

/*
 * 给html请求加上文件扩展名.html
 */
void HttpRequest::parsePath()
{
    if (path_ == "/")
    {
        path_ = "/index.html";
    }
    else
    {
        for (auto &item : DEFAULT_HTML_)
        {
            if (item == path_)
            {
                path_ += ".html";
                break;
            }
        }
    }
}

/*
 * 解析post 传递过来的内容，取出用户名密码及登陆验证等
 */
bool HttpRequest::parsePost()
{
    if (body_.size() < static_cast<size_t>(atol(header_.find("Content-Length")->second.data())))
    {
        return false;
    }
    if (method_ == "POST" && header_.find("Content-Type")->second == "application/x-www-form-urlencoded")
    {
        /* 将post请求体解析按键值对存入post */
        this->parseFromUrlencode();
        if (DEFAULT_HTML_TAG_.count(path_) == 1)
        {
            /* 判断是登陆还是注册 */
            bool isLogin = static_cast<bool>(DEFAULT_HTML_TAG_.find(path_)->second);
            if (this->userVerify(post_.find("username")->second, post_.find("password")->second, isLogin))
            {
                path_ = "/welcome.html";
            }
            else
            {
                path_ = "/error.html";
            }
        }
    }
    return true;
}

/*
 * 从body中提取登录注册信息
 *
 * HTML Form URL Encoded: application/x-www-form-urlencoded
 * username=xlp&password=123
 */
void HttpRequest::parseFromUrlencode()
{
    if (body_.size() == 0)
    {
        return;
    }
    std::string key;
    std::string value;
    std::string temp;
    int num = 0;
    int n = body_.size();

    for (int i = 0; i < n; i++)
    {
        char c = body_[i];
        switch (c)
        {
        case '=':
            /* =号前都是key */
            key = temp;
            temp.clear();
            break;
        case '+':
            /* 空格被转换为+号 */
            temp += ' ';
            break;
        case '&':
            /* &前面是用户名 */
            value = temp;
            temp.clear();
            post_[key] = value;
            break;
        case '%':
            /*浏览器会将非字母字母字符，encode成百分号+其ASCII码的十六进制*/
            /*%后面跟的是十六进制码,将十六进制转化为10进制*/
            num = this->convertHex(body_[i + 1]) * 16 + this->convertHex(body_[i + 2]);
            temp += static_cast<char>(num);
            i += 2;
            break;
        default:
            temp += c;
            break;
        }
    }
    /* 最后一个key和value存入post_ */
    if (post_.count(key) == 0)
    {
        value = temp;
        post_[key] = value;
    }
}