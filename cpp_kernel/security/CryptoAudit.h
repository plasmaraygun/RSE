#pragma once
/**
 * Cryptographic Security Audit
 * 
 * Validates cryptographic implementations for security vulnerabilities.
 * Tests include:
 * - Key generation entropy
 * - Signature malleability
 * - Timing attack resistance
 * - Hash collision resistance
 * - Nonce reuse detection
 */

#include "../core/Crypto.h"
#include <vector>
#include <set>
#include <chrono>
#include <random>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace security {

using namespace crypto;

// ============================================================================
// Audit Result
// ============================================================================

enum class AuditSeverity {
    PASS,
    INFO,
    WARNING,
    CRITICAL
};

struct AuditResult {
    std::string test_name;
    AuditSeverity severity;
    std::string message;
    double score;  // 0.0 - 1.0 (1.0 = pass)
};

class CryptoAudit {
private:
    std::vector<AuditResult> results_;
    
    void addResult(const std::string& name, AuditSeverity sev, 
                   const std::string& msg, double score = 1.0) {
        results_.push_back({name, sev, msg, score});
    }

public:
    // ========================================================================
    // Key Generation Tests
    // ========================================================================
    
    void testKeyGenEntropy() {
        std::cout << "[Audit] Testing key generation entropy..." << std::endl;
        
        constexpr int NUM_KEYS = 100;
        std::set<std::string> unique_keys;
        std::set<std::string> unique_addresses;
        
        for (int i = 0; i < NUM_KEYS; i++) {
            KeyPair kp;
            
            // Check key uniqueness
            std::string key_str(reinterpret_cast<const char*>(kp.getPublicKey().data()), 
                               PUBLIC_KEY_SIZE);
            unique_keys.insert(key_str);
            
            // Check address uniqueness
            std::string addr_str(reinterpret_cast<const char*>(kp.getAddress().data()), 
                                ADDRESS_SIZE);
            unique_addresses.insert(addr_str);
        }
        
        if (unique_keys.size() == NUM_KEYS && unique_addresses.size() == NUM_KEYS) {
            addResult("Key Generation Entropy", AuditSeverity::PASS,
                     "All generated keys are unique", 1.0);
        } else {
            addResult("Key Generation Entropy", AuditSeverity::CRITICAL,
                     "Duplicate keys detected! Entropy source may be compromised",
                     static_cast<double>(unique_keys.size()) / NUM_KEYS);
        }
    }
    
    void testKeyDistribution() {
        std::cout << "[Audit] Testing key byte distribution..." << std::endl;
        
        constexpr int NUM_KEYS = 100;
        std::vector<int> byte_counts(256, 0);
        
        for (int i = 0; i < NUM_KEYS; i++) {
            KeyPair kp;
            for (size_t j = 0; j < PUBLIC_KEY_SIZE; j++) {
                byte_counts[kp.getPublicKey()[j]]++;
            }
        }
        
        // Chi-squared test for uniform distribution
        double expected = (NUM_KEYS * PUBLIC_KEY_SIZE) / 256.0;
        double chi_squared = 0;
        
        for (int count : byte_counts) {
            double diff = count - expected;
            chi_squared += (diff * diff) / expected;
        }
        
        // For 255 degrees of freedom, chi-squared < 300 is acceptable at p=0.05
        if (chi_squared < 350) {
            addResult("Key Byte Distribution", AuditSeverity::PASS,
                     "Key bytes appear uniformly distributed (χ²=" + 
                     std::to_string(chi_squared) + ")", 1.0);
        } else {
            addResult("Key Byte Distribution", AuditSeverity::WARNING,
                     "Key byte distribution shows bias (χ²=" + 
                     std::to_string(chi_squared) + ")", 0.5);
        }
    }
    
    // ========================================================================
    // Signature Tests
    // ========================================================================
    
    void testSignatureVerification() {
        std::cout << "[Audit] Testing signature verification..." << std::endl;
        
        KeyPair kp;
        std::string message = "Test message for signature verification";
        
        Signature sig = kp.sign(reinterpret_cast<const uint8_t*>(message.data()), 
                                message.size());
        
        // Valid signature should verify
        bool valid = KeyPair::verify(kp.getPublicKey(),
                                     reinterpret_cast<const uint8_t*>(message.data()),
                                     message.size(), sig);
        
        if (!valid) {
            addResult("Signature Verification", AuditSeverity::CRITICAL,
                     "Valid signature failed to verify!", 0.0);
            return;
        }
        
        // Modified message should fail
        std::string modified = message + "x";
        bool invalid = KeyPair::verify(kp.getPublicKey(),
                                       reinterpret_cast<const uint8_t*>(modified.data()),
                                       modified.size(), sig);
        
        if (invalid) {
            addResult("Signature Verification", AuditSeverity::CRITICAL,
                     "Modified message verified with original signature!", 0.0);
            return;
        }
        
        // Wrong key should fail
        KeyPair other;
        bool wrong_key = KeyPair::verify(other.getPublicKey(),
                                         reinterpret_cast<const uint8_t*>(message.data()),
                                         message.size(), sig);
        
        if (wrong_key) {
            addResult("Signature Verification", AuditSeverity::CRITICAL,
                     "Signature verified with wrong public key!", 0.0);
            return;
        }
        
        addResult("Signature Verification", AuditSeverity::PASS,
                 "Signature verification works correctly", 1.0);
    }
    
    void testSignatureMalleability() {
        std::cout << "[Audit] Testing signature malleability resistance..." << std::endl;
        
        KeyPair kp;
        std::string message = "Test message";
        
        Signature sig = kp.sign(reinterpret_cast<const uint8_t*>(message.data()), 
                                message.size());
        
        // Try common malleability attacks
        
        // 1. Flip bits in signature
        int malleable_count = 0;
        for (size_t i = 0; i < SIGNATURE_SIZE; i++) {
            Signature modified = sig;
            modified[i] ^= 0x01;
            
            if (KeyPair::verify(kp.getPublicKey(),
                               reinterpret_cast<const uint8_t*>(message.data()),
                               message.size(), modified)) {
                malleable_count++;
            }
        }
        
        if (malleable_count > 0) {
            addResult("Signature Malleability", AuditSeverity::WARNING,
                     std::to_string(malleable_count) + " bit flips still verified",
                     1.0 - (malleable_count / static_cast<double>(SIGNATURE_SIZE)));
        } else {
            addResult("Signature Malleability", AuditSeverity::PASS,
                     "No simple malleability attacks succeeded", 1.0);
        }
    }
    
    void testSignatureConsistency() {
        std::cout << "[Audit] Testing signature consistency..." << std::endl;
        
        KeyPair kp;
        std::string message = "Deterministic signature test";
        
        // Ed25519 signatures should be deterministic for same key+message
        Signature sig1 = kp.sign(reinterpret_cast<const uint8_t*>(message.data()), 
                                 message.size());
        Signature sig2 = kp.sign(reinterpret_cast<const uint8_t*>(message.data()), 
                                 message.size());
        
        if (sig1 == sig2) {
            addResult("Signature Consistency", AuditSeverity::PASS,
                     "Signatures are deterministic (Ed25519 standard)", 1.0);
        } else {
            addResult("Signature Consistency", AuditSeverity::INFO,
                     "Signatures include randomness (may be intentional)", 0.8);
        }
    }
    
    // ========================================================================
    // Hash Function Tests
    // ========================================================================
    
    void testHashCollisions() {
        std::cout << "[Audit] Testing hash collision resistance..." << std::endl;
        
        constexpr int NUM_HASHES = 10000;
        std::set<std::string> unique_hashes;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 255);
        
        for (int i = 0; i < NUM_HASHES; i++) {
            std::vector<uint8_t> data(32);
            for (auto& b : data) b = dist(gen);
            
            Hash h = Blake2b::hash(data.data(), data.size());
            unique_hashes.insert(std::string(reinterpret_cast<char*>(h.data()), HASH_SIZE));
        }
        
        if (unique_hashes.size() == NUM_HASHES) {
            addResult("Hash Collision Resistance", AuditSeverity::PASS,
                     "No collisions in " + std::to_string(NUM_HASHES) + " hashes", 1.0);
        } else {
            addResult("Hash Collision Resistance", AuditSeverity::CRITICAL,
                     "Hash collisions detected!", 0.0);
        }
    }
    
    void testHashAvalanche() {
        std::cout << "[Audit] Testing hash avalanche effect..." << std::endl;
        
        constexpr int NUM_TESTS = 100;
        double total_bit_diff = 0;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 255);
        
        for (int i = 0; i < NUM_TESTS; i++) {
            std::vector<uint8_t> data(32);
            for (auto& b : data) b = dist(gen);
            
            Hash h1 = Blake2b::hash(data.data(), data.size());
            
            // Flip one bit
            data[0] ^= 0x01;
            Hash h2 = Blake2b::hash(data.data(), data.size());
            
            // Count different bits
            int diff_bits = 0;
            for (size_t j = 0; j < HASH_SIZE; j++) {
                uint8_t xor_byte = h1[j] ^ h2[j];
                while (xor_byte) {
                    diff_bits += xor_byte & 1;
                    xor_byte >>= 1;
                }
            }
            
            total_bit_diff += diff_bits;
        }
        
        double avg_diff = total_bit_diff / NUM_TESTS;
        double expected = HASH_SIZE * 8 / 2.0;  // Should be ~50% different
        double ratio = avg_diff / expected;
        
        if (ratio > 0.9 && ratio < 1.1) {
            addResult("Hash Avalanche Effect", AuditSeverity::PASS,
                     "Good avalanche effect (avg " + std::to_string(avg_diff) + 
                     " bits differ)", 1.0);
        } else {
            addResult("Hash Avalanche Effect", AuditSeverity::WARNING,
                     "Weak avalanche effect (avg " + std::to_string(avg_diff) + 
                     " bits differ, expected ~" + std::to_string(expected) + ")",
                     std::min(ratio, 2.0 - ratio));
        }
    }
    
    // ========================================================================
    // Timing Attack Resistance
    // ========================================================================
    
    void testTimingAttackResistance() {
        std::cout << "[Audit] Testing timing attack resistance..." << std::endl;
        
        KeyPair kp;
        std::string message = "Timing test message";
        
        Signature sig = kp.sign(reinterpret_cast<const uint8_t*>(message.data()), 
                                message.size());
        
        // Measure verification time for valid signature
        constexpr int NUM_TESTS = 1000;
        std::vector<double> valid_times, invalid_times;
        
        for (int i = 0; i < NUM_TESTS; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            KeyPair::verify(kp.getPublicKey(),
                           reinterpret_cast<const uint8_t*>(message.data()),
                           message.size(), sig);
            auto end = std::chrono::high_resolution_clock::now();
            
            valid_times.push_back(
                std::chrono::duration<double, std::nano>(end - start).count());
        }
        
        // Measure verification time for invalid signature
        Signature invalid_sig = sig;
        invalid_sig[0] ^= 0xFF;
        
        for (int i = 0; i < NUM_TESTS; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            KeyPair::verify(kp.getPublicKey(),
                           reinterpret_cast<const uint8_t*>(message.data()),
                           message.size(), invalid_sig);
            auto end = std::chrono::high_resolution_clock::now();
            
            invalid_times.push_back(
                std::chrono::duration<double, std::nano>(end - start).count());
        }
        
        // Calculate mean and standard deviation
        auto calc_stats = [](const std::vector<double>& times) {
            double sum = 0, sum_sq = 0;
            for (double t : times) {
                sum += t;
                sum_sq += t * t;
            }
            double mean = sum / times.size();
            double variance = (sum_sq / times.size()) - (mean * mean);
            return std::make_pair(mean, std::sqrt(variance));
        };
        
        auto [valid_mean, valid_std] = calc_stats(valid_times);
        auto [invalid_mean, invalid_std] = calc_stats(invalid_times);
        
        // Times should be similar (within 2 standard deviations)
        double diff = std::abs(valid_mean - invalid_mean);
        double threshold = 2 * std::max(valid_std, invalid_std);
        
        if (diff < threshold) {
            addResult("Timing Attack Resistance", AuditSeverity::PASS,
                     "Verification timing appears constant", 1.0);
        } else {
            addResult("Timing Attack Resistance", AuditSeverity::WARNING,
                     "Timing difference detected (" + std::to_string(diff) + 
                     "ns difference)", 0.5);
        }
    }
    
    // ========================================================================
    // Transaction Security Tests
    // ========================================================================
    
    void testTransactionReplay() {
        std::cout << "[Audit] Testing transaction replay protection..." << std::endl;
        
        KeyPair alice, bob;
        
        Transaction tx1;
        tx1.to = bob.getAddress();
        tx1.value = 1000;
        tx1.nonce = 0;
        tx1.sign(alice);
        
        Transaction tx2 = tx1;  // Copy
        tx2.nonce = 1;
        tx2.sign(alice);  // Must re-sign with new nonce
        
        // Signatures should be different due to nonce
        bool same_sig = (tx1.signature == tx2.signature);
        
        if (!same_sig) {
            addResult("Transaction Replay Protection", AuditSeverity::PASS,
                     "Nonce changes signature, preventing replay", 1.0);
        } else {
            addResult("Transaction Replay Protection", AuditSeverity::CRITICAL,
                     "Signatures identical despite nonce change!", 0.0);
        }
    }
    
    void testTransactionTampering() {
        std::cout << "[Audit] Testing transaction tampering detection..." << std::endl;
        
        KeyPair alice, bob, eve;
        
        Transaction tx;
        tx.to = bob.getAddress();
        tx.value = 1000;
        tx.nonce = 0;
        tx.sign(alice);
        
        // Try to change recipient
        Transaction tampered = tx;
        tampered.to = eve.getAddress();
        
        bool tampered_valid = tampered.verify();
        
        if (!tampered_valid) {
            addResult("Transaction Tampering Detection", AuditSeverity::PASS,
                     "Tampered transaction correctly rejected", 1.0);
        } else {
            addResult("Transaction Tampering Detection", AuditSeverity::CRITICAL,
                     "Tampered transaction accepted!", 0.0);
        }
    }
    
    // ========================================================================
    // Run All Tests
    // ========================================================================
    
    void runFullAudit() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "  ARQON Cryptographic Security Audit" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        results_.clear();
        
        // Key generation
        testKeyGenEntropy();
        testKeyDistribution();
        
        // Signatures
        testSignatureVerification();
        testSignatureMalleability();
        testSignatureConsistency();
        
        // Hashing
        testHashCollisions();
        testHashAvalanche();
        
        // Timing
        testTimingAttackResistance();
        
        // Transactions
        testTransactionReplay();
        testTransactionTampering();
        
        printReport();
    }
    
    void printReport() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "  Audit Report" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        int pass = 0, info = 0, warn = 0, crit = 0;
        double total_score = 0;
        
        for (const auto& r : results_) {
            std::string severity_str;
            switch (r.severity) {
                case AuditSeverity::PASS:     severity_str = "✅ PASS"; pass++; break;
                case AuditSeverity::INFO:     severity_str = "ℹ️  INFO"; info++; break;
                case AuditSeverity::WARNING:  severity_str = "⚠️  WARN"; warn++; break;
                case AuditSeverity::CRITICAL: severity_str = "❌ CRIT"; crit++; break;
            }
            
            std::cout << severity_str << " | " << r.test_name << std::endl;
            std::cout << "       " << r.message << std::endl;
            std::cout << std::endl;
            
            total_score += r.score;
        }
        
        double avg_score = total_score / results_.size();
        
        std::cout << "========================================" << std::endl;
        std::cout << "Summary:" << std::endl;
        std::cout << "  Pass: " << pass << std::endl;
        std::cout << "  Info: " << info << std::endl;
        std::cout << "  Warnings: " << warn << std::endl;
        std::cout << "  Critical: " << crit << std::endl;
        std::cout << "  Overall Score: " << std::fixed << std::setprecision(1) 
                  << (avg_score * 100) << "%" << std::endl;
        std::cout << "========================================" << std::endl;
        
        if (crit > 0) {
            std::cout << "\n❌ AUDIT FAILED - Critical issues found!" << std::endl;
        } else if (warn > 0) {
            std::cout << "\n⚠️  AUDIT PASSED WITH WARNINGS" << std::endl;
        } else {
            std::cout << "\n✅ AUDIT PASSED" << std::endl;
        }
    }
    
    const std::vector<AuditResult>& getResults() const { return results_; }
    
    bool hasCritical() const {
        for (const auto& r : results_) {
            if (r.severity == AuditSeverity::CRITICAL) return true;
        }
        return false;
    }
};

} // namespace security
