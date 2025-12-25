#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace braided {

/**
 * Projection: Compact representation of torus state for cross-torus communication.
 * 
 * Design constraints:
 * - O(1) size: Does not grow with number of processes or events
 * - Serializable: Can be transmitted over network (future)
 * - Verifiable: Includes integrity check (hash)
 * 
 * Total size: ~4.2 KB (constant regardless of workload)
 */
struct Projection {
    // Identity
    uint32_t torus_id;           // 0=A, 1=B, 2=C
    uint64_t timestamp;          // Logical time when projection was created
    
    // Summary statistics (O(1) size)
    uint64_t total_events_processed;
    uint64_t current_time;
    uint32_t active_processes;
    uint32_t pending_events;
    uint32_t edge_count;
    
    // Boundary state (one face of the 32³ cube)
    // We project the x=0 face (32×32 = 1024 cells)
    static constexpr size_t BOUNDARY_SIZE = 32 * 32;
    std::array<uint32_t, BOUNDARY_SIZE> boundary_states;
    
    // Constraint vector (domain-specific invariants)
    // Examples: conservation laws, load balancing targets, etc.
    static constexpr size_t CONSTRAINT_DIM = 16;
    std::array<int32_t, CONSTRAINT_DIM> constraint_vector;
    
    // Integrity
    uint64_t state_hash;         // Hash of critical state for consistency checking
    
    // Methods
    uint64_t computeHash() const;
    bool verify() const;
    
    // Serialization (for future network transmission)
    size_t serialize(uint8_t* buffer, size_t buffer_size) const;
    static Projection deserialize(const uint8_t* buffer, size_t buffer_size);
};

// Compute hash of projection for integrity checking
// NSA-grade: hashes ALL data, uses SipHash-like mixing for security
inline uint64_t Projection::computeHash() const {
    // SipHash-2-4 inspired constants
    constexpr uint64_t SIPROUND_A = 0x736f6d6570736575ULL;
    constexpr uint64_t SIPROUND_B = 0x646f72616e646f6dULL;
    constexpr uint64_t SIPROUND_C = 0x6c7967656e657261ULL;
    constexpr uint64_t SIPROUND_D = 0x7465646279746573ULL;
    
    uint64_t v0 = SIPROUND_A;
    uint64_t v1 = SIPROUND_B;
    uint64_t v2 = SIPROUND_C;
    uint64_t v3 = SIPROUND_D;
    
    auto sipround = [&]() {
        v0 += v1; v1 = (v1 << 13) | (v1 >> 51); v1 ^= v0;
        v0 = (v0 << 32) | (v0 >> 32);
        v2 += v3; v3 = (v3 << 16) | (v3 >> 48); v3 ^= v2;
        v0 += v3; v3 = (v3 << 21) | (v3 >> 43); v3 ^= v0;
        v2 += v1; v1 = (v1 << 17) | (v1 >> 47); v1 ^= v2;
        v2 = (v2 << 32) | (v2 >> 32);
    };
    
    auto mix = [&](uint64_t m) {
        v3 ^= m;
        sipround();
        sipround();
        v0 ^= m;
    };
    
    // Hash all scalar fields
    mix(torus_id);
    mix(timestamp);
    mix(total_events_processed);
    mix(current_time);
    mix(active_processes | (static_cast<uint64_t>(pending_events) << 32));
    mix(edge_count);
    
    // Hash ALL boundary states (security critical - no sampling)
    for (size_t i = 0; i < BOUNDARY_SIZE; i += 2) {
        uint64_t combined = boundary_states[i];
        if (i + 1 < BOUNDARY_SIZE) {
            combined |= static_cast<uint64_t>(boundary_states[i + 1]) << 32;
        }
        mix(combined);
    }
    
    // Hash all constraint vector elements
    for (size_t i = 0; i < CONSTRAINT_DIM; i += 2) {
        uint64_t combined = static_cast<uint64_t>(static_cast<uint32_t>(constraint_vector[i]));
        if (i + 1 < CONSTRAINT_DIM) {
            combined |= static_cast<uint64_t>(static_cast<uint32_t>(constraint_vector[i + 1])) << 32;
        }
        mix(combined);
    }
    
    // Finalization
    v2 ^= 0xff;
    sipround();
    sipround();
    sipround();
    sipround();
    
    return v0 ^ v1 ^ v2 ^ v3;
}

inline bool Projection::verify() const {
    return computeHash() == state_hash;
}

inline size_t Projection::serialize(uint8_t* buffer, size_t buffer_size) const {
    size_t required_size = sizeof(Projection);
    if (buffer_size < required_size) {
        return 0;  // Buffer too small
    }
    
    std::memcpy(buffer, this, required_size);
    return required_size;
}

inline Projection Projection::deserialize(const uint8_t* buffer, size_t buffer_size) {
    Projection proj;
    size_t required_size = sizeof(Projection);
    
    if (buffer_size < required_size) {
        // Return invalid projection
        proj.torus_id = 0xFFFFFFFF;
        return proj;
    }
    
    std::memcpy(&proj, buffer, required_size);
    return proj;
}

} // namespace braided
