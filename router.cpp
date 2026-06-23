#include "router.h"

void Router::RegisterRoute(const std::string& method, const std::string& path, Handler handler)
{
    routes_[method + " " + path] = handler;
}

void Router::get(const std::string& path, Handler handler)
{
    RegisterRoute("GET", path, handler);
}

void Router::post(const std::string& path, Handler handler)
{
    RegisterRoute("POST", path, handler);
}

void Router::put(const std::string& path, Handler handler)
{
    RegisterRoute("PUT", path, handler);
}

void Router::del(const std::string& path, Handler handler)
{
    RegisterRoute("DELETE", path, handler);
}

HttpResponse Router::route(const HttpRequest& req)
{
    std::string key = req.method + " " + req.path;

    auto it = routes_.find(key);
    if (it != routes_.end())
    {
        return it->second(req);
    }

    // 支持简单的 ":param" 路由匹配，例如 "/static/:file"
    for (const auto& pair : routes_)
    {
        const std::string& pattern = pair.first; // 格式: "METHOD /path/with/:param"
        size_t sp = pattern.find(' ');
        if (sp == std::string::npos) continue;
        std::string pmethod = pattern.substr(0, sp);
        if (pmethod != req.method) continue;
        std::string ppath = pattern.substr(sp + 1);

        // split paths into segments
        auto split_segments = [](const std::string& s) {
            std::vector<std::string> segs;
            size_t pos = 0;
            if (!s.empty() && s[0] == '/') pos = 1;
            while (pos <= s.size()) {
                size_t next = s.find('/', pos);
                if (next == std::string::npos) next = s.size();
                std::string seg = s.substr(pos, next - pos);
                if (!seg.empty()) segs.push_back(seg);
                if (next == s.size()) break;
                pos = next + 1;
            }
            return segs;
        };

        std::vector<std::string> pseg = split_segments(ppath);
        std::vector<std::string> rseg = split_segments(req.path);
        if (pseg.size() != rseg.size()) continue;

        bool ok = true;
        for (size_t i = 0; i < pseg.size(); ++i)
        {
            const std::string& ps = pseg[i];
            const std::string& rs = rseg[i];
            if (!ps.empty() && ps[0] == ':')
            {
                // parameter segment, accept any
                continue;
            }
            if (ps != rs)
            {
                ok = false;
                break;
            }
        }
        if (ok)
        {
            return pair.second(req);
        }
    }

    HttpResponse res;
    res.SetStatus(404, "Not Found");
    res.body = "<html><body><h1>404 Not Found</h1></body></html>";
    return res;
}