// 确保 router.h 先被包含（它已经包含了 http.h）
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

// 全局变量定义
std::unordered_map<int, Conn*> conn_map;
std::mutex conn_mtx;
std::atomic<bool> timer_stop = false;
std::atomic<bool> server_stop = false;

// 信号处理函数
void handle_signal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        LOG_INFO("收到终止信号，服务器即将退出");
        server_stop = true;
    }
}

// 设置非阻塞和禁用Nagle算法
void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
}

// 安全读取（带超时）
bool readn(int fd, char* buf, int len, int timeout_ms = 5000)
{
    int has_read = 0;
    int retry_count = 0;
    const int max_retries = timeout_ms / 10;
    
    while (has_read < len && retry_count < max_retries)
    {
        int n = read(fd, buf + has_read, len - has_read);
        if (n == 0)
        {
            return false;
        }
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                retry_count++;
                struct timespec ts = {0, 10000000};
                nanosleep(&ts, nullptr);
                continue;
            }
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (n > 0)
        {
            has_read += n;
            retry_count = 0;
        }
    }
    return has_read == len;
}

// 获取文件类型
std::string get_content_type(const std::string& filename)
{
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos)
    {
        return "text/plain";
    }
    
    std::string ext = filename.substr(dot_pos + 1);
    if (ext == "html" || ext == "htm")
    {
        return "text/html";
    }
    if (ext == "css")
    {
        return "text/css";
    }
    if (ext == "js")
    {
        return "application/javascript";
    }
    if (ext == "json")
    {
        return "application/json";
    }
    if (ext == "png")
    {
        return "image/png";
    }
    if (ext == "jpg" || ext == "jpeg")
    {
        return "image/jpeg";
    }
    if (ext == "gif")
    {
        return "image/gif";
    }
    if (ext == "svg")
    {
        return "image/svg+xml";
    }
    if (ext == "pdf")
    {
        return "application/pdf";
    }
    return "text/plain";
}

// 静态文件服务
HttpResponse serve_static_file(const std::string& file_path)
{
    HttpResponse res;
    
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        res.SetStatus(404, "Not Found");
        res.body = "<html><body><h1>404 File Not Found</h1></body></html>";
        return res;
    }
    
    std::streampos size = file.tellg();
    std::string content(size, '\0');
    file.seekg(0);
    file.read(&content[0], size);
    
    res.SetStatus(200, "OK");
    res.SetContentType(get_content_type(file_path));
    res.headers["Content-Length"] = std::to_string(size);
    res.body = content;
    
    return res;
}

// 刷新定时器（直接更新连接的活跃时间）
void refresh_timer(Conn* conn)
{
    conn->active_time.store(time(nullptr));
}

// 检查超时（简化版本，只在必要时检查）
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
        LOG_INFO("超时断开连接 fd: %d", timeout_fds[i]);
        delete timeout_conns[i];
    }
}

Router router;

int main()
{
    Logger::GetInstance().SetFile("log.txt");
    Logger::GetInstance().SetLevel(LOG_INFO);
    LOG_INFO("HTTP Web Server 启动，监听端口 5005");
    
    // 确保线程池至少有4个工作线程
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
    
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        LOG_ERROR("socket 创建失败");
        return -1;
    }
    set_nonblock(listen_fd);
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
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
    if (listen(listen_fd, 1024) < 0)
    {
        LOG_ERROR("listen 失败");
        close(listen_fd);
        return -1;
    }
    
    int epoll_fd = epoll_create(MAX_EVENTS);
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
            sleep(1);
            check_timeout(epoll_fd);
        }
        LOG_INFO("定时器线程退出");
    });
    
    std::vector<epoll_event> events(MAX_EVENTS);
    
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
                    int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &len);
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN)
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
                            LOG_WARN("连接数已达上限，拒绝连接 fd: %d", client_fd);
                            close(client_fd);
                            continue;
                        }
                    }
                    
                    set_nonblock(client_fd);
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
                        LOG_ERROR("epoll_ctl 添加客户端fd失败: %d", client_fd);
                        close(client_fd);
                        {
                            std::lock_guard<std::mutex> lock(conn_mtx);
                            conn_map.erase(client_fd);
                        }
                        delete new_conn;
                        continue;
                    }
                    LOG_INFO("新客户端连接 fd: %d", client_fd);
                }
            }
            else if (revents & EPOLLIN)
            {
                char buf[BUF_SIZE] = {0};
                int n = read(fd, buf, sizeof(buf) - 1);
                
                if (n <= 0)
                {
                    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                    {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        LOG_INFO("客户端断开连接 fd: %d", fd);
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
                
                buf[n] = '\0';
                std::string request(buf);
                
                pool.submit([fd, epoll_fd, request]() {
                    LOG_DEBUG("收到 HTTP 请求: %s", request.c_str());
                    
                    HttpRequest req;
                    HttpResponse res;
                    
                    if (req.parse(request))
                    {
                        LOG_INFO("HTTP %s %s", req.method.c_str(), req.path.c_str());
                        res = router.route(req);
                    }
                    else
                    {
                        res.SetStatus(400, "Bad Request");
                        res.body = "<html><body><h1>400 Bad Request</h1></body></html>";
                    }
                    
                    std::string response = res.to_string();
                    ssize_t total_written = 0;
                    ssize_t response_len = response.size();
                    
                    while (total_written < response_len)
                    {
                        ssize_t n_write = write(fd, response.c_str() + total_written, response_len - total_written);
                        if (n_write > 0)
                        {
                            total_written += n_write;
                        }
                        else if (n_write < 0 && errno == EAGAIN)
                        {
                            struct timespec ts = {0, 100000};
                            nanosleep(&ts, nullptr);
                            continue;
                        }
                        else
                        {
                            LOG_ERROR("write 失败 fd: %d", fd);
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
                            it->second->active_time = time(nullptr);
                        }
                    }
                    
                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    ev.data.fd = fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                });
            }
        }
    }
    
    timer_stop = true;
    timer_thread.join();
    
    pool.stop();
    
    close(epoll_fd);
    close(listen_fd);
    
    LOG_INFO("服务器已关闭");
    return 0;
}
