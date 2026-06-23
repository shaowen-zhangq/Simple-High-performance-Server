#include "http.h"
#include <unistd.h>

bool HttpRequest::parse(const std::string& data)
{
    size_t pos = 0;
    size_t end_pos = data.find("\r\n", pos);
    if(end_pos == std::string::npos)
    {
        return false;
    }

    std::string request_line = data.substr(pos, end_pos - pos);
    pos = end_pos + 2;

    size_t method_end = request_line.find(' ');
    if(method_end == std::string::npos)
    {
        return false;
    }
    method = request_line.substr(0, method_end);

    size_t path_end = request_line.find(' ', method_end + 1);
    if(path_end == std::string::npos)
    {
        return false;
    }
    std::string full_path = request_line.substr(method_end + 1, path_end - method_end - 1);
    
    size_t query_pos = full_path.find('?');
    if(query_pos != std::string::npos)
    {
        path = full_path.substr(0, query_pos);
        query_string = full_path.substr(query_pos + 1);
    }
    else
    {
        path = full_path;
    }

    http_version = request_line.substr(path_end + 1);

    while (pos < data.size())
    {
        end_pos = data.find("\r\n", pos);
        if(end_pos == std::string::npos)
        {
            break;
        }
        std::string line = data.substr(pos, end_pos - pos);
        pos = end_pos + 2;

        if(line.empty())
        {
            break;
        }

        size_t colon_pos = line.find(':');
        if(colon_pos != std::string::npos)
        {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            size_t value_start = value.find_first_not_of(" \t");
            if(value_start != std::string::npos)
            {
                value = value.substr(value_start);
            }
            headers[key] = value;
        }
    }

    if(pos < data.size())
    {
        body = data.substr(pos);
    }

    return true;
}

HttpResponse::HttpResponse()
    : status_code(200), status_message("OK"), file_fd(-1), file_offset(0), file_size(0) {}

HttpResponse::~HttpResponse()
{
    if (file_fd >= 0)
    {
        close(file_fd);
    }
}

HttpResponse::HttpResponse(HttpResponse&& other) noexcept
    : status_code(other.status_code), status_message(std::move(other.status_message)), headers(std::move(other.headers)), body(std::move(other.body)), file_fd(other.file_fd), file_offset(other.file_offset), file_size(other.file_size)
{
    other.file_fd = -1;
    other.file_offset = 0;
    other.file_size = 0;
}

HttpResponse& HttpResponse::operator=(HttpResponse&& other) noexcept
{
    if (this != &other)
    {
        if (file_fd >= 0)
        {
            close(file_fd);
        }
        status_code = other.status_code;
        status_message = std::move(other.status_message);
        headers = std::move(other.headers);
        body = std::move(other.body);
        file_fd = other.file_fd;
        file_offset = other.file_offset;
        file_size = other.file_size;
        other.file_fd = -1;
        other.file_offset = 0;
        other.file_size = 0;
    }
    return *this;
}

void HttpResponse::SetStatus(int code, const std::string& message)
{
    status_code = code;
    status_message = message;
}

void HttpResponse::SetContentType(const std::string& content_type)
{
    headers["Content-Type"] = content_type;
}

std::string HttpResponse::to_string() const
{
    std::string res;
    res += "HTTP/1.1 " + std::to_string(status_code) + " " + status_message + "\r\n";
    if (headers.find("Content-Length") == headers.end())
    {
        res += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    for(const auto& pair : headers)
    {
        res += pair.first + ": " + pair.second + "\r\n";
    }
    res += "\r\n";
    res += body;
    return res;
}