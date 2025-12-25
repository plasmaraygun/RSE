#pragma once

/**
 * Production Crypto with libsodium
 * 
 * Real Ed25519 signatures using libsodium
 * Compile with: -lsodium
 */

#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>

#ifdef USE_LIBSODIUM
#include <sodium.h>
#endif

namespace crypto {

constexpr size_t SEED_SIZE = 32;
constexpr size_t PRIVKEY_SIZE = 64;  // Ed25519 secret key
constexpr size_t PUBKEY_SIZE = 32;   // Ed25519 public key
constexpr size_t SIG_SIZE = 64;      // Ed25519 signature
constexpr size_t ADDR_SIZE = 20;     // Address (hash of pubkey)

using Seed = std::array<uint8_t, SEED_SIZE>;
using PrivateKey = std::array<uint8_t, PRIVKEY_SIZE>;
using PublicKey = std::array<uint8_t, PUBKEY_SIZE>;
using Signature = std::array<uint8_t, SIG_SIZE>;
using Address = std::array<uint8_t, ADDR_SIZE>;

#ifdef USE_LIBSODIUM

class SodiumInit {
public:
    SodiumInit() {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }
    }
    static SodiumInit& instance() {
        static SodiumInit init;
        return init;
    }
};

class KeyPair {
public:
    KeyPair() {
        SodiumInit::instance();
        crypto_sign_keypair(public_key_.data(), private_key_.data());
        deriveAddress();
    }
    
    explicit KeyPair(const Seed& seed) {
        SodiumInit::instance();
        crypto_sign_seed_keypair(public_key_.data(), private_key_.data(), seed.data());
        deriveAddress();
    }
    
    Signature sign(const uint8_t* message, size_t len) const {
        Signature sig;
        unsigned long long sig_len;
        crypto_sign_detached(sig.data(), &sig_len, message, len, private_key_.data());
        return sig;
    }
    
    static bool verify(const PublicKey& pubkey, const uint8_t* message, size_t len, const Signature& sig) {
        SodiumInit::instance();
        return crypto_sign_verify_detached(sig.data(), message, len, pubkey.data()) == 0;
    }
    
    const PublicKey& getPublicKey() const { return public_key_; }
    const Address& getAddress() const { return address_; }

private:
    void deriveAddress() {
        // BLAKE2b hash of public key, take first 20 bytes
        uint8_t hash[32];
        crypto_generichash(hash, sizeof(hash), public_key_.data(), public_key_.size(), nullptr, 0);
        std::memcpy(address_.data(), hash, ADDR_SIZE);
    }
    
    PrivateKey private_key_;
    PublicKey public_key_;
    Address address_;
};

// Secure random bytes
inline void randomBytes(uint8_t* buf, size_t len) {
    SodiumInit::instance();
    randombytes_buf(buf, len);
}

// Secure memory wipe
inline void secureZero(void* ptr, size_t len) {
    sodium_memzero(ptr, len);
}

// Constant-time comparison
inline bool secureCompare(const uint8_t* a, const uint8_t* b, size_t len) {
    SodiumInit::instance();
    return sodium_memcmp(a, b, len) == 0;
}

// BLAKE2b hash
inline std::array<uint8_t, 32> hash256(const uint8_t* data, size_t len) {
    std::array<uint8_t, 32> out;
    crypto_generichash(out.data(), 32, data, len, nullptr, 0);
    return out;
}

#else
// Fallback to simplified crypto when libsodium not available
#include "../core/Crypto.h"
using KeyPair = crypto::KeyPair;

inline void randomBytes(uint8_t* buf, size_t len) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    for (size_t i = 0; i < len; i++) buf[i] = dis(gen);
}

inline void secureZero(void* ptr, size_t len) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) *p++ = 0;
}

inline bool secureCompare(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

inline std::array<uint8_t, 32> hash256(const uint8_t* data, size_t len) {
    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < len; i++) {
        out[i % 32] ^= data[i];
        out[(i + 1) % 32] = (out[(i + 1) % 32] * 31 + data[i]) & 0xFF;
    }
    return out;
}

#endif

// Address utilities
struct AddressUtil {
    static std::string toHex(const Address& addr) {
        static const char hex[] = "0123456789abcdef";
        std::string out = "0x";
        out.reserve(42);
        for (uint8_t b : addr) {
            out += hex[b >> 4];
            out += hex[b & 0xF];
        }
        return out;
    }
    
    static Address fromHex(const std::string& hex) {
        Address addr{};
        size_t start = (hex.size() >= 2 && hex[0] == '0' && hex[1] == 'x') ? 2 : 0;
        for (size_t i = 0; i < ADDR_SIZE && (start + i * 2 + 1) < hex.size(); i++) {
            char hi = hex[start + i * 2];
            char lo = hex[start + i * 2 + 1];
            auto hexVal = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return 0;
            };
            addr[i] = (hexVal(hi) << 4) | hexVal(lo);
        }
        return addr;
    }
    
    static bool isZero(const Address& addr) {
        for (uint8_t b : addr) if (b != 0) return false;
        return true;
    }
};

} // namespace crypto
