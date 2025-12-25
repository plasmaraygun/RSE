#pragma once

#include "OSProcess.h"
#include "../FixedStructures.h"
#include <iostream>
#include <algorithm>

/**
 * TorusScheduler: Per-torus process scheduler.
 * 
 * Each torus has its own independent scheduler - no global coordination needed.
 * This eliminates the global scheduler bottleneck that plagues traditional OSes.
 * 
 * Key features:
 * - O(1) scheduling overhead (doesn't scale with number of processes)
 * - Cache-friendly (processes stay on same core)
 * - Lock-free (no contention between tori)
 * - Fair (prevents starvation)
 */

namespace os {

class TorusScheduler {
public:
    // Scheduling policy
    enum class Policy {
        ROUND_ROBIN,        // Simple round-robin
        PRIORITY,           // Priority-based
        FAIR                // Completely fair scheduler (CFS-like)
    };
    
    Policy policy_;
    
private:
    // Process queues
    FixedVector<OSProcess*, 1024> ready_queue_;     // Processes ready to run
    FixedVector<OSProcess*, 1024> blocked_queue_;   // Processes waiting for I/O
    
    // Current running process
    OSProcess* current_process_;
    
    // Statistics
    uint64_t total_ticks_;
    uint64_t idle_ticks_;
    uint64_t context_switches_;
    
    // Torus ID (for debugging)
    uint32_t torus_id_;
    
public:
    TorusScheduler(uint32_t torus_id, Policy policy = Policy::FAIR)
        : current_process_(nullptr),
          policy_(policy),
          total_ticks_(0),
          idle_ticks_(0),
          context_switches_(0),
          torus_id_(torus_id)
    {
    }
    
    // ========== Process Management ==========
    
    /**
     * Add a process to the ready queue.
     */
    bool addProcess(OSProcess* proc) {
        if (!proc) return false;
        
        proc->setReady();
        proc->torus_id = torus_id_;
        
        if (!ready_queue_.push_back(proc)) {
            std::cerr << "[TorusScheduler " << torus_id_ << "] Ready queue full!" << std::endl;
            return false;
        }
        
        return true;
    }
    
    /**
     * Remove a process from all queues.
     */
    bool removeProcess(uint32_t pid) {
        // Check current process
        if (current_process_ && current_process_->pid == pid) {
            current_process_ = nullptr;
            return true;
        }
        
        // Check ready queue
        for (size_t i = 0; i < ready_queue_.size(); i++) {
            if (ready_queue_[i]->pid == pid) {
                ready_queue_.erase_at(i);
                return true;
            }
        }
        
        // Check blocked queue
        for (size_t i = 0; i < blocked_queue_.size(); i++) {
            if (blocked_queue_[i]->pid == pid) {
                blocked_queue_.erase_at(i);
                return true;
            }
        }
        
        return false;
    }
    
    /**
     * Block a process (move to blocked queue).
     */
    bool blockProcess(uint32_t pid) {
        // Check if it's the current process
        if (current_process_ && current_process_->pid == pid) {
            current_process_->setBlocked();
            blocked_queue_.push_back(current_process_);
            current_process_ = nullptr;
            return true;
        }
        
        // Check ready queue
        for (size_t i = 0; i < ready_queue_.size(); i++) {
            if (ready_queue_[i]->pid == pid) {
                OSProcess* proc = ready_queue_[i];
                ready_queue_.erase_at(i);
                proc->setBlocked();
                blocked_queue_.push_back(proc);
                return true;
            }
        }
        
        return false;
    }
    
    /**
     * Unblock a process (move to ready queue).
     */
    bool unblockProcess(uint32_t pid) {
        for (size_t i = 0; i < blocked_queue_.size(); i++) {
            if (blocked_queue_[i]->pid == pid) {
                OSProcess* proc = blocked_queue_[i];
                blocked_queue_.erase_at(i);
                proc->setReady();
                ready_queue_.push_back(proc);
                return true;
            }
        }
        
        return false;
    }
    
    // ========== Scheduling Algorithms ==========
    
    /**
     * Pick next process to run (Round-Robin).
     */
    OSProcess* scheduleRoundRobin() {
        if (ready_queue_.empty()) {
            return nullptr;
        }
        
        // Simple: pick first, move to back
        OSProcess* next = ready_queue_[0];
        ready_queue_.erase_at(0);
        
        return next;
    }
    
    /**
     * Pick next process to run (Priority-based).
     */
    OSProcess* schedulePriority() {
        if (ready_queue_.empty()) {
            return nullptr;
        }
        
        // Find highest priority process
        size_t best_idx = 0;
        uint32_t best_priority = ready_queue_[0]->priority;
        
        for (size_t i = 1; i < ready_queue_.size(); i++) {
            if (ready_queue_[i]->priority > best_priority) {
                best_idx = i;
                best_priority = ready_queue_[i]->priority;
            }
        }
        
        OSProcess* next = ready_queue_[best_idx];
        ready_queue_.erase_at(best_idx);
        
        return next;
    }
    
    /**
     * Pick next process to run (Completely Fair Scheduler).
     * Picks the process with the least total runtime.
     */
    OSProcess* scheduleFair() {
        if (ready_queue_.empty()) {
            return nullptr;
        }
        
        // Find process with least runtime
        size_t best_idx = 0;
        uint64_t least_runtime = ready_queue_[0]->total_runtime;
        
        for (size_t i = 1; i < ready_queue_.size(); i++) {
            if (ready_queue_[i]->total_runtime < least_runtime) {
                best_idx = i;
                least_runtime = ready_queue_[i]->total_runtime;
            }
        }
        
        OSProcess* next = ready_queue_[best_idx];
        ready_queue_.erase_at(best_idx);
        
        return next;
    }
    
    /**
     * Pick next process based on current policy.
     */
    OSProcess* schedule() {
        switch (policy_) {
            case Policy::ROUND_ROBIN:
                return scheduleRoundRobin();
            case Policy::PRIORITY:
                return schedulePriority();
            case Policy::FAIR:
                return scheduleFair();
            default:
                return scheduleRoundRobin();
        }
    }
    
    // ========== Main Scheduler Tick ==========
    
    /**
     * Main scheduler tick - called every tick.
     */
    void tick() {
        total_ticks_++;
        
        // Check if current process should be preempted
        if (current_process_) {
            // Check if time slice expired
            if (current_process_->timeSliceExpired()) {
                // Preempt: save context and put back in ready queue
                current_process_->saveContext();
                current_process_->setReady();
                ready_queue_.push_back(current_process_);
                current_process_ = nullptr;
                context_switches_++;
            }
            // Check if process blocked itself
            else if (current_process_->isBlocked()) {
                // Move to blocked queue
                blocked_queue_.push_back(current_process_);
                current_process_ = nullptr;
                context_switches_++;
            }
            // Check if process terminated
            else if (current_process_->isZombie()) {
                // Process is done, don't reschedule
                current_process_ = nullptr;
                context_switches_++;
            }
        }
        
        // Pick next process if none running
        if (!current_process_) {
            current_process_ = schedule();
            
            if (current_process_) {
                // Context switch: restore and run
                current_process_->setRunning();
                current_process_->resetTimeSlice();
                current_process_->restoreContext();
                current_process_->last_scheduled = total_ticks_;
            } else {
                // No processes to run - idle
                idle_ticks_++;
            }
        }
        
        // Execute current process
        if (current_process_) {
            current_process_->execute();
        }
    }
    
    // ========== Statistics ==========
    
    uint32_t getProcessCount() const {
        uint32_t count = ready_queue_.size() + blocked_queue_.size();
        if (current_process_) count++;
        return count;
    }
    
    uint32_t getReadyCount() const {
        return ready_queue_.size();
    }
    
    uint32_t getBlockedCount() const {
        return blocked_queue_.size();
    }
    
    double getCPUUtilization() const {
        if (total_ticks_ == 0) return 0.0;
        return 100.0 * (total_ticks_ - idle_ticks_) / total_ticks_;
    }
    
    uint64_t getContextSwitches() const {
        return context_switches_;
    }
    
    OSProcess* getCurrentProcess() const {
        return current_process_;
    }
    
    // ========== Load Balancing Info ==========
    
    /**
     * Get a process that can be migrated to another torus.
     * Returns nullptr if no migratable processes.
     */
    OSProcess* pickMigratableProcess() {
        // Don't migrate if we only have a few processes
        if (getProcessCount() < 5) {
            return nullptr;
        }
        
        // Pick a ready process (not the currently running one)
        if (!ready_queue_.empty()) {
            OSProcess* proc = ready_queue_[ready_queue_.size() - 1];
            ready_queue_.erase_at(ready_queue_.size() - 1);
            return proc;
        }
        
        return nullptr;
    }
    
    /**
     * Receive a migrated process from another torus.
     */
    bool receiveProcess(OSProcess* proc) {
        if (!proc) return false;
        
        proc->torus_id = torus_id_;
        proc->setReady();
        return ready_queue_.push_back(proc);
    }
    
    // ========== Debugging ==========
    
    void printStatus() const {
        std::cout << "[TorusScheduler " << torus_id_ << "]"
                  << " Total=" << getProcessCount()
                  << " Ready=" << getReadyCount()
                  << " Blocked=" << getBlockedCount()
                  << " CPU=" << getCPUUtilization() << "%"
                  << " Switches=" << context_switches_
                  << std::endl;
        
        if (current_process_) {
            std::cout << "  Current: ";
            current_process_->print();
        }
    }
};

} // namespace os
