#ifndef ROUTER_H
#define  ROUTER_H

#include "http.h"
#include <functional>
#include <unordered_map>

class Router
{
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    
    void get(const std::string& path, Handler handler);
    void post(const std::string& path, Handler handler);
    void put(const std::string& path, Handler handler);
    void del(const std::string& path, Handler handler);

    HttpResponse route(const HttpRequest& req);

private:
    std::unordered_map<std::string, Handler> routes_;

    void RegisterRoute(const std::string& method, const std::string& path, Handler handler);
};

#endif