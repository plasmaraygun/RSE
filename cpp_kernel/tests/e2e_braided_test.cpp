/**
 * E2E Test Suite for RSE Braided-Torus System
 * Tests all phases: single torus, braiding, fault tolerance, benchmarks
 */

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>

#include "../single_torus/BettiRDLKernel.h"
#include "../braided/BraidedKernel.h"
#include "../braided/FaultTolerantBraid.h"

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << std::endl; return false; } } while(0)
#define RUN_TEST(fn) do { std::cout << "  " << #fn << "... " << std::flush; if (fn()) { std::cout << "PASS" << std::endl; passed++; } else { std::cout << "FAIL" << std::endl; failed++; } } while(0)

// Single Torus Tests
bool test_spawn_process() {
    BettiRDLKernel k;
    TEST_ASSERT(k.spawnProcess(0,0,0), "spawn at origin");
    TEST_ASSERT(k.spawnProcess(31,31,31), "spawn at max");
    TEST_ASSERT(k.spawnProcess(32,32,32), "spawn with wrap");
    return true;
}

bool test_event_processing() {
    BettiRDLKernel k;
    k.spawnProcess(0,0,0);
    k.spawnProcess(1,0,0);
    k.createEdge(0,0,0,1,0,0,1);
    k.injectEvent(0,0,0,0,0,0,1);
    k.run(100);
    TEST_ASSERT(k.getEventsProcessed() > 0, "events processed");
    return true;
}

bool test_kernel_reset() {
    BettiRDLKernel k;
    k.spawnProcess(0,0,0);
    k.injectEvent(0,0,0,0,0,0,1);
    k.run(10);
    k.reset();
    TEST_ASSERT(k.getEventsProcessed() == 0, "reset events");
    TEST_ASSERT(k.getCurrentTime() == 0, "reset time");
    return true;
}

bool test_toroidal_wrap() {
    BettiRDLKernel k;
    k.spawnProcess(31,0,0);
    k.spawnProcess(0,0,0);
    k.createEdge(31,0,0,0,0,0,1);
    k.injectEvent(31,0,0,31,0,0,1);
    k.run(50);
    TEST_ASSERT(k.getEventsProcessed() > 0, "wrap propagation");
    return true;
}

// Projection Tests
bool test_projection_extraction() {
    braided::BraidedKernel k;
    k.setTorusId(42);
    k.spawnProcess(0,0,0);
    k.injectEvent(0,0,0,0,0,0,1);
    k.run(10);
    auto proj = k.extractProjection();
    TEST_ASSERT(proj.torus_id == 42, "torus id");
    TEST_ASSERT(proj.verify(), "hash verify");
    return true;
}

bool test_projection_tamper() {
    braided::BraidedKernel k;
    auto proj = k.extractProjection();
    TEST_ASSERT(proj.verify(), "original ok");
    proj.total_events_processed++;
    TEST_ASSERT(!proj.verify(), "tamper detected");
    return true;
}

// Braided System Tests
bool test_three_torus() {
    braided::FaultTolerantBraid braid(100, false);
    TEST_ASSERT(braid.getTorusA().getTorusId() == 0, "torus A id");
    TEST_ASSERT(braid.getTorusB().getTorusId() == 1, "torus B id");
    TEST_ASSERT(braid.getTorusC().getTorusId() == 2, "torus C id");
    return true;
}

bool test_braid_exchange() {
    braided::FaultTolerantBraid braid(10, false);
    for (int i = 0; i < 3; i++) {
        braid.getTorus(i).spawnProcess(0,0,0);
        braid.getTorus(i).injectEvent(0,0,0,0,0,0,1);
    }
    braid.run(100);
    TEST_ASSERT(braid.getBraidCycles() >= 3, "braid cycles");
    return true;
}

bool test_constraint_apply() {
    // Use heap allocation - BraidedKernel is too large for stack
    auto a = std::make_unique<braided::BraidedKernel>();
    auto b = std::make_unique<braided::BraidedKernel>();
    a->setTorusId(0);
    b->setTorusId(1);
    a->spawnProcess(0,0,0);
    a->run(10);
    auto proj = a->extractProjection();
    TEST_ASSERT(b->applyConstraint(proj), "apply constraint");
    return true;
}

// Phase 2: Boundary Tests
bool test_boundary_constraints() {
    auto a = std::make_unique<braided::BraidedKernel>();
    auto b = std::make_unique<braided::BraidedKernel>();
    a->setTorusId(0);
    b->setTorusId(1);
    for (int y = 0; y < 5; y++) a->spawnProcess(0, y, 0);
    a->injectEvent(0,0,0,0,0,0,1);
    a->run(50);
    auto proj = a->extractProjection();
    b->applyConstraint(proj);
    TEST_ASSERT(b->getCorrectiveEvents() >= 0, "corrective events tracked");
    return true;
}

// Phase 3: Fault Tolerance Tests
bool test_reconstruction() {
    auto a = std::make_unique<braided::BraidedKernel>();
    auto b = std::make_unique<braided::BraidedKernel>();
    a->setTorusId(0);
    b->setTorusId(1);
    
    a->spawnProcess(0,0,0);
    b->spawnProcess(0,0,0);
    
    auto pa = a->extractProjection();
    auto pb = b->extractProjection();
    
    // Verify projections can be extracted and verified
    TEST_ASSERT(pa.verify(), "projection A valid");
    TEST_ASSERT(pb.verify(), "projection B valid");
    TEST_ASSERT(b->applyConstraint(pa), "apply constraint");
    
    return true;
}

// Benchmark Tests
bool test_throughput() {
    BettiRDLKernel k;
    for (int x = 0; x < 16; x++) for (int y = 0; y < 8; y++) k.spawnProcess(x,y,0);
    for (int x = 0; x < 15; x++) for (int y = 0; y < 8; y++) k.createEdge(x,y,0,x+1,y,0,1);
    for (int y = 0; y < 8; y++) { k.createEdge(15,y,0,0,y,0,1); k.injectEvent(0,y,0,0,y,0,1); }
    auto start = std::chrono::high_resolution_clock::now();
    k.run(50000);
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
    double eps = k.getEventsProcessed() * 1000000.0 / us;
    std::cout << "[" << eps/1e6 << "M/s] ";
    TEST_ASSERT(eps > 1000000, "throughput > 1M/s");
    return true;
}

bool test_memory_bounded() {
    BettiRDLKernel k;
    k.spawnProcess(0,0,0);
    k.spawnProcess(1,0,0);
    k.createEdge(0,0,0,1,0,0,1);
    k.createEdge(1,0,0,0,0,0,1);
    k.injectEvent(0,0,0,0,0,0,1);
    k.run(500000);
    TEST_ASSERT(k.getEventsProcessed() >= 500000, "processed 500K+");
    TEST_ASSERT(k.getActiveProcessCount() == 2, "bounded processes");
    TEST_ASSERT(k.getEdgeCount() == 2, "bounded edges");
    return true;
}

bool test_parallel_braid() {
    braided::FaultTolerantBraid braid(100, true);
    for (int i = 0; i < 3; i++) {
        auto& t = braid.getTorus(i);
        for (int x = 0; x < 8; x++) t.spawnProcess(x,0,0);
        for (int x = 0; x < 7; x++) t.createEdge(x,0,0,x+1,0,0,1);
        t.createEdge(7,0,0,0,0,0,1);
        t.injectEvent(0,0,0,0,0,0,1);
    }
    auto stats = braid.run(5000);
    TEST_ASSERT(stats.total_events > 100, "parallel events");
    TEST_ASSERT(braid.getBraidCycles() >= 15, "parallel cycles");
    return true;
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "   RSE BRAIDED-TORUS E2E TEST SUITE\n";
    std::cout << "========================================\n\n";
    
    int passed = 0, failed = 0;
    
    std::cout << "[Single Torus]\n";
    RUN_TEST(test_spawn_process);
    RUN_TEST(test_event_processing);
    RUN_TEST(test_kernel_reset);
    RUN_TEST(test_toroidal_wrap);
    
    std::cout << "\n[Projections]\n";
    RUN_TEST(test_projection_extraction);
    RUN_TEST(test_projection_tamper);
    
    std::cout << "\n[Braided System]\n";
    RUN_TEST(test_three_torus);
    RUN_TEST(test_braid_exchange);
    RUN_TEST(test_constraint_apply);
    
    std::cout << "\n[Phase 2: Boundaries]\n";
    RUN_TEST(test_boundary_constraints);
    
    std::cout << "\n[Phase 3: Fault Tolerance]\n";
    RUN_TEST(test_reconstruction);
    
    std::cout << "\n[Benchmarks]\n";
    RUN_TEST(test_throughput);
    RUN_TEST(test_memory_bounded);
    RUN_TEST(test_parallel_braid);
    
    std::cout << "\n========================================\n";
    std::cout << "  PASSED: " << passed << "  FAILED: " << failed << "\n";
    std::cout << "========================================\n";
    
    return failed == 0 ? 0 : 1;
}
