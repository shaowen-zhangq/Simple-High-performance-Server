
#include "logger.h"
#include <iostream>
#include <cstdarg>
#include <ctime>
#include <cstring>

Logger& Logger::GetInstance()
{
    static Logger instance;
    return instance;
}

Logger::Logger() : global_level_(LOG_DEBUG)
{
}

Logger::~Logger()
{
    if(ofs_.is_open())
    {
        ofs_.close();
    }
}

void Logger::SetFile(const std::string& file_name)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(ofs_.is_open())
    {
        ofs_.close();
    }
    ofs_.open(file_name, std::ios::app);
}

void Logger::SetLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mtx_);
    global_level_ = level;
}

void Logger::Log(LogLevel level, const char* fmt, ...)
{
    if(level < global_level_)
    {
        return;
    }

    char buf[4096] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    LogInternal(level, buf);
}

void Logger::LogInternal(LogLevel level, const char* msg)
{
    std::lock_guard<std::mutex> lock(mtx_);

    time_t now = time(nullptr);
    char time_str[64] = {0};
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    std::string log_msg = std::string("[") + time_str + "][" + level_str[level] + "]" + msg;
    
    std::cout << log_msg << std::endl;

    if(ofs_.is_open())
    {
        ofs_ << log_msg << std::endl;
    }
}
