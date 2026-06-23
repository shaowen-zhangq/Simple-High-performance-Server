#include "router.h"
#include "server.h"
#include "logger.h"
#include "threadpool.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <functional>
#include <ctime>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

std::unordered_map<int, Conn*> conn_map;
std::mutex conn_mtx;
std::atomic<bool> timer_stop = false;
std::atomic<bool> server_stop = false;

void handle_signal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        LOG_INFO("收到终止信号，服务器即将退出");
        server_stop = true;
    }
}

void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    
    int quickack = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
}

std::string get_content_type(const std::string& filename)
{
    static const std::unordered_map<std::string, std::string> mime_types = {
        {"html", "text/html"}, {"htm", "text/html"},
        {"css", "text/css"},
        {"js", "application/javascript"},
        {"json", "application/json"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"pdf", "application/pdf"}
    };
    
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos)
    {
        return "text/plain";
    }
    
    std::string ext = filename.substr(dot_pos + 1);
    auto it = mime_types.find(ext);
    return it != mime_types.end() ? it->second : "text/plain";
}

HttpResponse serve_static_file(const std::string& file_path)
{
    HttpResponse res;
    
    int fd = open(file_path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        res.SetStatus(404, "Not Found");
        res.body = "<html><body><h1>404 File Not Found</h1></body></html>";
        return res;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        close(fd);
        res.SetStatus(500, "Internal Server Error");
        res.body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
        return res;
    }
    
    off_t size = st.st_size;
    std::string content(size, '\0');
    read(fd, &content[0], size);
    close(fd);
    
    res.SetStatus(200, "OK");
    res.SetContentType(get_content_type(file_path));
    res.headers["Content-Length"] = std::to_string(size);
    res.headers["Cache-Control"] = "max-age=3600";
    res.body = content;
    
    return res;
}

void refresh_timer(Conn* conn)
{
    conn->active_time.store(time(nullptr));
}

void check_timeout(int epoll_fd)
{
    time_t now = time(nullptr);
    std::vector<int> timeout_fds;
    std::vector<Conn*> timeout_conns;
    
    {
        std::lock_guard<std::mutex> lock(conn_mtx);
        auto it = conn_map.begin();
        while (it != conn_map.end())
        {
            Conn* conn = it->second;
            if (now - conn->active_time.load() > IDLE_TIMEOUT)
            {
                timeout_fds.push_back(conn->fd);
                timeout_conns.push_back(conn);
                it = conn_map.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    
    for (size_t i = 0; i < timeout_fds.size(); ++i)
    {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, timeout_fds[i], nullptr);
        close(timeout_fds[i]);
        delete timeout_conns[i];
    }
}

Router router;

void handle_client_request(int fd, int epoll_fd, const std::string& request)
{
    HttpRequest req;
    HttpResponse res;
    
    if (req.parse(request))
    {
        res = router.route(req);
    }
    else
    {
        res.SetStatus(400, "Bad Request");
        res.body = "<html><body><h1>400 Bad Request</h1></body></html>";
    }
    
    std::string response = res.to_string();
    const char* ptr = response.data();
    size_t remaining = response.size();
    
    while (remaining > 0)
    {
        ssize_t n_write = write(fd, ptr, remaining);
        if (n_write > 0)
        {
            ptr += n_write;
            remaining -= n_write;
        }
        else if (n_write < 0 && errno == EAGAIN)
        {
            struct timespec ts = {0, 100000};
            nanosleep(&ts, nullptr);
            continue;
        }
        else
        {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            {
                std::lock_guard<std::mutex> lock(conn_mtx);
                auto it = conn_map.find(fd);
                if (it != conn_map.end())
                {
                    delete it->second;
                    conn_map.erase(it);
                }
            }
            return;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(conn_mtx);
        auto it = conn_map.find(fd);
        if (it != conn_map.end())
        {
            it->second->active_time.store(time(nullptr));
        }
    }
    
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

int main()
{
    Logger::GetInstance().SetFile("log.txt");
    Logger::GetInstance().SetLevel(LOG_INFO);
    LOG_INFO("HTTP Web Server 启动，监听端口 5005");
    
    size_t thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) thread_count = 4;
    LOG_INFO("线程池大小: %zu", thread_count);
    
    ThreadPool pool(thread_count);
    
    router.get("/", [](const HttpRequest& req) {
        (void) req;
        HttpResponse res;
        res.body = "<html><head><title>My Web Server</title></head>"
                   "<body><h1>Welcome to My HTTP Web Server!</h1>"
                   "<p>Built with C++17, epoll, and Thread Pool</p>"
                   "<p>Try: <a href='/api/users'>/api/users</a></p>"
                   "<p>Try: <a href='/static/test.html'>/static/test.html</a></p>"
                   "</body></html>";
        res.SetContentType("text/html");
        return res;
    });
    
    router.get("/api/users", [](const HttpRequest& req) {
        (void)req;
        HttpResponse res;
        res.body = R"({"users": [{"id": 1, "name": "Alice"}, {"id": 2, "name": "Bob"}]})";
        res.SetContentType("application/json");
        return res;
    });
    
    router.get("/api/users/1", [](const HttpRequest& req) {
        (void) req;
        HttpResponse res;
        res.body = R"({"id": 1, "name": "Alice", "email": "alice@example.com"})";
        res.SetContentType("application/json");
        return res;
    });
    
    router.post("/api/users", [](const HttpRequest& req) {
        (void)req;
        HttpResponse res;
        res.SetStatus(201, "Created");
        res.body = R"({"id": 3, "name": "New User"})";
        res.SetContentType("application/json");
        return res;
    });
    
    router.get("/static/:file", [](const HttpRequest& req) {
        std::string file_path = "www/" + req.path.substr(8);
        return serve_static_file(file_path);
    });
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd < 0)
    {
        LOG_ERROR("socket 创建失败");
        return -1;
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    int buffer_size = 65536;
    setsockopt(listen_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(listen_fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5005);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR("bind 失败");
        close(listen_fd);
        return -1;
    }
    if (listen(listen_fd, 4096) < 0)
    {
        LOG_ERROR("listen 失败");
        close(listen_fd);
        return -1;
    }
    
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        LOG_ERROR("epoll_create 创建失败");
        close(listen_fd);
        return -1;
    }
    
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0)
    {
        LOG_ERROR("epoll_ctl 添加监听fd失败");
        close(epoll_fd);
        close(listen_fd);
        return -1;
    }
    
    std::thread timer_thread([epoll_fd]() {
        while (!timer_stop)
        {
            sleep(5);
            check_timeout(epoll_fd);
        }
        LOG_INFO("定时器线程退出");
    });
    
    std::vector<epoll_event> events(MAX_EVENTS);
    
    char buffer[BUF_SIZE];
    
    while (!server_stop)
    {
        int n_ready = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, 1000);
        if (n_ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            LOG_ERROR("epoll_wait 失败");
            break;
        }
        if (n_ready == 0)
        {
            continue;
        }
        
        for (int i = 0; i < n_ready; ++i)
        {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;
            
            if (fd == listen_fd && (revents & EPOLLIN))
            {
                while (true)
                {
                    sockaddr_in client_addr{};
                    socklen_t len = sizeof(client_addr);
                    int client_fd = accept4(listen_fd, (sockaddr*)&client_addr, &len, SOCK_NONBLOCK);
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }
                        LOG_ERROR("accept 接受连接失败");
                        break;
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(conn_mtx);
                        if (conn_map.size() >= MAX_CONNECTIONS)
                        {
                            close(client_fd);
                            continue;
                        }
                    }
                    
                    int nodelay = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                    
                    Conn* new_conn = new Conn(client_fd);
                    {
                        std::lock_guard<std::mutex> lock(conn_mtx);
                        conn_map[client_fd] = new_conn;
                    }
                    
                    epoll_event c_ev{};
                    c_ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    c_ev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &c_ev) < 0)
                    {
                        close(client_fd);
                        {
                            std::lock_guard<std::mutex> lock(conn_mtx);
                            conn_map.erase(client_fd);
                        }
                        delete new_conn;
                        continue;
                    }
                }
            }
            else if (revents & EPOLLIN)
            {
                int n = read(fd, buffer, sizeof(buffer) - 1);
                
                if (n <= 0)
                {
                    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                    {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        {
                            std::lock_guard<std::mutex> lock(conn_mtx);
                            auto it = conn_map.find(fd);
                            if (it != conn_map.end())
                            {
                                delete it->second;
                                conn_map.erase(it);
                            }
                        }
                    }
                    continue;
                }
                
                buffer[n] = '\0';
                std::string request(buffer, n);
                
                pool.submit([fd, epoll_fd, request]() {
                    handle_client_request(fd, epoll_fd, request);
                });
            }
        }
    }
    
    timer_stop = true;
    timer_thread.join();
    
    pool.stop();
    
    close(epoll_fd);
    close(listen_fd);
    
    {
        std::lock_guard<std::mutex> lock(conn_mtx);
        for (auto& pair : conn_map)
        {
            close(pair.first);
            delete pair.second;
        }
        conn_map.clear();
    }
    
    LOG_INFO("服务器已关闭");
    return 0;
}
