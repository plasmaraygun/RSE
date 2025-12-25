#pragma once

/**
 * ARQON Performance Benchmarking Suite
 * Phase 8: Optimization - benchmarks for real hardware
 */

#include <cstdint>
#include <cstring>

namespace perf {

// High-precision timing using TSC (Time Stamp Counter)
inline uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// Memory fence for accurate timing
inline void mfence() {
    asm volatile("mfence" ::: "memory");
}

// Get CPU frequency estimate (cycles per second)
inline uint64_t estimateCPUFrequency() {
    // Use PIT for calibration (rough estimate)
    // In real implementation, use HPET or TSC calibration
    return 3000000000ULL;  // Assume 3 GHz default
}

// Benchmark result
struct BenchResult {
    const char* name;
    uint64_t iterations;
    uint64_t total_cycles;
    uint64_t min_cycles;
    uint64_t max_cycles;
    double avg_cycles;
    double ops_per_sec;
    
    void print() const;
};

// Benchmark runner
template<typename Func>
BenchResult benchmark(const char* name, uint64_t iterations, Func&& func) {
    BenchResult result = {};
    result.name = name;
    result.iterations = iterations;
    result.min_cycles = UINT64_MAX;
    result.max_cycles = 0;
    
    // Warmup
    for (uint64_t i = 0; i < 100; i++) {
        func();
    }
    
    mfence();
    uint64_t start_total = rdtsc();
    
    for (uint64_t i = 0; i < iterations; i++) {
        mfence();
        uint64_t start = rdtsc();
        
        func();
        
        mfence();
        uint64_t end = rdtsc();
        
        uint64_t cycles = end - start;
        if (cycles < result.min_cycles) result.min_cycles = cycles;
        if (cycles > result.max_cycles) result.max_cycles = cycles;
    }
    
    mfence();
    uint64_t end_total = rdtsc();
    
    result.total_cycles = end_total - start_total;
    result.avg_cycles = static_cast<double>(result.total_cycles) / iterations;
    result.ops_per_sec = static_cast<double>(iterations) * estimateCPUFrequency() / result.total_cycles;
    
    return result;
}

// ============================================================================
// Memory Benchmarks
// ============================================================================

struct MemoryBenchmarks {
    static BenchResult sequentialRead(size_t buffer_size, uint64_t iterations) {
        uint8_t* buffer = new uint8_t[buffer_size];
        memset(buffer, 0xAA, buffer_size);
        
        volatile uint64_t sum = 0;
        
        auto result = benchmark("Sequential Read", iterations, [&]() {
            for (size_t i = 0; i < buffer_size; i += 64) {
                sum += buffer[i];
            }
        });
        
        delete[] buffer;
        return result;
    }
    
    static BenchResult sequentialWrite(size_t buffer_size, uint64_t iterations) {
        uint8_t* buffer = new uint8_t[buffer_size];
        
        auto result = benchmark("Sequential Write", iterations, [&]() {
            memset(buffer, 0x55, buffer_size);
        });
        
        delete[] buffer;
        return result;
    }
    
    static BenchResult randomAccess(size_t buffer_size, uint64_t iterations) {
        uint64_t* buffer = new uint64_t[buffer_size / 8];
        
        // Create random access pattern using LCG
        uint64_t seed = 12345;
        volatile uint64_t sum = 0;
        
        auto result = benchmark("Random Access", iterations, [&]() {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            size_t idx = (seed >> 16) % (buffer_size / 8);
            sum += buffer[idx];
        });
        
        delete[] buffer;
        return result;
    }
    
    static BenchResult memcpyBench(size_t buffer_size, uint64_t iterations) {
        uint8_t* src = new uint8_t[buffer_size];
        uint8_t* dst = new uint8_t[buffer_size];
        memset(src, 0xAA, buffer_size);
        
        auto result = benchmark("memcpy", iterations, [&]() {
            memcpy(dst, src, buffer_size);
        });
        
        delete[] src;
        delete[] dst;
        return result;
    }
};

// ============================================================================
// SIMD Benchmarks
// ============================================================================

struct SIMDBenchmarks {
    // Check for AVX support
    static bool hasAVX() {
        uint32_t eax, ebx, ecx, edx;
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        return (ecx & (1 << 28)) != 0;  // AVX bit
    }
    
    // Check for AVX2 support
    static bool hasAVX2() {
        uint32_t eax, ebx, ecx, edx;
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
        return (ebx & (1 << 5)) != 0;  // AVX2 bit
    }
    
    // Check for AVX-512 support
    static bool hasAVX512() {
        uint32_t eax, ebx, ecx, edx;
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
        return (ebx & (1 << 16)) != 0;  // AVX-512F bit
    }
    
    // Scalar addition
    static BenchResult scalarAdd(size_t count, uint64_t iterations) {
        float* a = new float[count];
        float* b = new float[count];
        float* c = new float[count];
        
        for (size_t i = 0; i < count; i++) {
            a[i] = static_cast<float>(i);
            b[i] = static_cast<float>(i * 2);
        }
        
        auto result = benchmark("Scalar Add", iterations, [&]() {
            for (size_t i = 0; i < count; i++) {
                c[i] = a[i] + b[i];
            }
        });
        
        delete[] a;
        delete[] b;
        delete[] c;
        return result;
    }
    
    // SSE addition (128-bit, 4 floats at a time)
    static BenchResult sseAdd(size_t count, uint64_t iterations) {
        alignas(16) float* a = new (std::align_val_t(16)) float[count];
        alignas(16) float* b = new (std::align_val_t(16)) float[count];
        alignas(16) float* c = new (std::align_val_t(16)) float[count];
        
        for (size_t i = 0; i < count; i++) {
            a[i] = static_cast<float>(i);
            b[i] = static_cast<float>(i * 2);
        }
        
        auto result = benchmark("SSE Add", iterations, [&]() {
            for (size_t i = 0; i < count; i += 4) {
                asm volatile(
                    "movaps (%0), %%xmm0\n"
                    "movaps (%1), %%xmm1\n"
                    "addps %%xmm1, %%xmm0\n"
                    "movaps %%xmm0, (%2)\n"
                    : : "r"(a + i), "r"(b + i), "r"(c + i)
                    : "xmm0", "xmm1", "memory"
                );
            }
        });
        
        operator delete[](a, std::align_val_t(16));
        operator delete[](b, std::align_val_t(16));
        operator delete[](c, std::align_val_t(16));
        return result;
    }
    
    // AVX addition (256-bit, 8 floats at a time)
    static BenchResult avxAdd(size_t count, uint64_t iterations) {
        if (!hasAVX()) {
            BenchResult r = {};
            r.name = "AVX Add (not supported)";
            return r;
        }
        
        alignas(32) float* a = new (std::align_val_t(32)) float[count];
        alignas(32) float* b = new (std::align_val_t(32)) float[count];
        alignas(32) float* c = new (std::align_val_t(32)) float[count];
        
        for (size_t i = 0; i < count; i++) {
            a[i] = static_cast<float>(i);
            b[i] = static_cast<float>(i * 2);
        }
        
        auto result = benchmark("AVX Add", iterations, [&]() {
            for (size_t i = 0; i < count; i += 8) {
                asm volatile(
                    "vmovaps (%0), %%ymm0\n"
                    "vmovaps (%1), %%ymm1\n"
                    "vaddps %%ymm1, %%ymm0, %%ymm0\n"
                    "vmovaps %%ymm0, (%2)\n"
                    : : "r"(a + i), "r"(b + i), "r"(c + i)
                    : "ymm0", "ymm1", "memory"
                );
            }
        });
        
        operator delete[](a, std::align_val_t(32));
        operator delete[](b, std::align_val_t(32));
        operator delete[](c, std::align_val_t(32));
        return result;
    }
    
    // Dot product benchmark
    static BenchResult dotProduct(size_t count, uint64_t iterations) {
        float* a = new float[count];
        float* b = new float[count];
        
        for (size_t i = 0; i < count; i++) {
            a[i] = static_cast<float>(i) * 0.001f;
            b[i] = static_cast<float>(i) * 0.002f;
        }
        
        volatile float result_sum = 0;
        
        auto result = benchmark("Dot Product", iterations, [&]() {
            float sum = 0;
            for (size_t i = 0; i < count; i++) {
                sum += a[i] * b[i];
            }
            result_sum = sum;
        });
        
        delete[] a;
        delete[] b;
        return result;
    }
};

// ============================================================================
// RSE-Specific Benchmarks
// ============================================================================

struct RSEBenchmarks {
    // Torus cell lookup
    static BenchResult torusCellLookup(uint64_t iterations) {
        // Simulate 32x32x32 torus
        constexpr size_t TORUS_SIZE = 32;
        uint64_t* torus = new uint64_t[TORUS_SIZE * TORUS_SIZE * TORUS_SIZE];
        
        // Initialize with pattern
        for (size_t i = 0; i < TORUS_SIZE * TORUS_SIZE * TORUS_SIZE; i++) {
            torus[i] = i * 0xDEADBEEF;
        }
        
        uint64_t seed = 12345;
        volatile uint64_t sum = 0;
        
        auto result = benchmark("Torus Cell Lookup", iterations, [&]() {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            uint32_t x = (seed >> 0) % TORUS_SIZE;
            uint32_t y = (seed >> 8) % TORUS_SIZE;
            uint32_t z = (seed >> 16) % TORUS_SIZE;
            sum += torus[x * TORUS_SIZE * TORUS_SIZE + y * TORUS_SIZE + z];
        });
        
        delete[] torus;
        return result;
    }
    
    // Projection computation
    static BenchResult projectionCompute(uint64_t iterations) {
        constexpr size_t PROJ_SIZE = 1024;
        uint64_t* state = new uint64_t[PROJ_SIZE];
        uint64_t* projection = new uint64_t[PROJ_SIZE / 8];
        
        for (size_t i = 0; i < PROJ_SIZE; i++) {
            state[i] = i * 0xCAFEBABE;
        }
        
        auto result = benchmark("Projection Compute", iterations, [&]() {
            // Simulate projection computation (hash-based compression)
            for (size_t i = 0; i < PROJ_SIZE / 8; i++) {
                uint64_t hash = 0;
                for (size_t j = 0; j < 8; j++) {
                    hash ^= state[i * 8 + j];
                    hash = (hash << 7) | (hash >> 57);
                    hash *= 0x9E3779B97F4A7C15ULL;
                }
                projection[i] = hash;
            }
        });
        
        delete[] state;
        delete[] projection;
        return result;
    }
    
    // Blake2b hash (crypto hot path)
    static BenchResult blake2bHash(uint64_t iterations) {
        uint8_t data[64];
        uint8_t hash[32];
        
        for (int i = 0; i < 64; i++) data[i] = i;
        
        auto result = benchmark("Blake2b-256 Hash", iterations, [&]() {
            // Simplified Blake2b simulation
            uint64_t h[4] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
                            0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL};
            
            for (int i = 0; i < 8; i++) {
                uint64_t m = *reinterpret_cast<uint64_t*>(data + i * 8);
                h[i % 4] ^= m;
                h[i % 4] = (h[i % 4] << 13) | (h[i % 4] >> 51);
                h[i % 4] *= 0x9E3779B97F4A7C15ULL;
            }
            
            memcpy(hash, h, 32);
        });
        
        return result;
    }
    
    // Ed25519 signature verification simulation
    static BenchResult ed25519Verify(uint64_t iterations) {
        uint8_t sig[64];
        uint8_t pubkey[32];
        uint8_t msg[128];
        
        for (int i = 0; i < 64; i++) sig[i] = i;
        for (int i = 0; i < 32; i++) pubkey[i] = i * 2;
        for (int i = 0; i < 128; i++) msg[i] = i * 3;
        
        volatile bool valid = false;
        
        auto result = benchmark("Ed25519 Verify (sim)", iterations, [&]() {
            // Simulate Ed25519 verification cost
            uint64_t h = 0;
            for (int i = 0; i < 64; i++) h ^= sig[i] * 0x9E3779B97F4A7C15ULL;
            for (int i = 0; i < 32; i++) h ^= pubkey[i] * 0xBB67AE8584CAA73BULL;
            for (int i = 0; i < 128; i++) h ^= msg[i] * 0x3C6EF372FE94F82BULL;
            
            // Simulate field operations
            for (int i = 0; i < 100; i++) {
                h = (h << 17) | (h >> 47);
                h *= 0x9E3779B97F4A7C15ULL;
            }
            
            valid = (h & 1) == 0;  // Dummy check
        });
        
        return result;
    }
};

// ============================================================================
// NUMA Awareness
// ============================================================================

struct NUMAInfo {
    uint32_t num_nodes;
    uint32_t current_node;
    uint64_t node_memory[8];  // Memory per node
    
    static NUMAInfo detect() {
        NUMAInfo info = {};
        
        // Check for NUMA via CPUID
        uint32_t eax, ebx, ecx, edx;
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000008));
        
        // For now, assume single node
        info.num_nodes = 1;
        info.current_node = 0;
        
        return info;
    }
    
    // Bind thread to NUMA node
    static void bindToNode(uint32_t node) {
        #ifdef __linux__
        // Linux userspace: use sched_setaffinity
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        // Set affinity to CPUs on the specified NUMA node
        // For simplicity, assume node 0 = CPUs 0-7, node 1 = CPUs 8-15, etc.
        for (int cpu = node * 8; cpu < (node + 1) * 8; cpu++) {
            CPU_SET(cpu, &cpuset);
        }
        sched_setaffinity(0, sizeof(cpuset), &cpuset);
        #else
        // Bare metal kernel: configure Local APIC for CPU affinity
        // This requires ACPI/SRAT parsing to know which CPUs belong to which node
        (void)node;  // Suppress unused warning in bare metal builds
        #endif
    }
};

// ============================================================================
// Run all benchmarks
// ============================================================================

inline void runAllBenchmarks() {
    auto& fb = drivers::getFramebuffer();
    
    fb.setColor(0xFFFF00, 0);
    fb.write("\n=== ARQON Performance Benchmarks ===\n\n");
    fb.setColor(0xFFFFFF, 0);
    
    // CPU info
    fb.write("SIMD Support:\n");
    fb.printf("  AVX:    %s\n", SIMDBenchmarks::hasAVX() ? "Yes" : "No");
    fb.printf("  AVX2:   %s\n", SIMDBenchmarks::hasAVX2() ? "Yes" : "No");
    fb.printf("  AVX512: %s\n", SIMDBenchmarks::hasAVX512() ? "Yes" : "No");
    fb.write("\n");
    
    // Memory benchmarks
    fb.setColor(0x00FF00, 0);
    fb.write("Memory Benchmarks (1MB buffer):\n");
    fb.setColor(0xFFFFFF, 0);
    
    auto mem_seq_r = MemoryBenchmarks::sequentialRead(1024 * 1024, 1000);
    fb.printf("  Sequential Read:  %.0f M ops/sec\n", mem_seq_r.ops_per_sec / 1000000.0);
    
    auto mem_seq_w = MemoryBenchmarks::sequentialWrite(1024 * 1024, 1000);
    fb.printf("  Sequential Write: %.0f M ops/sec\n", mem_seq_w.ops_per_sec / 1000000.0);
    
    auto mem_rand = MemoryBenchmarks::randomAccess(1024 * 1024, 100000);
    fb.printf("  Random Access:    %.0f M ops/sec\n", mem_rand.ops_per_sec / 1000000.0);
    
    fb.write("\n");
    
    // SIMD benchmarks
    fb.setColor(0x00FF00, 0);
    fb.write("SIMD Benchmarks (64K floats):\n");
    fb.setColor(0xFFFFFF, 0);
    
    auto scalar = SIMDBenchmarks::scalarAdd(65536, 1000);
    fb.printf("  Scalar:   %.0f M ops/sec\n", scalar.ops_per_sec / 1000000.0);
    
    auto sse = SIMDBenchmarks::sseAdd(65536, 1000);
    fb.printf("  SSE:      %.0f M ops/sec (%.1fx)\n", 
             sse.ops_per_sec / 1000000.0,
             sse.ops_per_sec / scalar.ops_per_sec);
    
    if (SIMDBenchmarks::hasAVX()) {
        auto avx = SIMDBenchmarks::avxAdd(65536, 1000);
        fb.printf("  AVX:      %.0f M ops/sec (%.1fx)\n",
                 avx.ops_per_sec / 1000000.0,
                 avx.ops_per_sec / scalar.ops_per_sec);
    }
    
    fb.write("\n");
    
    // RSE benchmarks
    fb.setColor(0x00FF00, 0);
    fb.write("RSE Benchmarks:\n");
    fb.setColor(0xFFFFFF, 0);
    
    auto torus = RSEBenchmarks::torusCellLookup(1000000);
    fb.printf("  Torus Cell Lookup: %.0f M ops/sec\n", torus.ops_per_sec / 1000000.0);
    
    auto proj = RSEBenchmarks::projectionCompute(10000);
    fb.printf("  Projection:        %.0f K ops/sec\n", proj.ops_per_sec / 1000.0);
    
    auto blake = RSEBenchmarks::blake2bHash(100000);
    fb.printf("  Blake2b-256:       %.0f K ops/sec\n", blake.ops_per_sec / 1000.0);
    
    auto ed25519 = RSEBenchmarks::ed25519Verify(10000);
    fb.printf("  Ed25519 Verify:    %.0f K ops/sec\n", ed25519.ops_per_sec / 1000.0);
    
    fb.write("\n");
    
    // NUMA info
    auto numa = NUMAInfo::detect();
    fb.setColor(0x00FF00, 0);
    fb.write("NUMA Configuration:\n");
    fb.setColor(0xFFFFFF, 0);
    fb.printf("  Nodes: %d\n", numa.num_nodes);
    fb.printf("  Current Node: %d\n", numa.current_node);
    
    fb.write("\n");
    fb.setColor(0xFFFF00, 0);
    fb.write("=== Benchmarks Complete ===\n");
    fb.setColor(0xFFFFFF, 0);
}

} // namespace perf
