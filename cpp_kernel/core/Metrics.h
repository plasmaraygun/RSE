#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <array>

namespace rse {

/**
 * Metrics: Lock-free telemetry for RSE system observability.
 * 
 * All counters are atomic for thread-safe access without locks.
 * Histograms use fixed buckets for O(1) bounded memory.
 */
struct MetricCounter {
    std::atomic<uint64_t> value{0};
    
    void increment(uint64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }
    void decrement(uint64_t delta = 1) { value.fetch_sub(delta, std::memory_order_relaxed); }
    uint64_t get() const { return value.load(std::memory_order_relaxed); }
    void reset() { value.store(0, std::memory_order_relaxed); }
};

struct MetricGauge {
    std::atomic<int64_t> value{0};
    
    void set(int64_t v) { value.store(v, std::memory_order_relaxed); }
    void increment(int64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }
    void decrement(int64_t delta = 1) { value.fetch_sub(delta, std::memory_order_relaxed); }
    int64_t get() const { return value.load(std::memory_order_relaxed); }
};

// Fixed-bucket histogram for latency measurement
template<size_t NUM_BUCKETS = 16>
struct MetricHistogram {
    static constexpr size_t BUCKET_COUNT = NUM_BUCKETS;
    std::array<std::atomic<uint64_t>, NUM_BUCKETS> buckets{};
    std::atomic<uint64_t> total_count{0};
    std::atomic<uint64_t> total_sum{0};
    
    // Bucket boundaries: 0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, inf
    void observe(uint64_t value_us) {
        size_t bucket = 0;
        if (value_us > 0) {
            uint64_t threshold = 1;
            while (bucket < NUM_BUCKETS - 1 && value_us > threshold) {
                threshold *= 2;
                bucket++;
            }
        }
        buckets[bucket].fetch_add(1, std::memory_order_relaxed);
        total_count.fetch_add(1, std::memory_order_relaxed);
        total_sum.fetch_add(value_us, std::memory_order_relaxed);
    }
    
    double mean() const {
        uint64_t count = total_count.load(std::memory_order_relaxed);
        return count > 0 ? static_cast<double>(total_sum.load()) / count : 0.0;
    }
    
    void reset() {
        for (auto& b : buckets) b.store(0, std::memory_order_relaxed);
        total_count.store(0, std::memory_order_relaxed);
        total_sum.store(0, std::memory_order_relaxed);
    }
};

/**
 * SystemMetrics: Centralized metrics for the RSE system.
 */
class SystemMetrics {
public:
    // Event processing
    MetricCounter events_processed;
    MetricCounter events_injected;
    MetricCounter events_dropped;
    MetricHistogram<16> event_latency_us;
    
    // Torus health
    MetricGauge active_tori;
    MetricCounter torus_failures;
    MetricCounter torus_reconstructions;
    MetricHistogram<16> reconstruction_time_us;
    
    // Braid coordination
    MetricCounter projection_exchanges;
    MetricCounter consistency_violations;
    MetricCounter boundary_corrections;
    MetricHistogram<16> exchange_latency_us;
    
    // Memory
    MetricGauge processes_allocated;
    MetricGauge edges_allocated;
    MetricGauge events_queued;
    
    // Singleton access
    static SystemMetrics& instance() {
        static SystemMetrics metrics;
        return metrics;
    }
    
    void reset() {
        events_processed.reset();
        events_injected.reset();
        events_dropped.reset();
        event_latency_us.reset();
        active_tori.set(0);
        torus_failures.reset();
        torus_reconstructions.reset();
        reconstruction_time_us.reset();
        projection_exchanges.reset();
        consistency_violations.reset();
        boundary_corrections.reset();
        exchange_latency_us.reset();
        processes_allocated.set(0);
        edges_allocated.set(0);
        events_queued.set(0);
    }

private:
    SystemMetrics() = default;
};

// Convenience macros for metric access
#define RSE_METRICS rse::SystemMetrics::instance()

// RAII timer for latency measurement
class ScopedTimer {
public:
    explicit ScopedTimer(MetricHistogram<16>& histogram) 
        : histogram_(histogram)
        , start_(std::chrono::high_resolution_clock::now()) {}
    
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        histogram_.observe(static_cast<uint64_t>(us));
    }

private:
    MetricHistogram<16>& histogram_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace rse
