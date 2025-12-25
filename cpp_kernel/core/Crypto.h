#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <string>
#include <stdexcept>
#include <sodium.h>

/**
 * Cryptographic Identity System for ARQON
 * 
 * Real Ed25519 signatures and BLAKE2b hashing via libsodium.
 * Compile with: -lsodium
 */

namespace crypto {

// Initialize libsodium (call once at startup)
inline bool init_crypto() {
    static bool initialized = false;
    if (!initialized) {
        if (sodium_init() < 0) {
            return false;
        }
        initialized = true;
    }
    return true;
}

// ============================================================================
// Real Ed25519 Cryptography via libsodium
// ============================================================================

constexpr size_t PRIVATE_KEY_SIZE = crypto_sign_SECRETKEYBYTES;  // 64 bytes
constexpr size_t PUBLIC_KEY_SIZE = crypto_sign_PUBLICKEYBYTES;   // 32 bytes
constexpr size_t SIGNATURE_SIZE = crypto_sign_BYTES;             // 64 bytes
constexpr size_t SEED_SIZE = crypto_sign_SEEDBYTES;              // 32 bytes
constexpr size_t ADDRESS_SIZE = 20;  // First 20 bytes of pubkey hash (like Ethereum)
constexpr size_t HASH_SIZE = crypto_generichash_BYTES;           // 32 bytes

using PrivateKey = std::array<uint8_t, PRIVATE_KEY_SIZE>;
using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
using Seed = std::array<uint8_t, SEED_SIZE>;
using Address = std::array<uint8_t, ADDRESS_SIZE>;
using Hash = std::array<uint8_t, HASH_SIZE>;

// ============================================================================
// Real BLAKE2b Hash Function via libsodium
// ============================================================================

class Blake2b {
public:
    static Hash hash(const uint8_t* data, size_t len) {
        Hash result{};
        crypto_generichash(result.data(), HASH_SIZE, data, len, nullptr, 0);
        return result;
    }
    
    static Hash hash(const std::string& str) {
        return hash(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }
    
    // Hash with key (for MACs)
    static Hash hash_keyed(const uint8_t* data, size_t len, 
                           const uint8_t* key, size_t key_len) {
        Hash result{};
        crypto_generichash(result.data(), HASH_SIZE, data, len, key, key_len);
        return result;
    }
};

// Alias for backward compatibility
using SimpleHash = Blake2b;

// ============================================================================
// Real Ed25519 Key Generation via libsodium
// ============================================================================

class KeyPair {
private:
    PrivateKey secret_key_;  // 64-byte Ed25519 secret key
    PublicKey public_key_;   // 32-byte Ed25519 public key
    Address address_;        // 20-byte address derived from pubkey
    
public:
    KeyPair() {
        init_crypto();
        generate();
    }
    
    // Generate new keypair using real Ed25519
    void generate() {
        crypto_sign_keypair(public_key_.data(), secret_key_.data());
        deriveAddress();
    }
    
    // Generate from seed (deterministic)
    void generateFromSeed(const Seed& seed) {
        crypto_sign_seed_keypair(public_key_.data(), secret_key_.data(), seed.data());
        deriveAddress();
    }
    
    // Load from existing secret key (64 bytes)
    void loadSecretKey(const PrivateKey& key) {
        secret_key_ = key;
        // Extract public key from secret key (last 32 bytes)
        std::memcpy(public_key_.data(), secret_key_.data() + 32, PUBLIC_KEY_SIZE);
        deriveAddress();
    }
    
    const PrivateKey& getPrivateKey() const { return secret_key_; }
    const PrivateKey& getSecretKey() const { return secret_key_; }
    const PublicKey& getPublicKey() const { return public_key_; }
    const Address& getAddress() const { return address_; }
    
    // Real Ed25519 signature
    Signature sign(const uint8_t* message, size_t len) const {
        Signature sig{};
        unsigned long long sig_len;
        crypto_sign_detached(sig.data(), &sig_len, message, len, secret_key_.data());
        return sig;
    }
    
    // Real Ed25519 verification
    static bool verify(const PublicKey& pubkey, const uint8_t* message, size_t len, const Signature& sig) {
        return crypto_sign_verify_detached(sig.data(), message, len, pubkey.data()) == 0;
    }
    
private:
    void deriveAddress() {
        // Address = first 20 bytes of BLAKE2b(public_key)
        Hash pubkey_hash = Blake2b::hash(public_key_.data(), PUBLIC_KEY_SIZE);
        std::memcpy(address_.data(), pubkey_hash.data(), ADDRESS_SIZE);
    }
};

// ============================================================================
// Address Utilities
// ============================================================================

class AddressUtil {
public:
    // Convert address to Qx hex string with EIP-55 style checksum
    static std::string toHex(const Address& addr) {
        static const char hex_lower[] = "0123456789abcdef";
        static const char hex_upper[] = "0123456789ABCDEF";
        
        // First, create lowercase hex
        std::string lower;
        lower.reserve(ADDRESS_SIZE * 2);
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            lower += hex_lower[(addr[i] >> 4) & 0xF];
            lower += hex_lower[addr[i] & 0xF];
        }
        
        // Compute hash for checksum
        auto checksum_hash = SimpleHash::hash(reinterpret_cast<const uint8_t*>(lower.data()), lower.size());
        
        // Apply checksum: uppercase if hash nibble >= 8
        std::string result = "Qx";
        result.reserve(42);
        for (size_t i = 0; i < 40; i++) {
            uint8_t hash_byte = checksum_hash[i / 2];
            uint8_t nibble = (i % 2 == 0) ? (hash_byte >> 4) : (hash_byte & 0xF);
            
            if (nibble >= 8 && lower[i] >= 'a' && lower[i] <= 'f') {
                result += hex_upper[lower[i] - 'a' + 10];
            } else {
                result += lower[i];
            }
        }
        
        return result;
    }
    
    // Parse Qx hex string to address (accepts both Qx and legacy 0x)
    static bool fromHex(const std::string& hex, Address& addr) {
        if (hex.size() != ADDRESS_SIZE * 2 + 2) return false;
        
        // Accept both Qx and 0x prefix for backwards compatibility
        bool valid_prefix = (hex[0] == 'Q' && hex[1] == 'x') || 
                           (hex[0] == '0' && hex[1] == 'x');
        if (!valid_prefix) return false;
        
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
    
    // Short form: Qx7a3B9c...7890
    static std::string toShort(const Address& addr) {
        std::string full = toHex(addr);
        return full.substr(0, 8) + "..." + full.substr(38, 4);
    }
    
    // Check if address is zero (null address)
    static bool isZero(const Address& addr) {
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            if (addr[i] != 0) return false;
        }
        return true;
    }
};

// ============================================================================
// Transaction Structure
// ============================================================================

struct Transaction {
    Address from;           // Sender address
    Address to;             // Recipient address
    uint64_t value;         // Amount to transfer
    uint64_t gas_price;     // Gas price (wei per gas)
    uint64_t gas_limit;     // Max gas to consume
    uint64_t nonce;         // Transaction nonce (prevents replay)
    
    // Payload (for smart contracts)
    std::array<uint8_t, 256> data;
    size_t data_len;
    
    // Signature
    PublicKey public_key;
    Signature signature;
    
    Transaction() : value(0), gas_price(0), gas_limit(0), nonce(0), data_len(0) {
        from.fill(0);
        to.fill(0);
        data.fill(0);
        public_key.fill(0);
        signature.fill(0);
    }
    
    // Serialize for signing (everything except signature)
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
    
    // Sign transaction
    void sign(const KeyPair& keypair) {
        from = keypair.getAddress();
        public_key = keypair.getPublicKey();
        
        uint8_t buffer[1024];
        size_t len;
        serialize(buffer, len);
        
        signature = keypair.sign(buffer, len);
    }
    
    // Verify transaction signature
    bool verify() const {
        uint8_t buffer[1024];
        size_t len;
        serialize(buffer, len);
        
        return KeyPair::verify(public_key, buffer, len, signature);
    }
    
    // Get transaction hash
    std::array<uint8_t, 32> hash() const {
        uint8_t buffer[1024];
        size_t len;
        serialize(buffer, len);
        
        // Include signature in hash
        std::memcpy(buffer + len, signature.data(), SIGNATURE_SIZE);
        len += SIGNATURE_SIZE;
        
        return SimpleHash::hash(buffer, len);
    }
};

// ============================================================================
// Account State
// ============================================================================

struct Account {
    Address address;
    uint64_t balance;       // Account balance (in wei)
    uint64_t nonce;         // Transaction count (prevents replay attacks)
    uint64_t stake;         // Staked amount (for validators)
    
    Account() : balance(0), nonce(0), stake(0) {
        address.fill(0);
    }
    
    Account(const Address& addr) : address(addr), balance(0), nonce(0), stake(0) {}
};

} // namespace crypto
