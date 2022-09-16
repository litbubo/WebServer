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

HttpRequest::HTTP_CODE HttpRequest::parse(Buffer &buff)
{
}

HttpRequest::PARSE_STATE HttpRequest::state() const
{
    return state_;
}

std::string HttpRequest::path() const
{
    return path_;
}

std::string &HttpRequest::path()
{
    return path_;
}

std::string HttpRequest::method() const
{
    return method_;
}

std::string HttpRequest::version() const
{
    return version_;
}

std::string HttpRequest::getPost(const std::string &key) const
{
    assert(key != "");
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::getPost(const char *key) const
{
    assert(key != nullptr);
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

bool HttpRequest::iskeepAlive() const
{
    if (header_.count("Connection") == 1)
    {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

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

bool HttpRequest::userVerify(const std::string &name, const std::string &pwd, bool isLogin)
{
    if (name == "" || pwd == "")
        return false;
    LOG_INFO("Verify name == %s, pwd == %s", name.data(), pwd.data());
    MYSQL *sql = nullptr;
    SqlConnRAII sqlConn(&sql, SqlConnPool::instance());
    assert(sql != nullptr);

    MYSQL_RES *res = nullptr;
    char SQL_Qurey[1024] = {0};
    snprintf(SQL_Qurey, sizeof(SQL_Qurey) - 1, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.data());
    LOG_DEBUG("%s", SQL_Qurey);

    if (mysql_query(sql, SQL_Qurey))
    {
        return false;
    }
    res = mysql_store_result(sql);
    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        LOG_DEBUG("MYSQL name == %s, pwd == %s", row[0], row[1]);
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
        else
        {
            mysql_free_result(res);
            LOG_WARN("User has been exits !!");
            return false;
        }
    }

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
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADER;
        return true;
    }
    LOG_ERROR("Request line error ! %s", line.data());
    return false;
}

/*
 *   Connection: keep-alive\r\n
 *   User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36 Edg/105.0.1343.33\r\n
 *   Accept: image/webp,image/apng,image/svg+xml;q=0.8\r\n
 *   Referer: http://www.baidu.com/\r\n
 *   Accept-Encoding: gzip, deflate\r\n
 *   Accept-Language: zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6\r\n
 *   \r\n
 */
bool HttpRequest::parseHeader(const std::string &line)
{
    std::regex pattern("^([^:]*): ?(.*)$");
    std::smatch subMatch;
    if (std::regex_match(line, subMatch, pattern))
    {
        header_[subMatch[1]] = subMatch[2];
        return true;
    }
    else
    {
        state_ = BODY;
        return true;
    }
    return false;
}

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
bool HttpRequest::parsePath()
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

bool HttpRequest::parsePost()
{
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
    if (post_.count(key) == 0)
    {
        value = temp;
        post_[key] = value;
    }
}