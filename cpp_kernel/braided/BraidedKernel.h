#pragma once

#include "../single_torus/BettiRDLKernel.h"
#include "Projection.h"
#include <array>
#include <cmath>

namespace braided {

/**
 * BraidedKernel: Wrapper around BettiRDLKernel that adds braided system support.
 * 
 * This class composes BettiRDLKernel (has-a relationship) rather than inheriting from it,
 * allowing us to add braided functionality without modifying the original class.
 */
class BraidedKernel {
private:
    BettiRDLKernel kernel_;
    uint32_t torus_id_ = 0;
    uint64_t total_boundary_violations_ = 0;
    uint64_t total_corrective_events_ = 0;
    
public:
    BraidedKernel() = default;
    
    // Forward all BettiRDLKernel methods
    bool spawnProcess(int x, int y, int z) {
        return kernel_.spawnProcess(x, y, z);
    }
    
    // REAL: Spawn process with initial state for reconstruction
    bool spawnProcessWithState(int x, int y, int z, int64_t initial_state, int64_t initial_accum) {
        return kernel_.spawnProcessWithState(x, y, z, initial_state, initial_accum);
    }
    
    // REAL: Get total accumulated computation
    int64_t getTotalAccumulator() const {
        return kernel_.getTotalAccumulator();
    }
    
    // REAL: Get total events received by all processes
    uint64_t getTotalEventsReceived() const {
        return kernel_.getTotalEventsReceived();
    }
    
    bool createEdge(int x1, int y1, int z1, int x2, int y2, int z2, unsigned long long initial_delay) {
        return kernel_.createEdge(x1, y1, z1, x2, y2, z2, initial_delay);
    }
    
    bool injectEvent(int dst_x, int dst_y, int dst_z, int src_x, int src_y, int src_z, int payload) {
        return kernel_.injectEvent(dst_x, dst_y, dst_z, src_x, src_y, src_z, payload);
    }
    
    void tick() {
        kernel_.tick();
    }
    
    int run(int max_events) {
        return kernel_.run(max_events);
    }
    
    unsigned long long getCurrentTime() const {
        return kernel_.getCurrentTime();
    }
    
    unsigned long long getEventsProcessed() const {
        return kernel_.getEventsProcessed();
    }
    
    // Braided system support
    void setTorusId(uint32_t id) { torus_id_ = id; }
    uint32_t getTorusId() const { return torus_id_; }
    
    /**
     * Extract projection of current state.
     * This is a compact summary (O(1) size) for cross-torus communication.
     */
    Projection extractProjection() const {
        Projection proj;
        
        // 1. Identity
        proj.torus_id = torus_id_;
        proj.timestamp = kernel_.getCurrentTime();
        
        // 2. Summary statistics - REAL DATA
        proj.total_events_processed = kernel_.getEventsProcessed();
        proj.current_time = kernel_.getCurrentTime();
        proj.active_processes = static_cast<uint32_t>(kernel_.getActiveProcessCount());
        proj.pending_events = static_cast<uint32_t>(kernel_.getPendingEventCount());
        proj.edge_count = static_cast<uint32_t>(kernel_.getEdgeCount());
        
        // 3. Boundary state (x=0 face) - REAL DATA
        std::array<uint32_t, 1024> boundary;
        kernel_.getBoundaryState(0, boundary);
        for (size_t i = 0; i < Projection::BOUNDARY_SIZE; i++) {
            proj.boundary_states[i] = boundary[i];
        }
        
        // 4. Constraint vector (domain-specific)
        proj.constraint_vector = {};
        
        // Constraint[0]: Total event count (for conservation)
        proj.constraint_vector[0] = static_cast<int32_t>(kernel_.getEventsProcessed() % INT32_MAX);
        
        // Constraint[3]: Current time
        proj.constraint_vector[3] = static_cast<int32_t>(kernel_.getCurrentTime() % INT32_MAX);
        
        // 5. Compute hash for integrity
        proj.state_hash = proj.computeHash();
        
        return proj;
    }
    
    /**
     * Apply constraint from another torus.
     * Returns true if successful, false if consistency violation detected.
     */
    bool applyConstraint(const Projection& proj) {
        // 1. Verify projection integrity
        if (!proj.verify()) {
            std::cerr << "[Torus " << torus_id_ << "] Invalid projection from Torus " 
                      << proj.torus_id << std::endl;
            return false;
        }
        
        // 2. Check consistency
        bool consistent = verifyConsistency(proj);
        if (!consistent) {
            // For Phase 1, we just log inconsistencies
            // Phase 3 will add corrective events
            std::cerr << "[Torus " << torus_id_ << "] Consistency violation with Torus " 
                      << proj.torus_id << std::endl;
        }
        
        // 3. Apply boundary constraints - PHASE 2
        int violations = applyBoundaryConstraints(proj);
        total_boundary_violations_ += violations;
        
        // 4. Propagate constraint vector - PHASE 2
        propagateConstraints(proj);
        
        return consistent;
    }
    
    uint64_t getBoundaryViolations() const { return total_boundary_violations_; }
    uint64_t getCorrectiveEvents() const { return total_corrective_events_; }
    void reset() { kernel_.reset(); total_boundary_violations_ = 0; total_corrective_events_ = 0; }
    
public:
    /**
     * Verify consistency with projection from another torus.
     */
    bool verifyConsistency(const Projection& proj) {
        bool consistent = true;
        
        // REAL Check 1: Projection hash integrity
        if (!proj.verify()) {
            std::cerr << "[Torus " << torus_id_ << "] INTEGRITY FAILURE: Projection hash mismatch from Torus " 
                      << proj.torus_id << std::endl;
            return false;
        }
        
        // REAL Check 2: Time divergence with threshold
        int64_t time_diff = static_cast<int64_t>(kernel_.getCurrentTime()) - 
                           static_cast<int64_t>(proj.current_time);
        if (std::abs(time_diff) > 10000) {
            std::cerr << "[Torus " << torus_id_ << "] TIME DIVERGENCE: " << time_diff 
                      << " ticks from Torus " << proj.torus_id << std::endl;
            consistent = false;
        }
        
        // REAL Check 3: Boundary state correlation
        // Our x=31 face should correlate with neighbor's x=0 face
        std::array<uint32_t, 1024> our_boundary;
        kernel_.getBoundaryState(1, our_boundary);  // Our x=31 face
        
        int boundary_mismatches = 0;
        int64_t divergence_sum = 0;
        for (size_t i = 0; i < Projection::BOUNDARY_SIZE; i++) {
            if (proj.boundary_states[i] != 0 || our_boundary[i] != 0) {
                int64_t diff = static_cast<int64_t>(proj.boundary_states[i]) - 
                              static_cast<int64_t>(our_boundary[i]);
                divergence_sum += std::abs(diff);
                if (std::abs(diff) > 1000) {
                    boundary_mismatches++;
                }
            }
        }
        
        if (boundary_mismatches > 100) {  // More than 10% of boundary cells diverged
            std::cerr << "[Torus " << torus_id_ << "] BOUNDARY DIVERGENCE: " << boundary_mismatches 
                      << " cells, total divergence: " << divergence_sum << std::endl;
            consistent = false;
        }
        
        // REAL Check 4: Event rate sanity check
        if (proj.total_events_processed > 0) {
            uint64_t their_time = (proj.current_time > 0) ? proj.current_time : 1;
            uint64_t our_time = (kernel_.getCurrentTime() > 0) ? kernel_.getCurrentTime() : 1;
            double their_rate = static_cast<double>(proj.total_events_processed) / static_cast<double>(their_time);
            double our_rate = static_cast<double>(kernel_.getEventsProcessed()) / static_cast<double>(our_time);
            
            if (our_rate > 0 && their_rate > 0) {
                double rate_ratio = their_rate / our_rate;
                if (rate_ratio < 0.1 || rate_ratio > 10.0) {
                    std::cerr << "[Torus " << torus_id_ << "] EVENT RATE DIVERGENCE: " 
                              << rate_ratio << "x from Torus " << proj.torus_id << std::endl;
                    consistent = false;
                }
            }
        }
        
        return consistent;
    }
    
    int applyBoundaryConstraints(const Projection& proj) {
        int corrections = 0;
        std::array<uint32_t, 1024> our_boundary;
        kernel_.getBoundaryState(1, our_boundary);
        for (size_t i = 0; i < Projection::BOUNDARY_SIZE; i++) {
            if (proj.boundary_states[i] != 0 && our_boundary[i] == 0) {
                int y = static_cast<int>(i / 32);
                int z = static_cast<int>(i % 32);
                kernel_.injectCorrectiveEvent(31, y, z, static_cast<int32_t>(proj.boundary_states[i]));
                corrections++;
                total_corrective_events_++;
            }
        }
        return corrections;
    }
    
    void propagateConstraints(const Projection& proj) {
        int32_t their_events = proj.constraint_vector[0];
        int32_t our_events = static_cast<int32_t>(kernel_.getEventsProcessed() % INT32_MAX);
        if (std::abs(their_events - our_events) > 100000) {
            std::cerr << "[Torus " << torus_id_ << "] Event divergence with Torus " << proj.torus_id << std::endl;
        }
    }
};

} // namespace braided
