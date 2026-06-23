#ifndef HTTTP_H
#define HTTP_H

#include <sys/types.h>
#include <string>
#include <unordered_map>

class HttpRequest
{
public:
    std::string method;
    std::string path;
    std::string query_string;
    std::string http_version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    bool parse(const std::string& data);
};

class HttpResponse
{
public:
    int status_code;
    std::string status_message;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    int file_fd;
    off_t file_offset;
    size_t file_size;

    HttpResponse();
    ~HttpResponse();
    HttpResponse(const HttpResponse&) = delete;
    HttpResponse& operator=(const HttpResponse&) = delete;
    HttpResponse(HttpResponse&& other) noexcept;
    HttpResponse& operator=(HttpResponse&& other) noexcept;

    std::string to_string() const;

    void SetContentType(const std::string& content_type);
    void SetStatus(int code, const std::string& message);
};

#endif