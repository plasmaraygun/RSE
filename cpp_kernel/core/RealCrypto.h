#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <array>
#include <string>
#include <vector>
#include <stdexcept>

/**
 * Production-Grade Cryptography using libsodium
 * 
 * IMPORTANT: This requires libsodium to be installed:
 *   sudo apt-get install libsodium-dev  (Ubuntu/Debian)
 *   brew install libsodium              (macOS)
 * 
 * Link with: -lsodium
 */

#ifdef USE_LIBSODIUM
#include <sodium.h>
#else
// Fallback to simplified crypto if libsodium not available
#warning "libsodium not found, using simplified crypto (NOT SECURE FOR PRODUCTION)"
#include "Crypto.h"
namespace crypto_real = crypto;
#endif

namespace crypto_real {

#ifdef USE_LIBSODIUM

// ============================================================================
// Constants (libsodium)
// ============================================================================

constexpr size_t PRIVATE_KEY_SIZE = crypto_sign_SECRETKEYBYTES;  // 64 bytes
constexpr size_t PUBLIC_KEY_SIZE = crypto_sign_PUBLICKEYBYTES;   // 32 bytes
constexpr size_t SIGNATURE_SIZE = crypto_sign_BYTES;             // 64 bytes
constexpr size_t ADDRESS_SIZE = 20;  // First 20 bytes of SHA-256(pubkey)
constexpr size_t HASH_SIZE = crypto_hash_sha256_BYTES;           // 32 bytes

using PrivateKey = std::array<uint8_t, PRIVATE_KEY_SIZE>;
using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
using Address = std::array<uint8_t, ADDRESS_SIZE>;
using Hash256 = std::array<uint8_t, HASH_SIZE>;

// ============================================================================
// Initialization
// ============================================================================

class CryptoInit {
private:
    static bool initialized_;
    
public:
    static void init() {
        if (!initialized_) {
            if (sodium_init() < 0) {
                throw std::runtime_error("libsodium initialization failed");
            }
            initialized_ = true;
        }
    }
    
    static bool isInitialized() { return initialized_; }
};

bool CryptoInit::initialized_ = false;

// ============================================================================
// SHA-256 Hashing
// ============================================================================

class SHA256 {
public:
    static Hash256 hash(const uint8_t* data, size_t len) {
        CryptoInit::init();
        
        Hash256 result;
        crypto_hash_sha256(result.data(), data, len);
        return result;
    }
    
    static Hash256 hash(const std::string& str) {
        return hash(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }
    
    static Hash256 hash(const std::vector<uint8_t>& data) {
        return hash(data.data(), data.size());
    }
    
    // Double SHA-256 (Bitcoin-style)
    static Hash256 doubleHash(const uint8_t* data, size_t len) {
        auto first = hash(data, len);
        return hash(first.data(), first.size());
    }
};

// ============================================================================
// Ed25519 Key Pair
// ============================================================================

class KeyPair {
private:
    PrivateKey private_key_;
    PublicKey public_key_;
    Address address_;
    
    void deriveAddress() {
        // Address = first 20 bytes of SHA-256(public_key)
        auto hash = SHA256::hash(public_key_.data(), public_key_.size());
        std::memcpy(address_.data(), hash.data(), ADDRESS_SIZE);
    }
    
public:
    KeyPair() {
        generate();
    }
    
    // Generate new keypair
    void generate() {
        CryptoInit::init();
        
        crypto_sign_keypair(public_key_.data(), private_key_.data());
        deriveAddress();
    }
    
    // Load from existing private key
    void loadPrivateKey(const PrivateKey& key) {
        CryptoInit::init();
        
        private_key_ = key;
        
        // Extract public key from private key
        // In Ed25519, the private key contains the public key in the last 32 bytes
        std::memcpy(public_key_.data(), private_key_.data() + 32, PUBLIC_KEY_SIZE);
        
        deriveAddress();
    }
    
    // Load from seed (32 bytes)
    void loadSeed(const uint8_t* seed) {
        CryptoInit::init();
        
        crypto_sign_seed_keypair(public_key_.data(), private_key_.data(), seed);
        deriveAddress();
    }
    
    const PrivateKey& getPrivateKey() const { return private_key_; }
    const PublicKey& getPublicKey() const { return public_key_; }
    const Address& getAddress() const { return address_; }
    
    // Sign a message (detached signature)
    Signature sign(const uint8_t* message, size_t len) const {
        CryptoInit::init();
        
        Signature sig;
        unsigned long long sig_len;
        
        crypto_sign_detached(sig.data(), &sig_len, message, len, private_key_.data());
        
        return sig;
    }
    
    // Verify a signature (static method)
    static bool verify(const PublicKey& pubkey, const uint8_t* message, size_t len, const Signature& sig) {
        CryptoInit::init();
        
        return crypto_sign_verify_detached(sig.data(), message, len, pubkey.data()) == 0;
    }
    
    // Export private key to file (encrypted with password)
    bool exportEncrypted(const std::string& filename, const std::string& password) const {
        // Use libsodium's password-based encryption with proper salt
        
        // Generate random salt for password hashing (SEPARATE from nonce)
        std::array<uint8_t, crypto_pwhash_SALTBYTES> salt;
        randombytes_buf(salt.data(), salt.size());
        
        // Generate random nonce for encryption
        std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce;
        randombytes_buf(nonce.data(), nonce.size());
        
        // Derive key from password using salt
        std::array<uint8_t, crypto_secretbox_KEYBYTES> key;
        if (crypto_pwhash(
            key.data(), key.size(),
            password.c_str(), password.size(),
            salt.data(),  // Use salt, NOT nonce
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE,
            crypto_pwhash_ALG_DEFAULT
        ) != 0) {
            return false;  // Out of memory
        }
        
        // Encrypt private key
        std::vector<uint8_t> ciphertext(PRIVATE_KEY_SIZE + crypto_secretbox_MACBYTES);
        crypto_secretbox_easy(
            ciphertext.data(),
            private_key_.data(), PRIVATE_KEY_SIZE,
            nonce.data(),
            key.data()
        );
        
        // Securely clear the derived key
        sodium_memzero(key.data(), key.size());
        
        // Write to file: salt + nonce + ciphertext
        FILE* f = fopen(filename.c_str(), "wb");
        if (!f) return false;
        
        fwrite(salt.data(), 1, salt.size(), f);
        fwrite(nonce.data(), 1, nonce.size(), f);
        fwrite(ciphertext.data(), 1, ciphertext.size(), f);
        fclose(f);
        
        return true;
    }
    
    // Import private key from encrypted file
    bool importEncrypted(const std::string& filename, const std::string& password) {
        FILE* f = fopen(filename.c_str(), "rb");
        if (!f) return false;
        
        // Read salt
        std::array<uint8_t, crypto_pwhash_SALTBYTES> salt;
        if (fread(salt.data(), 1, salt.size(), f) != salt.size()) {
            fclose(f);
            return false;
        }
        
        // Read nonce
        std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce;
        if (fread(nonce.data(), 1, nonce.size(), f) != nonce.size()) {
            fclose(f);
            return false;
        }
        
        // Read ciphertext
        std::vector<uint8_t> ciphertext(PRIVATE_KEY_SIZE + crypto_secretbox_MACBYTES);
        if (fread(ciphertext.data(), 1, ciphertext.size(), f) != ciphertext.size()) {
            fclose(f);
            return false;
        }
        fclose(f);
        
        // Derive key from password using salt
        std::array<uint8_t, crypto_secretbox_KEYBYTES> key;
        if (crypto_pwhash(
            key.data(), key.size(),
            password.c_str(), password.size(),
            salt.data(),  // Use salt, NOT nonce
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE,
            crypto_pwhash_ALG_DEFAULT
        ) != 0) {
            return false;
        }
        
        // Decrypt
        PrivateKey decrypted;
        if (crypto_secretbox_open_easy(
            decrypted.data(),
            ciphertext.data(), ciphertext.size(),
            nonce.data(),
            key.data()
        ) != 0) {
            // Securely clear key before returning
            sodium_memzero(key.data(), key.size());
            return false;  // Wrong password or corrupted file
        }
        
        // Securely clear key
        sodium_memzero(key.data(), key.size());
        
        loadPrivateKey(decrypted);
        
        // Securely clear decrypted key from stack
        sodium_memzero(decrypted.data(), decrypted.size());
        
        return true;
    }
    
    // Destructor - securely clear private key
    ~KeyPair() {
        sodium_memzero(private_key_.data(), private_key_.size());
    }
    
    // Delete copy constructor/assignment to prevent accidental key copies
    KeyPair(const KeyPair&) = delete;
    KeyPair& operator=(const KeyPair&) = delete;
    
    // Allow move
    KeyPair(KeyPair&& other) noexcept {
        private_key_ = other.private_key_;
        public_key_ = other.public_key_;
        address_ = other.address_;
        sodium_memzero(other.private_key_.data(), other.private_key_.size());
    }
    
    KeyPair& operator=(KeyPair&& other) noexcept {
        if (this != &other) {
            sodium_memzero(private_key_.data(), private_key_.size());
            private_key_ = other.private_key_;
            public_key_ = other.public_key_;
            address_ = other.address_;
            sodium_memzero(other.private_key_.data(), other.private_key_.size());
        }
        return *this;
    }
};

// ============================================================================
// Address Utilities
// ============================================================================

class AddressUtil {
public:
    // Convert address to hex string
    static std::string toHex(const Address& addr) {
        static const char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(ADDRESS_SIZE * 2 + 2);
        result += "0x";
        
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            result += hex[(addr[i] >> 4) & 0xF];
            result += hex[addr[i] & 0xF];
        }
        
        return result;
    }
    
    // Parse hex string to address
    static bool fromHex(const std::string& hex, Address& addr) {
        if (hex.size() != ADDRESS_SIZE * 2 + 2) return false;
        if (hex[0] != '0' || hex[1] != 'x') return false;
        
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            char high = hex[2 + i * 2];
            char low = hex[2 + i * 2 + 1];
            
            auto hexval = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            
            int h = hexval(high);
            int l = hexval(low);
            if (h < 0 || l < 0) return false;
            
            addr[i] = static_cast<uint8_t>((h << 4) | l);
        }
        
        return true;
    }
    
    // Check if address is zero
    static bool isZero(const Address& addr) {
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            if (addr[i] != 0) return false;
        }
        return true;
    }
    
    // Generate checksum address (EIP-55 style)
    static std::string toChecksumHex(const Address& addr) {
        std::string hex_lower = toHex(addr);
        auto hash = SHA256::hash(hex_lower.substr(2));  // Hash without "0x"
        
        std::string result = "0x";
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            char high = hex_lower[2 + i * 2];
            char low = hex_lower[2 + i * 2 + 1];
            
            // Capitalize if hash bit is set
            if (hash[i / 2] & (i % 2 == 0 ? 0x80 : 0x08)) {
                high = toupper(high);
            }
            if (hash[i / 2] & (i % 2 == 0 ? 0x40 : 0x04)) {
                low = toupper(low);
            }
            
            result += high;
            result += low;
        }
        
        return result;
    }
};

// ============================================================================
// Transaction Structure (same as before, but with real crypto)
// ============================================================================

struct Transaction {
    Address from;
    Address to;
    uint64_t value;
    uint64_t gas_price;
    uint64_t gas_limit;
    uint64_t nonce;
    
    std::array<uint8_t, 256> data;
    size_t data_len;
    
    PublicKey public_key;
    Signature signature;
    
    Transaction() : value(0), gas_price(0), gas_limit(0), nonce(0), data_len(0) {
        from.fill(0);
        to.fill(0);
        data.fill(0);
        public_key.fill(0);
        signature.fill(0);
    }
    
    void serialize(uint8_t* buffer, size_t& len) const {
        size_t pos = 0;
        
        std::memcpy(buffer + pos, from.data(), ADDRESS_SIZE);
        pos += ADDRESS_SIZE;
        
        std::memcpy(buffer + pos, to.data(), ADDRESS_SIZE);
        pos += ADDRESS_SIZE;
        
        std::memcpy(buffer + pos, &value, 8);
        pos += 8;
        
        std::memcpy(buffer + pos, &gas_price, 8);
        pos += 8;
        
        std::memcpy(buffer + pos, &gas_limit, 8);
        pos += 8;
        
        std::memcpy(buffer + pos, &nonce, 8);
        pos += 8;
        
        std::memcpy(buffer + pos, &data_len, 8);
        pos += 8;
        
        std::memcpy(buffer + pos, data.data(), data_len);
        pos += data_len;
        
        len = pos;
    }
    
    void sign(const KeyPair& keypair) {
        from = keypair.getAddress();
        public_key = keypair.getPublicKey();
        
        uint8_t buffer[1024];
        size_t len;
        serialize(buffer, len);
        
        signature = keypair.sign(buffer, len);
    }
    
    bool verify() const {
        uint8_t buffer[1024];
        size_t len;
        serialize(buffer, len);
        
        return KeyPair::verify(public_key, buffer, len, signature);
    }
    
    Hash256 hash() const {
        uint8_t buffer[1024];
        size_t len;
        serialize(buffer, len);
        
        // Include signature in hash
        std::memcpy(buffer + len, signature.data(), SIGNATURE_SIZE);
        len += SIGNATURE_SIZE;
        
        return SHA256::hash(buffer, len);
    }
};

// ============================================================================
// Account State
// ============================================================================

struct Account {
    Address address;
    uint64_t balance;
    uint64_t nonce;
    uint64_t stake;
    
    Account() : balance(0), nonce(0), stake(0) {
        address.fill(0);
    }
    
    Account(const Address& addr) : address(addr), balance(0), nonce(0), stake(0) {}
};

#endif // USE_LIBSODIUM

} // namespace crypto_real
