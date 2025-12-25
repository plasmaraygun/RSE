#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace rse {
namespace crypto {

/**
 * BLAKE3-inspired hash for NSA-grade integrity verification.
 * 
 * BLAKE3 is the successor to BLAKE2, designed for:
 * - High security (256-bit)
 * - High performance (faster than MD5)
 * - Parallelizable
 * - No length-extension attacks
 */

class Blake3 {
public:
    static constexpr size_t BLOCK_SIZE = 64;
    static constexpr size_t OUTPUT_SIZE = 32;
    static constexpr size_t KEY_SIZE = 32;
    
    using Hash = std::array<uint8_t, OUTPUT_SIZE>;
    using Key = std::array<uint8_t, KEY_SIZE>;
    
    // BLAKE3 IV constants
    static constexpr uint32_t IV[8] = {
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
    };
    
    // Message schedule permutation
    static constexpr uint8_t MSG_SCHEDULE[7][16] = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
        {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
        {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
        {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
        {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
        {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13}
    };

private:
    uint32_t state_[8];
    uint8_t buffer_[BLOCK_SIZE];
    size_t buffer_len_;
    uint64_t total_len_;
    uint32_t flags_;
    
    static constexpr uint32_t rotr(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }
    
    static void g(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d,
                  uint32_t mx, uint32_t my) {
        a = a + b + mx;
        d = rotr(d ^ a, 16);
        c = c + d;
        b = rotr(b ^ c, 12);
        a = a + b + my;
        d = rotr(d ^ a, 8);
        c = c + d;
        b = rotr(b ^ c, 7);
    }
    
    void compress(const uint32_t* block_words, uint64_t counter, uint32_t block_len, uint32_t flags) {
        uint32_t v[16];
        
        // Initialize working vector
        for (int i = 0; i < 8; i++) v[i] = state_[i];
        v[8] = IV[0];
        v[9] = IV[1];
        v[10] = IV[2];
        v[11] = IV[3];
        v[12] = static_cast<uint32_t>(counter);
        v[13] = static_cast<uint32_t>(counter >> 32);
        v[14] = block_len;
        v[15] = flags;
        
        // Rounds
        for (int round = 0; round < 7; round++) {
            const uint8_t* s = MSG_SCHEDULE[round];
            
            // Column step
            g(v[0], v[4], v[8],  v[12], block_words[s[0]],  block_words[s[1]]);
            g(v[1], v[5], v[9],  v[13], block_words[s[2]],  block_words[s[3]]);
            g(v[2], v[6], v[10], v[14], block_words[s[4]],  block_words[s[5]]);
            g(v[3], v[7], v[11], v[15], block_words[s[6]],  block_words[s[7]]);
            
            // Diagonal step
            g(v[0], v[5], v[10], v[15], block_words[s[8]],  block_words[s[9]]);
            g(v[1], v[6], v[11], v[12], block_words[s[10]], block_words[s[11]]);
            g(v[2], v[7], v[8],  v[13], block_words[s[12]], block_words[s[13]]);
            g(v[3], v[4], v[9],  v[14], block_words[s[14]], block_words[s[15]]);
        }
        
        // Finalize
        for (int i = 0; i < 8; i++) {
            state_[i] = v[i] ^ v[i + 8];
        }
    }

public:
    Blake3() { reset(); }
    
    void reset() {
        for (int i = 0; i < 8; i++) state_[i] = IV[i];
        buffer_len_ = 0;
        total_len_ = 0;
        flags_ = 0;
    }
    
    void update(const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        
        while (len > 0) {
            size_t to_copy = std::min(len, BLOCK_SIZE - buffer_len_);
            std::memcpy(buffer_ + buffer_len_, p, to_copy);
            buffer_len_ += to_copy;
            p += to_copy;
            len -= to_copy;
            total_len_ += to_copy;
            
            if (buffer_len_ == BLOCK_SIZE) {
                uint32_t block_words[16];
                for (int i = 0; i < 16; i++) {
                    block_words[i] = 
                        static_cast<uint32_t>(buffer_[i*4]) |
                        (static_cast<uint32_t>(buffer_[i*4+1]) << 8) |
                        (static_cast<uint32_t>(buffer_[i*4+2]) << 16) |
                        (static_cast<uint32_t>(buffer_[i*4+3]) << 24);
                }
                compress(block_words, total_len_ / BLOCK_SIZE - 1, BLOCK_SIZE, flags_);
                buffer_len_ = 0;
            }
        }
    }
    
    Hash finalize() {
        // Pad remaining buffer
        std::memset(buffer_ + buffer_len_, 0, BLOCK_SIZE - buffer_len_);
        
        uint32_t block_words[16];
        for (int i = 0; i < 16; i++) {
            block_words[i] = 
                static_cast<uint32_t>(buffer_[i*4]) |
                (static_cast<uint32_t>(buffer_[i*4+1]) << 8) |
                (static_cast<uint32_t>(buffer_[i*4+2]) << 16) |
                (static_cast<uint32_t>(buffer_[i*4+3]) << 24);
        }
        
        // Final compression with ROOT flag
        compress(block_words, 0, static_cast<uint32_t>(buffer_len_), flags_ | 0x08);
        
        Hash result;
        for (int i = 0; i < 8; i++) {
            result[i*4]   = static_cast<uint8_t>(state_[i]);
            result[i*4+1] = static_cast<uint8_t>(state_[i] >> 8);
            result[i*4+2] = static_cast<uint8_t>(state_[i] >> 16);
            result[i*4+3] = static_cast<uint8_t>(state_[i] >> 24);
        }
        return result;
    }
    
    // Convenience: hash in one call
    static Hash hash(const void* data, size_t len) {
        Blake3 hasher;
        hasher.update(data, len);
        return hasher.finalize();
    }
    
    // HMAC-like keyed hash
    static Hash keyedHash(const Key& key, const void* data, size_t len) {
        Blake3 hasher;
        // Use key as IV modification
        for (int i = 0; i < 8; i++) {
            hasher.state_[i] = IV[i] ^ 
                (static_cast<uint32_t>(key[i*4]) |
                 (static_cast<uint32_t>(key[i*4+1]) << 8) |
                 (static_cast<uint32_t>(key[i*4+2]) << 16) |
                 (static_cast<uint32_t>(key[i*4+3]) << 24));
        }
        hasher.flags_ = 0x10; // KEYED_HASH flag
        hasher.update(data, len);
        return hasher.finalize();
    }
};

// Constant-time hash comparison
[[nodiscard]] inline bool constantTimeHashCompare(const Blake3::Hash& a, const Blake3::Hash& b) {
    volatile uint8_t result = 0;
    for (size_t i = 0; i < Blake3::OUTPUT_SIZE; i++) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

// Convert hash to hex string for logging
inline std::array<char, 65> hashToHex(const Blake3::Hash& hash) {
    static constexpr char hex[] = "0123456789abcdef";
    std::array<char, 65> result;
    for (size_t i = 0; i < 32; i++) {
        result[i*2] = hex[hash[i] >> 4];
        result[i*2+1] = hex[hash[i] & 0xF];
    }
    result[64] = '\0';
    return result;
}

} // namespace crypto
} // namespace rse
