#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <buffer.h>

class HttpRequest
{
public:
    enum PARSE_STATE
    {
        REQUEST_LINE,
        HEADER,
        BODY,
        FINISH,
    };

    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

    HttpRequest();
    ~HttpRequest() = default;

    void init();
    HTTP_CODE parse(Buffer &buff);
    PARSE_STATE state() const;
    std::string path() const;
    std::string &path();
    std::string method() const;
    std::string version() const;
    std::string getPost(const std::string &key) const;
    std::string getPost(const char *key) const;
    bool iskeepAlive() const;

private:
    static int convertHex(char ch);

    static bool userVerify(const std::string &name, const std::string &pwd, bool isLogin);

    bool parseRequestLine(const std::string &line);
    bool parseBody(const std::string &line);
    bool parsePost();
    void parseHeader(const std::string &line);
    void parsePath();
    void parseFromUrlencode();

    PARSE_STATE state_;
    std::string method_;
    std::string path_;
    std::string version_;
    std::string body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;

    static const std::unordered_set<std::string> DEFAULT_HTML_;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG_;
};