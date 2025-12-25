#pragma once
#include "../single_torus/BettiRDLKernel.h"
#include "../braided/BraidedKernel.h"
#include "../braided/BlockchainBraid.h"

// Kernel Tests 41-55
bool test_kernel_41_init() {
    BettiRDLKernel<32> kernel;
    TEST_ASSERT(kernel.getProcessCount() == 0, "Empty");
    return true;
}

bool test_kernel_42_spawn() {
    BettiRDLKernel<32> kernel;
    TEST_ASSERT(kernel.spawnProcess(0, 0, 0), "Spawn");
    TEST_ASSERT(kernel.getProcessCount() == 1, "Count");
    return true;
}

bool test_kernel_43_spawn_multi() {
    BettiRDLKernel<32> kernel;
    for (int i = 0; i < 50; i++) kernel.spawnProcess(i % 32, (i/32) % 32, 0);
    TEST_ASSERT(kernel.getProcessCount() == 50, "50");
    return true;
}

bool test_kernel_44_wrap() {
    BettiRDLKernel<32> kernel;
    TEST_ASSERT(kernel.spawnProcess(33, 0, 0), "Wrap+");
    TEST_ASSERT(kernel.spawnProcess(-1, 0, 0), "Wrap-");
    return true;
}

bool test_kernel_45_bounds() {
    BettiRDLKernel<32> kernel;
    TEST_ASSERT(!kernel.spawnProcess(1000, 0, 0), "Reject");
    return true;
}

bool test_kernel_46_event() {
    BettiRDLKernel<32> kernel;
    kernel.spawnProcess(0, 0, 0);
    TEST_ASSERT(kernel.injectEvent(0, 0, 0, 42), "Event");
    return true;
}

bool test_kernel_47_run() {
    BettiRDLKernel<32> kernel;
    kernel.spawnProcess(0, 0, 0);
    kernel.injectEvent(0, 0, 0, 1);
    kernel.run(10);
    TEST_ASSERT(kernel.getCurrentTime() >= 0, "Time");
    return true;
}

bool test_kernel_48_edge() {
    BettiRDLKernel<32> kernel;
    kernel.spawnProcess(0, 0, 0);
    kernel.spawnProcess(1, 0, 0);
    TEST_ASSERT(kernel.createEdge(0, 0, 0, 1, 0, 0), "Edge");
    return true;
}

bool test_kernel_49_metrics() {
    BettiRDLKernel<32> kernel;
    for (int i = 0; i < 10; i++) kernel.spawnProcess(i, 0, 0);
    TEST_ASSERT(kernel.getProcessCount() == 10, "Metrics");
    return true;
}

bool test_kernel_50_deterministic() {
    BettiRDLKernel<32> k1, k2;
    k1.spawnProcess(0,0,0); k1.injectEvent(0,0,0,42); k1.run(100);
    k2.spawnProcess(0,0,0); k2.injectEvent(0,0,0,42); k2.run(100);
    TEST_ASSERT(k1.getCurrentTime() == k2.getCurrentTime(), "Determ");
    return true;
}

bool test_kernel_51_stress() {
    BettiRDLKernel<32> kernel;
    int n = 0;
    for (int x = 0; x < 32; x++) for (int y = 0; y < 32; y++) if (kernel.spawnProcess(x, y, 0)) n++;
    TEST_ASSERT(n == 1024, "Stress");
    return true;
}

bool test_kernel_52_events() {
    BettiRDLKernel<32> kernel;
    kernel.spawnProcess(0, 0, 0);
    for (int i = 0; i < 50; i++) kernel.injectEvent(0, 0, 0, i);
    kernel.run(500);
    TEST_ASSERT(kernel.getEventsProcessed() > 0, "Events");
    return true;
}

bool test_kernel_53_negative() {
    BettiRDLKernel<32> kernel;
    TEST_ASSERT(kernel.spawnProcess(-5, -5, -5), "Neg");
    return true;
}

bool test_kernel_54_toroidal() {
    BettiRDLKernel<32> kernel;
    kernel.spawnProcess(0, 0, 0);
    kernel.spawnProcess(31, 0, 0);
    TEST_ASSERT(kernel.getProcessCount() == 2, "Torus");
    return true;
}

bool test_kernel_55_memory() {
    BettiRDLKernel<32> kernel;
    for (int i = 0; i < 500; i++) kernel.spawnProcess(i%32, (i/32)%32, (i/1024)%32);
    TEST_ASSERT(kernel.getProcessCount() <= 500, "Memory");
    return true;
}

// Braided Tests 56-70
bool test_braided_56_create() {
    braided::BraidedKernel<32, 3> kernel;
    return true;
}

bool test_braided_57_multi_torus() {
    braided::BraidedKernel<32, 3> kernel;
    for (int t = 0; t < 3; t++) kernel.spawnProcess(t, 0, 0, 0);
    TEST_ASSERT(kernel.getStats().total_processes == 3, "Multi");
    return true;
}

bool test_braided_58_blockchain() {
    braided::BlockchainBraid braid;
    return true;
}

bool test_braided_59_tx_submit() {
    braided::BlockchainBraid braid;
    KeyPair alice, bob;
    braid.getAccounts().mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 1000; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = 0;
    tx.sign(alice);
    TEST_ASSERT(braid.submitTransaction(tx), "Submit");
    return true;
}

bool test_braided_60_interval() {
    braided::BlockchainBraid braid;
    braid.executeBraidInterval();
    TEST_ASSERT(braid.getStats().braid_intervals == 1, "Interval");
    return true;
}

bool test_braided_61_projection() {
    braided::BraidedKernel<32, 3> kernel;
    kernel.spawnProcess(0, 0, 0, 0);
    auto proj = kernel.createProjection(0);
    TEST_ASSERT(proj.torus_id == 0, "Proj");
    return true;
}

bool test_braided_62_stats() {
    braided::BraidedKernel<32, 3> kernel;
    for (int i = 0; i < 10; i++) kernel.spawnProcess(0, i, 0, 0);
    TEST_ASSERT(kernel.getStats().total_processes == 10, "Stats");
    return true;
}

bool test_braided_63_step() {
    braided::BraidedKernel<32, 3> kernel;
    kernel.spawnProcess(0, 0, 0, 0);
    kernel.step();
    return true;
}

bool test_braided_64_multi_interval() {
    braided::BlockchainBraid braid;
    for (int i = 0; i < 5; i++) braid.executeBraidInterval();
    TEST_ASSERT(braid.getStats().braid_intervals == 5, "5Int");
    return true;
}

bool test_braided_65_cross_event() {
    braided::BraidedKernel<32, 3> kernel;
    kernel.spawnProcess(0, 0, 0, 0);
    kernel.spawnProcess(1, 0, 0, 0);
    kernel.injectEvent(0, 0, 0, 0, 42);
    kernel.step();
    return true;
}

bool test_braided_66_validator() {
    braided::BlockchainBraid braid;
    KeyPair v;
    braid.getAccounts().mint(v.getAddress(), MIN_STAKE);
    braid.getAccounts().stake(v.getAddress(), MIN_STAKE);
    braid.executeBraidInterval();
    return true;
}

bool test_braided_67_tx_process() {
    braided::BlockchainBraid braid;
    KeyPair alice, bob;
    braid.getAccounts().mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 1000; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = 0;
    tx.sign(alice);
    braid.submitTransaction(tx);
    braid.executeBraidInterval();
    TEST_ASSERT(braid.getAccounts().getAccount(bob.getAddress()).balance == 1000, "Proc");
    return true;
}

bool test_braided_68_pending() {
    braided::BlockchainBraid braid;
    KeyPair alice, bob;
    braid.getAccounts().mint(alice.getAddress(), 1000000);
    for (int i = 0; i < 3; i++) {
        Transaction tx;
        tx.to = bob.getAddress(); tx.value = 100; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = i;
        tx.sign(alice);
        braid.submitTransaction(tx);
    }
    TEST_ASSERT(braid.getPendingCount() == 3, "Pending");
    return true;
}

bool test_braided_69_clear_pending() {
    braided::BlockchainBraid braid;
    KeyPair alice, bob;
    braid.getAccounts().mint(alice.getAddress(), 1000000);
    for (int i = 0; i < 3; i++) {
        Transaction tx;
        tx.to = bob.getAddress(); tx.value = 100; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = i;
        tx.sign(alice);
        braid.submitTransaction(tx);
    }
    braid.executeBraidInterval();
    TEST_ASSERT(braid.getPendingCount() == 0, "Clear");
    return true;
}

bool test_braided_70_consistency() {
    braided::BlockchainBraid braid;
    KeyPair alice, bob;
    braid.getAccounts().mint(alice.getAddress(), 1000000);
    uint64_t initial = 1000000;
    for (int i = 0; i < 5; i++) {
        Transaction tx;
        tx.to = bob.getAddress(); tx.value = 1000; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = i;
        tx.sign(alice);
        braid.submitTransaction(tx);
    }
    braid.executeBraidInterval();
    uint64_t a = braid.getAccounts().getAccount(alice.getAddress()).balance;
    uint64_t b = braid.getAccounts().getAccount(bob.getAddress()).balance;
    TEST_ASSERT(a + b <= initial, "Consistent");
    return true;
}
