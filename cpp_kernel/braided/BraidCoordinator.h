#pragma once

#include "Projection.h"
#include "BraidedKernel.h"

#include <iostream>

namespace braided {

/**
 * BraidCoordinator: Orchestrates cyclic projection exchange between tori.
 * 
 * The braid operates in three phases:
 * - A_PROJECTS: Torus A projects, B and C ingest
 * - B_PROJECTS: Torus B projects, A and C ingest
 * - C_PROJECTS: Torus C projects, A and B ingest
 * 
 * This creates a cyclic flow: A → B → C → A
 */
class BraidCoordinator {
public:
    enum Phase {
        A_PROJECTS,  // Torus A projects, B and C ingest
        B_PROJECTS,  // Torus B projects, A and C ingest
        C_PROJECTS   // Torus C projects, A and B ingest
    };
    
private:
    Phase current_phase_;
    uint64_t exchange_count_;
    
    // Statistics
    uint64_t total_exchanges_;
    uint64_t consistency_violations_;
    
public:
    BraidCoordinator() 
        : current_phase_(A_PROJECTS)
        , exchange_count_(0)
        , total_exchanges_(0)
        , consistency_violations_(0) 
    {}
    
    /**
     * Perform one braid exchange step.
     * Extracts projection from one torus and applies to the other two.
     */
    void exchange(BraidedKernel& torus_a, 
                  BraidedKernel& torus_b, 
                  BraidedKernel& torus_c);
    
    // Query current phase
    Phase getCurrentPhase() const { return current_phase_; }
    uint64_t getExchangeCount() const { return exchange_count_; }
    
    // Statistics
    uint64_t getTotalExchanges() const { return total_exchanges_; }
    uint64_t getConsistencyViolations() const { return consistency_violations_; }
    
    // Phase name for debugging
    const char* getPhaseName() const {
        switch (current_phase_) {
            case A_PROJECTS: return "A_PROJECTS";
            case B_PROJECTS: return "B_PROJECTS";
            case C_PROJECTS: return "C_PROJECTS";
            default: return "UNKNOWN";
        }
    }
};

inline void BraidCoordinator::exchange(
    BraidedKernel& torus_a,
    BraidedKernel& torus_b,
    BraidedKernel& torus_c)
{
    switch (current_phase_) {
        case A_PROJECTS: {
            // Torus A projects its state
            Projection proj = torus_a.extractProjection();
            
            // Verify projection integrity
            if (!proj.verify()) {
                std::cerr << "[BRAID] WARNING: Invalid projection from Torus A" << std::endl;
                consistency_violations_++;
            }
            
            // B and C ingest as constraint
            bool b_ok = torus_b.applyConstraint(proj);
            bool c_ok = torus_c.applyConstraint(proj);
            
            if (!b_ok || !c_ok) {
                consistency_violations_++;
            }
            
            // Rotate to next phase
            current_phase_ = B_PROJECTS;
            break;
        }
        
        case B_PROJECTS: {
            // Torus B projects its state
            Projection proj = torus_b.extractProjection();
            
            if (!proj.verify()) {
                std::cerr << "[BRAID] WARNING: Invalid projection from Torus B" << std::endl;
                consistency_violations_++;
            }
            
            bool a_ok = torus_a.applyConstraint(proj);
            bool c_ok = torus_c.applyConstraint(proj);
            
            if (!a_ok || !c_ok) {
                consistency_violations_++;
            }
            
            // Rotate to next phase
            current_phase_ = C_PROJECTS;
            break;
        }
        
        case C_PROJECTS: {
            // Torus C projects its state
            Projection proj = torus_c.extractProjection();
            
            if (!proj.verify()) {
                std::cerr << "[BRAID] WARNING: Invalid projection from Torus C" << std::endl;
                consistency_violations_++;
            }
            
            bool a_ok = torus_a.applyConstraint(proj);
            bool b_ok = torus_b.applyConstraint(proj);
            
            if (!a_ok || !b_ok) {
                consistency_violations_++;
            }
            
            // Complete one full cycle
            current_phase_ = A_PROJECTS;
            exchange_count_++;
            break;
        }
    }
    
    total_exchanges_++;
}

} // namespace braided
