#pragma once

#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <memory>

namespace MultiThreadedInstaller {

class ThreadPoolManager {
public:
    explicit ThreadPoolManager(size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPoolManager();
    
    // 禁用拷贝构造和赋值
    ThreadPoolManager(const ThreadPoolManager&) = delete;
    ThreadPoolManager& operator=(const ThreadPoolManager&) = delete;
    
    // 将任务加入队列
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
    
    // 等待所有任务完成
    void waitForAll();
    
    // 获取活跃线程数
    size_t getActiveThreadCount() const;
    
    // 获取总线程数
    size_t getTotalThreadCount() const;
    
    // 停止线程池
    void stop();
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stopFlag;
    
    size_t activeThreads;
    mutable std::mutex activeThreadsMutex;
    std::condition_variable allTasksComplete;
    
    // 工作线程函数
    void workerThread();
};

// 模板函数实现
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
    }
    
    condition.notify_one();
    return result;
}

} // namespace MultiThreadedInstaller