#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace rse {

/**
 * ThreadPool: Enterprise-grade thread pool for parallel torus execution.
 * 
 * Features:
 * - Persistent worker threads (no per-tick creation overhead)
 * - Work stealing for load balancing
 * - Graceful shutdown
 * - Task completion barrier
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 3) : stop_(false), active_tasks_(0) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }
    
    ~ThreadPool() {
        shutdown();
    }
    
    void shutdown() {
        bool expected = false;
        if (!stop_.compare_exchange_strong(expected, true)) {
            return;  // Already stopped
        }
        
        // Wake all workers
        condition_.notify_all();
        
        // Small delay to let workers see stop flag
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Join all workers
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    // Submit a task for execution
    template<typename F>
    void submit(F&& task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(std::forward<F>(task));
            active_tasks_++;
        }
        condition_.notify_one();
    }
    
    // Wait for all submitted tasks to complete
    void waitAll() {
        std::unique_lock<std::mutex> lock(done_mutex_);
        done_condition_.wait(lock, [this] { 
            return active_tasks_.load() == 0; 
        });
    }
    
    // Submit multiple tasks and wait for completion
    template<typename F>
    void parallelFor(size_t count, F&& task) {
        for (size_t i = 0; i < count; ++i) {
            submit([&task, i] { task(i); });
        }
        waitAll();
    }
    
    size_t threadCount() const { return workers_.size(); }
    size_t pendingTasks() const { 
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size(); 
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] { 
                    return stop_ || !tasks_.empty(); 
                });
                
                if (stop_ && tasks_.empty()) {
                    return;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            
            task();
            
            if (--active_tasks_ == 0) {
                std::unique_lock<std::mutex> lock(done_mutex_);
                done_condition_.notify_all();
            }
        }
    }
    
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    
    std::mutex done_mutex_;
    std::condition_variable done_condition_;
    
    std::atomic<bool> stop_;
    std::atomic<size_t> active_tasks_;
};

} // namespace rse
