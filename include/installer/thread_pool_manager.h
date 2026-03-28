#pragma once

#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <memory>
#include <stdexcept>

namespace MultiThreadedInstaller {

size_t ResolveThreadPoolWorkerCount(size_t requestedThreadCount);

class ThreadPoolManager {
public:
    explicit ThreadPoolManager(size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPoolManager();
    

    ThreadPoolManager(const ThreadPoolManager&) = delete;
    ThreadPoolManager& operator=(const ThreadPoolManager&) = delete;
    

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
    

    void waitForAll();
    

    size_t getActiveThreadCount() const;
    

    size_t getTotalThreadCount() const;
    

    void stop();
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stopFlag;
    size_t pendingTasks;
    
    size_t activeThreads;
    mutable std::mutex activeThreadsMutex;
    std::condition_variable allTasksComplete;
    

    void workerThread();
};


template<typename F, typename... Args>
auto ThreadPoolManager::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> result = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        if (stopFlag) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        
        tasks.emplace([task]() { (*task)(); });
        ++pendingTasks;
    }
    
    condition.notify_one();
    return result;
}

} // namespace MultiThreadedInstaller
