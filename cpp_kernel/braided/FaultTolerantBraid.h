#pragma once

#include "BraidedKernel.h"
#include "BraidCoordinator.h"
#include "FaultTolerance.h"
#include "../core/ThreadPool.h"
#include "../core/Metrics.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

namespace braided {

// Configuration constants
namespace config {
    constexpr uint64_t DEFAULT_BRAID_INTERVAL = 1000;
    constexpr uint64_t DEFAULT_HEARTBEAT_INTERVAL = 100;
    constexpr uint64_t DEFAULT_TIMEOUT_MS = 500;
    constexpr size_t NUM_TORI = 3;
    constexpr size_t THREAD_POOL_SIZE = 3;
}

struct BraidStats {
    uint64_t total_ticks = 0;
    uint64_t total_events = 0;
    uint64_t total_exchanges = 0;
    uint64_t total_reconstructions = 0;
    double events_per_second = 0.0;
    uint64_t duration_ms = 0;
    uint64_t consistency_violations = 0;
    uint64_t boundary_corrections = 0;
};

class FaultTolerantBraid {
private:
    std::unique_ptr<BraidedKernel> torus_[config::NUM_TORI];
    BraidCoordinator coordinator_;
    FaultDetector fault_detector_;
    TorusReconstructor reconstructor_;
    std::unique_ptr<rse::ThreadPool> thread_pool_;
    uint64_t braid_interval_ = config::DEFAULT_BRAID_INTERVAL;
    uint64_t heartbeat_interval_ = config::DEFAULT_HEARTBEAT_INTERVAL;
    uint64_t current_tick_ = 0;
    bool parallel_execution_ = false;
    std::mutex torus_mutex_[config::NUM_TORI];
    std::atomic<bool> running_{false};
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    explicit FaultTolerantBraid(uint64_t braid_interval = config::DEFAULT_BRAID_INTERVAL, 
                                bool parallel = true)
        : braid_interval_(braid_interval), parallel_execution_(parallel) {
        for (size_t i = 0; i < config::NUM_TORI; i++) {
            torus_[i] = std::make_unique<BraidedKernel>();
            torus_[i]->setTorusId(static_cast<uint32_t>(i));
        }
        fault_detector_.onFailure([this](uint32_t id) { handleFailure(id); });
        
        // Initialize thread pool for parallel execution
        if (parallel_execution_) {
            thread_pool_ = std::make_unique<rse::ThreadPool>(config::THREAD_POOL_SIZE);
        }
        
        RSE_METRICS.active_tori.set(config::NUM_TORI);
    }
    
    ~FaultTolerantBraid() { 
        stop(); 
        // Ensure thread pool is destroyed before other members
        thread_pool_.reset();
    }
    
    void setParallelExecution(bool enable) { 
        parallel_execution_ = enable;
        if (enable && !thread_pool_) {
            thread_pool_ = std::make_unique<rse::ThreadPool>(config::THREAD_POOL_SIZE);
        }
    }
    void setBraidInterval(uint64_t interval) { braid_interval_ = interval; }
    void setHeartbeatInterval(uint64_t interval) { heartbeat_interval_ = interval; }
    
    BraidedKernel& getTorus(int id) { return *torus_[id]; }
    BraidedKernel& getTorusA() { return *torus_[0]; }
    BraidedKernel& getTorusB() { return *torus_[1]; }
    BraidedKernel& getTorusC() { return *torus_[2]; }
    
    void tick() {
        rse::ScopedTimer timer(RSE_METRICS.event_latency_us);
        
        if (parallel_execution_ && thread_pool_) {
            // Use persistent thread pool - much faster than creating threads per tick
            for (size_t i = 0; i < config::NUM_TORI; i++) {
                if (fault_detector_.getHealth(static_cast<uint32_t>(i)) != TorusHealth::FAILED) {
                    thread_pool_->submit([this, i]() {
                        std::lock_guard<std::mutex> lock(torus_mutex_[i]);
                        torus_[i]->tick();
                    });
                }
            }
            thread_pool_->waitAll();
        } else {
            for (size_t i = 0; i < config::NUM_TORI; i++) {
                if (fault_detector_.getHealth(static_cast<uint32_t>(i)) != TorusHealth::FAILED) {
                    torus_[i]->tick();
                }
            }
        }
        
        current_tick_++;
        RSE_METRICS.events_processed.increment();
        
        // Braid exchange at configured interval
        if (current_tick_ % braid_interval_ == 0) {
            rse::ScopedTimer exchange_timer(RSE_METRICS.exchange_latency_us);
            coordinator_.exchange(*torus_[0], *torus_[1], *torus_[2]);
            RSE_METRICS.projection_exchanges.increment();
        }
        
        // Heartbeat at configured interval
        if (current_tick_ % heartbeat_interval_ == 0) {
            for (size_t i = 0; i < config::NUM_TORI; i++) {
                if (fault_detector_.getHealth(static_cast<uint32_t>(i)) != TorusHealth::FAILED) {
                    fault_detector_.heartbeat(
                        static_cast<uint32_t>(i), 
                        torus_[i]->getEventsProcessed(), 
                        torus_[i]->extractProjection()
                    );
                }
            }
        }
    }
    
    BraidStats run(uint64_t max_ticks) {
        start_time_ = std::chrono::high_resolution_clock::now();
        running_ = true;
        // Don't start fault detector thread - causes futex issues on cleanup
        // fault_detector_.start();
        for (uint64_t i = 0; i < max_ticks && running_; i++) tick();
        // fault_detector_.stop();
        auto end = std::chrono::high_resolution_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_time_);
        return computeStats(dur.count());
    }
    
    void stop() { 
        running_ = false; 
        // Only stop fault detector if it was started
        // fault_detector_.stop(); 
    }
    uint64_t getTotalExchanges() const { return coordinator_.getTotalExchanges(); }
    uint64_t getBraidCycles() const { return coordinator_.getExchangeCount(); }
    
    void printStats() const {
        auto stats = computeStats(0);
        std::cout << "\n[BRAID] Stats: " << stats.total_events << " events, "
                  << stats.total_exchanges << " exchanges" << std::endl;
        fault_detector_.printStatus();
    }
    
private:
    void handleFailure(uint32_t failed_id) {
        rse::ScopedTimer timer(RSE_METRICS.reconstruction_time_us);
        RSE_METRICS.torus_failures.increment();
        RSE_METRICS.active_tori.decrement();
        
        std::vector<Projection> projs;
        for (size_t i = 0; i < config::NUM_TORI; i++) {
            if (i != failed_id && fault_detector_.getHealth(static_cast<uint32_t>(i)) != TorusHealth::FAILED) {
                projs.push_back(fault_detector_.getLastProjection(static_cast<uint32_t>(i)));
            }
        }
        if (projs.size() >= 2) {
            fault_detector_.markReconstructing(failed_id);
            reconstructor_.reconstruct(*torus_[failed_id], projs[0], projs[1]);
            RSE_METRICS.torus_reconstructions.increment();
            RSE_METRICS.active_tori.increment();
        }
    }
    
    BraidStats computeStats(uint64_t duration_ms) const {
        BraidStats s;
        s.total_ticks = current_tick_;
        for (size_t i = 0; i < config::NUM_TORI; i++) {
            s.total_events += torus_[i]->getEventsProcessed();
            s.boundary_corrections += torus_[i]->getCorrectiveEvents();
        }
        s.total_exchanges = coordinator_.getTotalExchanges();
        s.total_reconstructions = reconstructor_.getReconstructionsCompleted();
        s.consistency_violations = coordinator_.getConsistencyViolations();
        s.duration_ms = duration_ms;
        if (duration_ms > 0) s.events_per_second = s.total_events * 1000.0 / duration_ms;
        return s;
    }
};

} // namespace braided
