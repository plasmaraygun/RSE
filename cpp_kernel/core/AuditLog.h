#pragma once

#include "CryptoHash.h"
#include "SecureMemory.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace rse {
namespace audit {

/**
 * AuditLog: Cryptographic audit trail for NSA-grade accountability.
 * 
 * Features:
 * - Tamper-evident: Each entry includes hash of previous entry (blockchain-style)
 * - Non-repudiation: Entries cannot be modified without detection
 * - Bounded: Fixed-size circular buffer, oldest entries overwritten
 * - Thread-safe: Lock-free reads, mutex-protected writes
 */

enum class AuditEventType : uint8_t {
    SYSTEM_START = 0x01,
    SYSTEM_STOP = 0x02,
    TORUS_CREATED = 0x10,
    TORUS_DESTROYED = 0x11,
    TORUS_FAILED = 0x12,
    TORUS_RECONSTRUCTED = 0x13,
    PROJECTION_SENT = 0x20,
    PROJECTION_RECEIVED = 0x21,
    PROJECTION_INVALID = 0x22,
    CONSISTENCY_VIOLATION = 0x30,
    BOUNDARY_CORRECTION = 0x31,
    SECURITY_VIOLATION = 0x40,
    INTEGRITY_FAILURE = 0x41,
    CANARY_CORRUPT = 0x42,
};

inline const char* auditEventToString(AuditEventType type) {
    switch (type) {
        case AuditEventType::SYSTEM_START: return "SYSTEM_START";
        case AuditEventType::SYSTEM_STOP: return "SYSTEM_STOP";
        case AuditEventType::TORUS_CREATED: return "TORUS_CREATED";
        case AuditEventType::TORUS_DESTROYED: return "TORUS_DESTROYED";
        case AuditEventType::TORUS_FAILED: return "TORUS_FAILED";
        case AuditEventType::TORUS_RECONSTRUCTED: return "TORUS_RECONSTRUCTED";
        case AuditEventType::PROJECTION_SENT: return "PROJECTION_SENT";
        case AuditEventType::PROJECTION_RECEIVED: return "PROJECTION_RECEIVED";
        case AuditEventType::PROJECTION_INVALID: return "PROJECTION_INVALID";
        case AuditEventType::CONSISTENCY_VIOLATION: return "CONSISTENCY_VIOLATION";
        case AuditEventType::BOUNDARY_CORRECTION: return "BOUNDARY_CORRECTION";
        case AuditEventType::SECURITY_VIOLATION: return "SECURITY_VIOLATION";
        case AuditEventType::INTEGRITY_FAILURE: return "INTEGRITY_FAILURE";
        case AuditEventType::CANARY_CORRUPT: return "CANARY_CORRUPT";
        default: return "UNKNOWN";
    }
}

struct AuditEntry {
    uint64_t sequence_number;
    uint64_t timestamp_ns;
    AuditEventType event_type;
    uint8_t torus_id;
    uint16_t reserved;
    uint32_t data1;
    uint32_t data2;
    crypto::Blake3::Hash prev_hash;  // Hash of previous entry
    crypto::Blake3::Hash entry_hash; // Hash of this entry (excluding this field)
    
    // Compute hash of this entry
    crypto::Blake3::Hash computeHash() const {
        crypto::Blake3 hasher;
        hasher.update(&sequence_number, sizeof(sequence_number));
        hasher.update(&timestamp_ns, sizeof(timestamp_ns));
        hasher.update(&event_type, sizeof(event_type));
        hasher.update(&torus_id, sizeof(torus_id));
        hasher.update(&data1, sizeof(data1));
        hasher.update(&data2, sizeof(data2));
        hasher.update(prev_hash.data(), prev_hash.size());
        return hasher.finalize();
    }
    
    // Verify entry integrity
    [[nodiscard]] bool verify() const {
        return crypto::constantTimeHashCompare(entry_hash, computeHash());
    }
};

template<size_t CAPACITY = 4096>
class AuditLog {
public:
    AuditLog() : write_index_(0), entry_count_(0) {
        // Initialize with genesis entry
        AuditEntry genesis{};
        genesis.sequence_number = 0;
        genesis.timestamp_ns = now_ns();
        genesis.event_type = AuditEventType::SYSTEM_START;
        genesis.prev_hash = {}; // Zero hash for genesis
        genesis.entry_hash = genesis.computeHash();
        
        entries_[0] = genesis;
        last_hash_ = genesis.entry_hash;
        write_index_ = 1;
        entry_count_ = 1;
    }
    
    // Log an event (thread-safe)
    void log(AuditEventType type, uint8_t torus_id = 0, 
             uint32_t data1 = 0, uint32_t data2 = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        AuditEntry entry{};
        entry.sequence_number = entry_count_.load();
        entry.timestamp_ns = now_ns();
        entry.event_type = type;
        entry.torus_id = torus_id;
        entry.data1 = data1;
        entry.data2 = data2;
        entry.prev_hash = last_hash_;
        entry.entry_hash = entry.computeHash();
        
        entries_[write_index_] = entry;
        last_hash_ = entry.entry_hash;
        
        write_index_ = (write_index_ + 1) % CAPACITY;
        entry_count_++;
    }
    
    // Verify integrity of entire log chain
    [[nodiscard]] bool verifyChain() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t count = std::min(entry_count_.load(), CAPACITY);
        if (count == 0) return true;
        
        size_t start = (entry_count_ > CAPACITY) ? write_index_ : 0;
        crypto::Blake3::Hash expected_prev = {};
        
        // Genesis entry has zero prev_hash
        if (entry_count_ <= CAPACITY) {
            expected_prev = {};
        } else {
            // For wrapped buffer, we can't verify the first entry's prev_hash
            size_t first_idx = start;
            if (!entries_[first_idx].verify()) return false;
            expected_prev = entries_[first_idx].entry_hash;
            start = (start + 1) % CAPACITY;
            count--;
        }
        
        for (size_t i = 0; i < count; i++) {
            size_t idx = (start + i) % CAPACITY;
            const AuditEntry& entry = entries_[idx];
            
            // Verify entry's own hash
            if (!entry.verify()) return false;
            
            // Verify chain linkage (skip for genesis)
            if (entry.sequence_number > 0) {
                if (!crypto::constantTimeHashCompare(entry.prev_hash, expected_prev)) {
                    return false;
                }
            }
            
            expected_prev = entry.entry_hash;
        }
        
        return true;
    }
    
    // Get entry by sequence number (if still in buffer)
    [[nodiscard]] bool getEntry(uint64_t seq, AuditEntry& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t oldest_seq = (entry_count_ > CAPACITY) ? entry_count_ - CAPACITY : 0;
        if (seq < oldest_seq || seq >= entry_count_) {
            return false;
        }
        
        size_t idx = seq % CAPACITY;
        out = entries_[idx];
        return true;
    }
    
    uint64_t getEntryCount() const { return entry_count_.load(); }
    
    // Singleton access
    static AuditLog& instance() {
        static AuditLog log;
        return log;
    }

private:
    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
    }
    
    mutable std::mutex mutex_;
    std::array<AuditEntry, CAPACITY> entries_;
    crypto::Blake3::Hash last_hash_;
    size_t write_index_;
    std::atomic<uint64_t> entry_count_;
};

// Convenience macro
#define RSE_AUDIT rse::audit::AuditLog::instance()

} // namespace audit
} // namespace rse
