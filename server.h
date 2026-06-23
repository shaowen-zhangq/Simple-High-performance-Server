#ifndef SERVER_H
#define SERVER_H

// 注意：不再包含 http.h，因为 router.h 已经包含了
#include "router.h"
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <queue>

// 全局配置常量
const int MAX_EVENTS = 4096;       // 增大事件队列
const int BUF_SIZE = 8192;          // 增大缓冲区
const int IDLE_TIMEOUT = 30;        // 延长超时时间
const int MAX_CONNECTIONS = 100000; // 增大连接上限

// 连接结构体
struct Conn
{
    int fd;
    std::atomic<time_t> active_time;
    std::string buffer;
    Conn(int f) : fd(f), active_time(time(nullptr)) {}
};

// 定时器节点
struct TimerNode
{
    Conn* conn;
    time_t expire_time;
    TimerNode(Conn* c, time_t e) : conn(c), expire_time(e) {}
    bool operator>(const TimerNode& other) const
    {
        return expire_time > other.expire_time;
    }
};

// 定时器映射，用于快速查找和更新连接的最新定时器
extern std::unordered_map<int, time_t> timer_map;

// 工具函数声明
void set_nonblock(int fd);
void handle_signal(int sig);
bool readn(int fd, char* buf, int len);
HttpResponse serve_static_file(const std::string& file_path);
std::string get_content_type(const std::string& filename);

// 全局变量
extern std::priority_queue<TimerNode, std::vector<TimerNode>, std::greater<TimerNode>> timer_heap;
extern std::unordered_map<int, Conn*> conn_map;
extern std::mutex timer_mtx;
extern std::mutex conn_mtx;
extern std::atomic<bool> timer_stop;
extern std::atomic<bool> server_stop;

// 定时器函数
void refresh_timer(Conn* conn);
void check_timeout(int epoll_fd);

#endif // SERVER_H
