#include "threadpool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t thread_num) : stop_(false)
{
    std::cout << "ThreadPool created with " << thread_num << " threads" << std::endl;
    for (size_t i = 0; i < thread_num; ++i)
    {
        workers_.emplace_back([this, i]() {
            std::cout << "Worker thread " << i << " started" << std::endl;
            WorkerThread();
        });
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::stop()
{
    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        stop_ = true;
    }
    cond_.notify_all();
    for (auto& t : workers_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

void ThreadPool::WorkerThread()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mtx_);
            cond_.wait(lock, [this]() {
                return stop_ || !tasks_.empty();
            });
            if (stop_ && tasks_.empty())
            {
                return;
            }
            if (!tasks_.empty())
            {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        if (task)
        {
            task();
        }
    }
}
