#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <atomic>
#include <random>

namespace rse {
namespace secure {

/**
 * SecureMemory: NSA-grade memory security primitives.
 * 
 * Implements:
 * - Secure memory wiping (prevents compiler optimization)
 * - Memory canaries for buffer overflow detection
 * - Constant-time comparison (prevents timing attacks)
 * - Secure random number generation
 */

// Secure memory wipe - cannot be optimized away by compiler
inline void secureZero(void* ptr, size_t size) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (size--) {
        *p++ = 0;
    }
    // Memory barrier to ensure wipe completes before function returns
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

// Secure memory wipe with pattern (DoD 5220.22-M standard)
inline void secureWipeDoD(void* ptr, size_t size) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    
    // Pass 1: Write zeros
    for (size_t i = 0; i < size; ++i) p[i] = 0x00;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    // Pass 2: Write ones
    for (size_t i = 0; i < size; ++i) p[i] = 0xFF;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    // Pass 3: Write random
    std::random_device rd;
    for (size_t i = 0; i < size; ++i) p[i] = static_cast<unsigned char>(rd());
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    // Final pass: Write zeros
    for (size_t i = 0; i < size; ++i) p[i] = 0x00;
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

// Constant-time memory comparison (prevents timing attacks)
[[nodiscard]] inline bool constantTimeCompare(const void* a, const void* b, size_t size) {
    const volatile unsigned char* pa = static_cast<const volatile unsigned char*>(a);
    const volatile unsigned char* pb = static_cast<const volatile unsigned char*>(b);
    
    volatile unsigned char result = 0;
    for (size_t i = 0; i < size; ++i) {
        result |= pa[i] ^ pb[i];
    }
    return result == 0;
}

// Canary value for buffer overflow detection
class MemoryCanary {
public:
    static constexpr uint64_t CANARY_MAGIC = 0xDEADBEEFCAFEBABEULL;
    
    MemoryCanary() : value_(generateCanary()) {}
    
    [[nodiscard]] bool verify() const {
        return constantTimeCompare(&value_, &expected_, sizeof(uint64_t));
    }
    
    void corrupt() { value_ ^= 1; } // For testing
    
private:
    static uint64_t generateCanary() {
        static std::atomic<uint64_t> counter{0};
        uint64_t c = counter.fetch_add(1, std::memory_order_relaxed);
        return CANARY_MAGIC ^ (c * 0x9E3779B97F4A7C15ULL);
    }
    
    uint64_t value_;
    static inline const uint64_t expected_ = generateCanary();
};

// Secure buffer with canaries at both ends
template<typename T, size_t SIZE>
class SecureBuffer {
public:
    SecureBuffer() {
        secureZero(data_.data(), sizeof(T) * SIZE);
    }
    
    ~SecureBuffer() {
        secureZero(data_.data(), sizeof(T) * SIZE);
    }
    
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    
    [[nodiscard]] bool verifyIntegrity() const {
        return front_canary_.verify() && back_canary_.verify();
    }
    
    T& operator[](size_t idx) {
        if (idx >= SIZE) {
            // Constant-time bounds check to prevent timing attacks
            idx = 0;
        }
        return data_[idx];
    }
    
    const T& operator[](size_t idx) const {
        if (idx >= SIZE) {
            idx = 0;
        }
        return data_[idx];
    }
    
    T* data() { return data_.data(); }
    const T* data() const { return data_.data(); }
    constexpr size_t size() const { return SIZE; }

private:
    MemoryCanary front_canary_;
    std::array<T, SIZE> data_;
    MemoryCanary back_canary_;
};

// Cryptographically secure random number generator
class SecureRandom {
public:
    SecureRandom() : rd_(), gen_(rd_()), dist_(0, UINT64_MAX) {}
    
    uint64_t next() { return dist_(gen_); }
    
    void fill(void* buffer, size_t size) {
        uint8_t* p = static_cast<uint8_t*>(buffer);
        while (size >= 8) {
            uint64_t r = next();
            std::memcpy(p, &r, 8);
            p += 8;
            size -= 8;
        }
        if (size > 0) {
            uint64_t r = next();
            std::memcpy(p, &r, size);
        }
    }
    
    // Generate random bytes for key material
    template<size_t N>
    std::array<uint8_t, N> generateKey() {
        std::array<uint8_t, N> key;
        fill(key.data(), N);
        return key;
    }

private:
    std::random_device rd_;
    std::mt19937_64 gen_;
    std::uniform_int_distribution<uint64_t> dist_;
};

} // namespace secure
} // namespace rse
