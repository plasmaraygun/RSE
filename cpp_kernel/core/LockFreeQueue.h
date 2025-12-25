#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <optional>

namespace rse {

/**
 * LockFreeQueue: SPSC (Single-Producer Single-Consumer) lock-free queue.
 * 
 * For multi-producer scenarios, use with external synchronization on producer side.
 * Consumer side is always lock-free.
 * 
 * Memory ordering:
 * - Producer: release semantics on write
 * - Consumer: acquire semantics on read
 * - Ensures proper visibility across threads
 */
template<typename T, size_t CAPACITY>
class LockFreeQueue {
    static_assert(CAPACITY > 0 && (CAPACITY & (CAPACITY - 1)) == 0, 
                  "CAPACITY must be a power of 2 for efficient modulo");
    
public:
    LockFreeQueue() : head_(0), tail_(0) {}
    
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    
    // Try to enqueue an item. Returns false if queue is full.
    [[nodiscard]] bool tryPush(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (CAPACITY - 1);
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    [[nodiscard]] bool tryPush(T&& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (CAPACITY - 1);
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        buffer_[current_tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    // Try to dequeue an item. Returns nullopt if queue is empty.
    [[nodiscard]] std::optional<T> tryPop() {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt; // Queue is empty
        }
        
        T item = std::move(buffer_[current_head]);
        head_.store((current_head + 1) & (CAPACITY - 1), std::memory_order_release);
        return item;
    }
    
    // Check if queue is empty (may be stale immediately after return)
    [[nodiscard]] bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    // Approximate size (may be slightly inaccurate under concurrent access)
    [[nodiscard]] size_t size() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t - h + CAPACITY) & (CAPACITY - 1);
    }
    
    [[nodiscard]] constexpr size_t capacity() const { return CAPACITY - 1; } // One slot reserved
    
    void clear() {
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
    }

private:
    std::array<T, CAPACITY> buffer_{};
    alignas(64) std::atomic<size_t> head_; // Separate cache lines
    alignas(64) std::atomic<size_t> tail_; // to avoid false sharing
};

/**
 * MPMCQueue: Multi-Producer Multi-Consumer bounded queue.
 * 
 * Uses a ticket-based approach for ordering.
 * Higher overhead than SPSC but safe for any number of threads.
 */
template<typename T, size_t CAPACITY>
class MPMCQueue {
    static_assert(CAPACITY > 0 && (CAPACITY & (CAPACITY - 1)) == 0,
                  "CAPACITY must be a power of 2");
    
    struct Slot {
        std::atomic<size_t> turn{0};
        T data;
    };

public:
    MPMCQueue() : head_(0), tail_(0) {
        for (size_t i = 0; i < CAPACITY; ++i) {
            slots_[i].turn.store(i, std::memory_order_relaxed);
        }
    }
    
    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    
    [[nodiscard]] bool tryPush(const T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        while (true) {
            Slot& slot = slots_[tail & (CAPACITY - 1)];
            size_t turn = slot.turn.load(std::memory_order_acquire);
            
            if (turn == tail) {
                if (tail_.compare_exchange_weak(tail, tail + 1, 
                    std::memory_order_relaxed)) {
                    slot.data = item;
                    slot.turn.store(tail + 1, std::memory_order_release);
                    return true;
                }
            } else if (turn < tail) {
                return false; // Queue is full
            } else {
                tail = tail_.load(std::memory_order_relaxed);
            }
        }
    }
    
    [[nodiscard]] std::optional<T> tryPop() {
        size_t head = head_.load(std::memory_order_relaxed);
        
        while (true) {
            Slot& slot = slots_[head & (CAPACITY - 1)];
            size_t turn = slot.turn.load(std::memory_order_acquire);
            
            if (turn == head + 1) {
                if (head_.compare_exchange_weak(head, head + 1,
                    std::memory_order_relaxed)) {
                    T item = std::move(slot.data);
                    slot.turn.store(head + CAPACITY, std::memory_order_release);
                    return item;
                }
            } else if (turn < head + 1) {
                return std::nullopt; // Queue is empty
            } else {
                head = head_.load(std::memory_order_relaxed);
            }
        }
    }
    
    [[nodiscard]] bool empty() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return head == tail;
    }
    
    [[nodiscard]] size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return tail - head;
    }

private:
    std::array<Slot, CAPACITY> slots_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

} // namespace rse
