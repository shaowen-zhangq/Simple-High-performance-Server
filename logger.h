#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <queue>
#include <atomic>
#include <thread>
#include <condition_variable>

enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

class Logger
{
public:
    static Logger& GetInstance();
    
    void SetFile(const std::string& file_name);
    void SetLevel(LogLevel level);
    
    void Log(LogLevel level, const char* fmt, ...);
    
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

private:
    Logger();
    ~Logger();
    
    void WorkerThread();
    void LogInternal(LogLevel level, const char* msg);
    
    std::ofstream ofs_;
    std::mutex mtx_;
    std::mutex queue_mtx_;
    std::queue<std::string> log_queue_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::thread worker_thread_;
    LogLevel global_level_;
};

#define LOG_DEBUG(...) Logger::GetInstance().Log(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) Logger::GetInstance().Log(LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...) Logger::GetInstance().Log(LOG_WARN, __VA_ARGS__)
#define LOG_ERROR(...) Logger::GetInstance().Log(LOG_ERROR, __VA_ARGS__)

#endif // LOGGER_H
