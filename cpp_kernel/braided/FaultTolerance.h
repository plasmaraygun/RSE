#pragma once

#include "BraidedKernel.h"
#include "Projection.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

namespace braided {

enum class TorusHealth { HEALTHY, DEGRADED, FAILED, RECONSTRUCTING, RECOVERED };

inline const char* healthToString(TorusHealth h) {
    switch (h) {
        case TorusHealth::HEALTHY: return "HEALTHY";
        case TorusHealth::DEGRADED: return "DEGRADED";
        case TorusHealth::FAILED: return "FAILED";
        case TorusHealth::RECONSTRUCTING: return "RECONSTRUCTING";
        case TorusHealth::RECOVERED: return "RECOVERED";
        default: return "UNKNOWN";
    }
}

struct HeartbeatRecord {
    uint32_t torus_id = 0;
    uint64_t last_heartbeat_time = 0;
    uint64_t last_event_count = 0;
    uint64_t consecutive_failures = 0;
    TorusHealth health = TorusHealth::HEALTHY;
    Projection last_projection;
    
    void recordHeartbeat(uint64_t now_ms, uint64_t events, const Projection& proj) {
        last_heartbeat_time = now_ms;
        last_event_count = events;
        last_projection = proj;
        consecutive_failures = 0;
        if (health == TorusHealth::DEGRADED || health == TorusHealth::RECOVERED) {
            health = TorusHealth::HEALTHY;
        }
    }
    
    void recordFailure() {
        consecutive_failures++;
        if (consecutive_failures >= 3) health = TorusHealth::FAILED;
        else if (consecutive_failures >= 1) health = TorusHealth::DEGRADED;
    }
};

class FaultDetector {
private:
    HeartbeatRecord records_[3];
    uint64_t heartbeat_interval_ms_ = 100;
    uint64_t timeout_ms_ = 500;
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    mutable std::mutex mutex_;
    std::function<void(uint32_t)> on_failure_;
    std::function<void(uint32_t)> on_recovery_;
    
    uint64_t now_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
public:
    FaultDetector() {
        for (int i = 0; i < 3; i++) {
            records_[i].torus_id = i;
            records_[i].last_heartbeat_time = now_ms();
        }
    }
    ~FaultDetector() { 
        // Only stop if we actually started
        if (running_.load()) {
            stop(); 
        }
    }
    
    void setHeartbeatInterval(uint64_t ms) { heartbeat_interval_ms_ = ms; }
    void setTimeout(uint64_t ms) { timeout_ms_ = ms; }
    void onFailure(std::function<void(uint32_t)> cb) { on_failure_ = cb; }
    void onRecovery(std::function<void(uint32_t)> cb) { on_recovery_ = cb; }
    
    void heartbeat(uint32_t torus_id, uint64_t events, const Projection& proj) {
        if (torus_id >= 3) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& rec = records_[torus_id];
        bool was_failed = (rec.health == TorusHealth::FAILED || rec.health == TorusHealth::RECONSTRUCTING);
        rec.recordHeartbeat(now_ms(), events, proj);
        if (was_failed) {
            rec.health = TorusHealth::RECOVERED;
            if (on_recovery_) on_recovery_(torus_id);
        }
    }
    
    void checkHealth() {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t now = now_ms();
        for (int i = 0; i < 3; i++) {
            auto& rec = records_[i];
            if (rec.health == TorusHealth::RECONSTRUCTING) continue;
            if (now - rec.last_heartbeat_time > timeout_ms_) {
                TorusHealth old = rec.health;
                rec.recordFailure();
                if (rec.health == TorusHealth::FAILED && old != TorusHealth::FAILED) {
                    if (on_failure_) on_failure_(i);
                }
            }
        }
    }
    
    TorusHealth getHealth(uint32_t id) const {
        if (id >= 3) return TorusHealth::FAILED;
        std::lock_guard<std::mutex> lock(mutex_);
        return records_[id].health;
    }
    
    Projection getLastProjection(uint32_t id) const {
        if (id >= 3) return Projection{};
        std::lock_guard<std::mutex> lock(mutex_);
        return records_[id].last_projection;
    }
    
    void markReconstructing(uint32_t id) {
        if (id >= 3) return;
        std::lock_guard<std::mutex> lock(mutex_);
        records_[id].health = TorusHealth::RECONSTRUCTING;
    }
    
    void start() {
        if (running_.load()) return;
        running_.store(true);
        monitor_thread_ = std::thread([this]() {
            while (running_.load()) {
                checkHealth();
                std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_ms_));
            }
        });
    }
    
    void stop() {
        running_.store(false);
        // Give thread time to notice the flag change
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    void printStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[FAULT] Status:";
        for (int i = 0; i < 3; i++) {
            std::cout << " T" << i << "=" << healthToString(records_[i].health);
        }
        std::cout << std::endl;
    }
};

class TorusReconstructor {
private:
    uint64_t reconstructions_ = 0;
    uint64_t successful_reconstructions_ = 0;
    uint64_t total_time_us_ = 0;
    uint64_t total_processes_restored_ = 0;
    uint64_t total_events_injected_ = 0;
    
public:
    // REAL reconstruction: rebuild process state from neighbor projections
    bool reconstruct(BraidedKernel& failed, const Projection& proj_a, const Projection& proj_b) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Verify projection integrity before using
        if (!proj_a.verify() || !proj_b.verify()) {
            std::cerr << "[RECONSTRUCT] FAILED: Projection integrity check failed" << std::endl;
            reconstructions_++;
            return false;
        }
        
        // Reset the failed torus
        failed.reset();
        
        // REAL: Determine target time from consensus of neighbors
        uint64_t target_time = std::max(proj_a.current_time, proj_b.current_time);
        
        // REAL: Rebuild boundary processes from projection data
        // We spawn processes at boundary cells based on neighbor state
        int processes_created = 0;
        for (size_t i = 0; i < Projection::BOUNDARY_SIZE; i++) {
            // Use consensus: prefer non-zero, or average if both non-zero
            uint32_t state_a = proj_a.boundary_states[i];
            uint32_t state_b = proj_b.boundary_states[i];
            
            uint32_t reconstructed_state = 0;
            if (state_a != 0 && state_b != 0) {
                // Both neighbors have state - use average for consistency
                reconstructed_state = (state_a + state_b) / 2;
            } else {
                reconstructed_state = (state_a != 0) ? state_a : state_b;
            }
            
            if (reconstructed_state != 0) {
                int y = static_cast<int>(i / 32);
                int z = static_cast<int>(i % 32);
                
                // Spawn process with reconstructed state on x=0 boundary
                failed.spawnProcessWithState(0, y, z, 
                    static_cast<int64_t>(reconstructed_state),
                    static_cast<int64_t>(reconstructed_state));
                processes_created++;
            }
        }
        total_processes_restored_ += processes_created;
        
        // REAL: Inject events to propagate reconstructed state inward
        int events_injected = 0;
        for (size_t i = 0; i < Projection::BOUNDARY_SIZE; i++) {
            uint32_t state = (proj_a.boundary_states[i] + proj_b.boundary_states[i]) / 2;
            if (state != 0) {
                int y = static_cast<int>(i / 32);
                int z = static_cast<int>(i % 32);
                
                // Inject events from boundary inward (x=0 to x=1)
                failed.injectEvent(1, y, z, 0, y, z, static_cast<int32_t>(state & 0x7FFFFFFF));
                events_injected++;
                
                // Also inject on opposite boundary (x=31)
                if (events_injected < 500) {
                    failed.injectEvent(31, y, z, 31, y, z, static_cast<int32_t>(state & 0x7FFFFFFF));
                    events_injected++;
                }
            }
        }
        total_events_injected_ += events_injected;
        
        // REAL: Run ticks to propagate reconstructed state
        // Number of ticks proportional to how much state needs rebuilding
        int ticks_to_run = std::min(1000, std::max(100, processes_created * 10));
        for (int i = 0; i < ticks_to_run; i++) {
            failed.tick();
        }
        
        // Apply constraints from both neighbors for final consistency
        failed.applyConstraint(proj_a);
        failed.applyConstraint(proj_b);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        reconstructions_++;
        successful_reconstructions_++;
        total_time_us_ += dur.count();
        
        std::cout << "[RECONSTRUCT] Restored torus: " << processes_created << " processes, "
                  << events_injected << " events, " << ticks_to_run << " ticks, "
                  << dur.count() << "us" << std::endl;
        
        return true;
    }
    
    uint64_t getReconstructionsCompleted() const { return reconstructions_; }
    uint64_t getSuccessfulReconstructions() const { return successful_reconstructions_; }
    uint64_t getTotalProcessesRestored() const { return total_processes_restored_; }
    uint64_t getTotalEventsInjected() const { return total_events_injected_; }
    double getAverageReconstructionTimeUs() const {
        return reconstructions_ ? static_cast<double>(total_time_us_) / reconstructions_ : 0;
    }
};

} // namespace braided
