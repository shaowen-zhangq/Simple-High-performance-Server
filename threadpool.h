#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool
{
public:
    explicit ThreadPool(size_t thread_num = 4);
    ~ThreadPool();

    template<typename F>
    void submit(F&& func);

    void stop();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

private:
    void WorkerThread();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mtx_;
    std::condition_variable cond_;
    std::atomic<bool> stop_;
};

template<typename F>
void ThreadPool::submit(F&& func)
{
    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        tasks_.emplace(std::forward<F>(func));
    }
    cond_.notify_one();
}

#endif