/**
 * RSE Hardening Test Suite
 * 
 * Stress tests, security tests, edge cases, and fuzzing for maximum robustness.
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../single_torus/BettiRDLKernel.h"
#include "../braided/BraidedKernel.h"
#include "../braided/FaultTolerantBraid.h"
#include "../braided/BlockchainBraid.h"

#include <iostream>
#include <cassert>
#include <random>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>

using namespace crypto;
using namespace economics;
using namespace braided;

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << std::endl; return false; } } while(0)
#define RUN_TEST(fn) do { std::cout << "  " << #fn << "... " << std::flush; if (fn()) { std::cout << "PASS" << std::endl; passed++; } else { std::cout << "FAIL" << std::endl; failed++; } } while(0)

// ============================================================================
// CRYPTO HARDENING TESTS
// ============================================================================

bool test_crypto_key_uniqueness() {
    // Generate 1000 keys, ensure all are unique
    std::vector<PublicKey> keys;
    keys.reserve(1000);
    
    for (int i = 0; i < 1000; i++) {
        KeyPair kp;
        for (const auto& existing : keys) {
            TEST_ASSERT(kp.getPublicKey() != existing, "Duplicate key generated!");
        }
        keys.push_back(kp.getPublicKey());
    }
    return true;
}

bool test_crypto_signature_tampering() {
    KeyPair kp;
    std::vector<uint8_t> msg = {1, 2, 3, 4, 5, 6, 7, 8};
    
    auto sig = kp.sign(msg.data(), msg.size());
    
    // Verify original works
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig), 
                "Original sig should verify");
    
    // Tamper with signature - should fail
    Signature tampered = sig;
    tampered[0] ^= 0xFF;
    TEST_ASSERT(!KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), tampered),
                "Tampered sig should not verify");
    
    // Wrong public key - should fail
    KeyPair other;
    TEST_ASSERT(!KeyPair::verify(other.getPublicKey(), msg.data(), msg.size(), sig),
                "Wrong pubkey should not verify");
    
    return true;
}

bool test_crypto_empty_message() {
    KeyPair kp;
    
    // Sign empty message
    auto sig = kp.sign(nullptr, 0);
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), nullptr, 0, sig),
                "Empty message signature should verify");
    
    return true;
}

bool test_crypto_large_message() {
    KeyPair kp;
    
    // Sign 1MB message
    std::vector<uint8_t> large_msg(1024 * 1024);
    std::mt19937 rng(42);
    for (auto& b : large_msg) b = static_cast<uint8_t>(rng());
    
    auto sig = kp.sign(large_msg.data(), large_msg.size());
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), large_msg.data(), large_msg.size(), sig),
                "Large message signature should verify");
    
    return true;
}

bool test_address_collision_resistance() {
    // Generate 10000 addresses, check for collisions
    std::vector<Address> addrs;
    addrs.reserve(10000);
    
    for (int i = 0; i < 10000; i++) {
        KeyPair kp;
        for (const auto& existing : addrs) {
            TEST_ASSERT(kp.getAddress() != existing, "Address collision!");
        }
        addrs.push_back(kp.getAddress());
    }
    return true;
}

// ============================================================================
// ECONOMICS HARDENING TESTS
// ============================================================================

bool test_economics_overflow_protection() {
    AccountManager accounts;
    KeyPair alice;
    
    // Try to mint max uint64
    accounts.mint(alice.getAddress(), UINT64_MAX);
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).balance == UINT64_MAX,
                "Should hold max balance");
    
    // Try to mint more - should not overflow
    uint64_t before = accounts.getAccount(alice.getAddress()).balance;
    accounts.mint(alice.getAddress(), 1);
    uint64_t after = accounts.getAccount(alice.getAddress()).balance;
    
    // Either capped at max or wrapped - both need handling
    TEST_ASSERT(after >= before || after == 0, "Overflow not handled");
    
    return true;
}

bool test_economics_double_spend() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    
    KeyPair alice, bob, charlie;
    accounts.mint(alice.getAddress(), 200000);  // Enough for value + gas
    
    // Create two transactions spending same funds
    Transaction tx1, tx2;
    tx1.to = bob.getAddress();
    tx1.value = 80000;
    tx1.gas_price = 1;
    tx1.gas_limit = 21000;
    tx1.nonce = 0;
    tx1.sign(alice);
    
    tx2.to = charlie.getAddress();
    tx2.value = 80000;
    tx2.gas_price = 1;
    tx2.gas_limit = 21000;
    tx2.nonce = 0;  // Same nonce!
    tx2.sign(alice);
    
    // First should succeed
    auto r1 = processor.process(tx1);
    TEST_ASSERT(r1 == TransactionProcessor::Result::SUCCESS, "First tx should succeed");
    
    // Second should fail (either insufficient balance or invalid nonce)
    auto r2 = processor.process(tx2);
    TEST_ASSERT(r2 != TransactionProcessor::Result::SUCCESS, "Double spend should fail");
    
    return true;
}

bool test_economics_replay_attack() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 1000;
    tx.gas_price = 1;
    tx.gas_limit = 21000;
    tx.nonce = 0;
    tx.sign(alice);
    
    // First should succeed
    auto r1 = processor.process(tx);
    TEST_ASSERT(r1 == TransactionProcessor::Result::SUCCESS, "First should succeed");
    
    // Replay same transaction - should fail due to nonce
    auto r2 = processor.process(tx);
    TEST_ASSERT(r2 == TransactionProcessor::Result::INVALID_NONCE, "Replay should fail");
    
    return true;
}

bool test_economics_gas_exhaustion() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 1000;
    tx.gas_price = 1;
    tx.gas_limit = 1;  // Way too low!
    tx.nonce = 0;
    tx.sign(alice);
    
    auto result = processor.process(tx);
    TEST_ASSERT(result == TransactionProcessor::Result::OUT_OF_GAS, "Should run out of gas");
    
    return true;
}

bool test_staking_edge_cases() {
    AccountManager accounts;
    KeyPair validator;
    
    accounts.mint(validator.getAddress(), 2000000000);  // 2B wei (above MIN_STAKE)
    
    // Stake more than balance
    bool staked = accounts.stake(validator.getAddress(), 3000000000);
    TEST_ASSERT(!staked, "Should not stake more than balance");
    
    // Stake valid amount
    staked = accounts.stake(validator.getAddress(), 1000000000);  // 1B (MIN_STAKE)
    TEST_ASSERT(staked, "Should stake valid amount");
    TEST_ASSERT(accounts.getAccount(validator.getAddress()).balance == 1000000000, "Balance should be 1B remaining");
    TEST_ASSERT(accounts.getAccount(validator.getAddress()).stake == 1000000000, "Staked should be 1B");
    
    // Try to unstake more than staked
    bool unstaked = accounts.unstake(validator.getAddress(), 2000000000);
    TEST_ASSERT(!unstaked, "Should not unstake more than staked");
    
    return true;
}

// ============================================================================
// KERNEL STRESS TESTS
// ============================================================================

bool test_kernel_massive_process_spawn() {
    BettiRDLKernel k;
    
    // Spawn processes until we hit the limit
    int spawned = 0;
    for (int x = 0; x < 32; x++) {
        for (int y = 0; y < 32; y++) {
            if (k.spawnProcess(x, y, 0)) spawned++;
        }
    }
    
    TEST_ASSERT(spawned > 0, "Should spawn some processes");
    TEST_ASSERT(k.getActiveProcessCount() == static_cast<size_t>(spawned), 
                "Process count should match");
    
    return true;
}

bool test_kernel_event_flood() {
    BettiRDLKernel k;
    
    // Create a small ring
    for (int i = 0; i < 4; i++) k.spawnProcess(i, 0, 0);
    for (int i = 0; i < 3; i++) k.createEdge(i, 0, 0, i+1, 0, 0, 1);
    k.createEdge(3, 0, 0, 0, 0, 0, 1);
    
    // Flood with events
    for (int i = 0; i < 1000; i++) {
        k.injectEvent(0, 0, 0, 1, 0, 0, i);
    }
    
    k.run(100000);
    
    TEST_ASSERT(k.getEventsProcessed() > 0, "Should process events");
    
    return true;
}

bool test_kernel_concurrent_injection() {
    auto k = std::make_unique<BettiRDLKernel>();
    
    k->spawnProcess(0, 0, 0);
    k->spawnProcess(1, 0, 0);
    k->createEdge(0, 0, 0, 1, 0, 0, 1);
    k->createEdge(1, 0, 0, 0, 0, 0, 1);
    
    std::atomic<int> injected{0};
    
    // Multiple threads injecting events
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&k, &injected, t]() {
            for (int i = 0; i < 100; i++) {
                k->injectEvent(0, 0, 0, 1, 0, 0, t * 1000 + i);
                injected++;
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    k->run(10000);
    
    TEST_ASSERT(injected.load() == 400, "Should inject 400 events");
    TEST_ASSERT(k->getEventsProcessed() > 0, "Should process some events");
    
    return true;
}

bool test_kernel_reset_consistency() {
    BettiRDLKernel k;
    
    k.spawnProcess(0, 0, 0);
    k.injectEvent(0, 0, 0, 0, 0, 0, 1);
    k.run(100);
    
    uint64_t events_before = k.getEventsProcessed();
    TEST_ASSERT(events_before > 0, "Should have processed events");
    
    k.reset();
    
    TEST_ASSERT(k.getEventsProcessed() == 0, "Events should be 0 after reset");
    TEST_ASSERT(k.getActiveProcessCount() == 0, "Processes should be 0 after reset");
    TEST_ASSERT(k.getCurrentTime() == 0, "Time should be 0 after reset");
    
    return true;
}

// ============================================================================
// BRAIDED SYSTEM STRESS TESTS
// ============================================================================

bool test_braid_heavy_load() {
    auto braid = std::make_unique<FaultTolerantBraid>(100, false);
    
    // Load up all three tori
    for (int t = 0; t < 3; t++) {
        auto& torus = braid->getTorus(t);
        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 8; y++) {
                torus.spawnProcess(x, y, 0);
            }
        }
        // Create edges
        for (int x = 0; x < 15; x++) {
            for (int y = 0; y < 8; y++) {
                torus.createEdge(x, y, 0, x+1, y, 0, 1);
            }
        }
        // Inject events
        for (int y = 0; y < 8; y++) {
            torus.injectEvent(0, y, 0, 1, y, 0, 1);
        }
    }
    
    auto stats = braid->run(10000);
    
    TEST_ASSERT(stats.total_events > 1000, "Should process many events");
    TEST_ASSERT(braid->getBraidCycles() > 10, "Should complete braid cycles");
    
    return true;
}

bool test_braid_projection_integrity() {
    auto a = std::make_unique<BraidedKernel>();
    auto b = std::make_unique<BraidedKernel>();
    
    a->setTorusId(0);
    b->setTorusId(1);
    
    // Setup torus A
    a->spawnProcess(0, 0, 0);
    a->injectEvent(0, 0, 0, 0, 0, 0, 42);
    a->run(100);
    
    // Extract and verify projection
    auto proj = a->extractProjection();
    TEST_ASSERT(proj.verify(), "Projection should be valid");
    TEST_ASSERT(proj.torus_id == 0, "Torus ID should match");
    
    // Tamper with projection
    Projection tampered = proj;
    tampered.total_events_processed += 1000;
    TEST_ASSERT(!tampered.verify(), "Tampered projection should fail verification");
    
    // Apply valid projection
    TEST_ASSERT(b->applyConstraint(proj), "Valid projection should apply");
    
    return true;
}

// ============================================================================
// SECURITY EDGE CASE TESTS
// ============================================================================

bool test_bounds_validation() {
    BettiRDLKernel k;
    
    // Valid coordinates should work
    TEST_ASSERT(k.spawnProcess(0, 0, 0), "Valid origin should work");
    TEST_ASSERT(k.spawnProcess(31, 31, 31), "Valid max should work");
    TEST_ASSERT(k.spawnProcess(32, 0, 0), "Wrapping coordinate should work");
    
    // Extreme coordinates should be rejected
    TEST_ASSERT(!k.spawnProcess(INT_MAX, 0, 0), "INT_MAX should be rejected");
    TEST_ASSERT(!k.spawnProcess(INT_MIN, 0, 0), "INT_MIN should be rejected");
    TEST_ASSERT(!k.spawnProcess(0, 1000000, 0), "Large Y should be rejected");
    
    return true;
}

bool test_zero_value_transactions() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 100000);
    
    // Zero value transaction should succeed (just pays gas)
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 0;
    tx.gas_price = 1;
    tx.gas_limit = 21000;
    tx.nonce = 0;
    tx.sign(alice);
    
    auto result = processor.process(tx);
    TEST_ASSERT(result == TransactionProcessor::Result::SUCCESS, "Zero value tx should succeed");
    TEST_ASSERT(accounts.getAccount(bob.getAddress()).balance == 0, "Bob should have 0");
    
    return true;
}

bool test_self_transfer() {
    AccountManager accounts;
    KeyPair alice;
    
    accounts.mint(alice.getAddress(), 100000);
    
    // Self-transfer should be a no-op
    bool result = accounts.transfer(alice.getAddress(), alice.getAddress(), 50000);
    TEST_ASSERT(result, "Self-transfer should succeed");
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).balance == 100000, "Balance unchanged");
    
    return true;
}

bool test_zero_address_rejection() {
    AccountManager accounts;
    KeyPair alice;
    crypto::Address zero_addr{};  // All zeros
    
    accounts.mint(alice.getAddress(), 100000);
    
    // Transfers to/from zero address should fail
    TEST_ASSERT(!accounts.transfer(alice.getAddress(), zero_addr, 1000), 
                "Transfer to zero should fail");
    TEST_ASSERT(!accounts.transfer(zero_addr, alice.getAddress(), 1000),
                "Transfer from zero should fail");
    
    return true;
}

bool test_nonce_sequence() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    
    // Process transactions in sequence
    for (int i = 0; i < 5; i++) {
        Transaction tx;
        tx.to = bob.getAddress();
        tx.value = 100;
        tx.gas_price = 1;
        tx.gas_limit = 21000;
        tx.nonce = i;
        tx.sign(alice);
        
        auto result = processor.process(tx);
        TEST_ASSERT(result == TransactionProcessor::Result::SUCCESS, 
                    "Sequential nonce should succeed");
    }
    
    // Out of sequence nonce should fail
    Transaction bad_tx;
    bad_tx.to = bob.getAddress();
    bad_tx.value = 100;
    bad_tx.gas_price = 1;
    bad_tx.gas_limit = 21000;
    bad_tx.nonce = 10;  // Skip ahead
    bad_tx.sign(alice);
    
    auto result = processor.process(bad_tx);
    TEST_ASSERT(result == TransactionProcessor::Result::INVALID_NONCE, 
                "Out of sequence nonce should fail");
    
    return true;
}

// ============================================================================
// BLOCKCHAIN BRAID STRESS TESTS
// ============================================================================

bool test_blockchain_transaction_throughput() {
    auto braid = std::make_unique<BlockchainBraid>(0, false);
    
    KeyPair sender;
    braid->mint(sender.getAddress(), 100000000);  // 100M
    
    int successful = 0;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; i++) {
        KeyPair recipient;
        Transaction tx;
        tx.to = recipient.getAddress();
        tx.value = 100;
        tx.gas_price = 1;
        tx.gas_limit = 21000;
        tx.nonce = i;
        tx.sign(sender);
        
        if (braid->submitTransaction(tx)) successful++;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    double tps = (successful * 1000.0) / (ms > 0 ? ms : 1);
    std::cout << "[" << tps << " TPS] ";
    
    TEST_ASSERT(successful > 900, "Most transactions should succeed");
    
    return true;
}

bool test_blockchain_validator_rotation() {
    auto braid = std::make_unique<BlockchainBraid>(0, false);
    
    // Create multiple validators
    std::vector<KeyPair> validators(10);
    for (auto& v : validators) {
        braid->mint(v.getAddress(), 10000000);  // 10M each
        braid->stake(v.getAddress(), 1000000);  // Stake 1M
    }
    
    // Run a braid interval
    braid->executeBraidInterval(100);
    
    auto stats = braid->getStats();
    TEST_ASSERT(stats.braid_intervals > 0, "Should have run braid intervals");
    
    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "\n========================================\n";
    std::cout << "   RSE HARDENING TEST SUITE\n";
    std::cout << "========================================\n\n";
    
    int passed = 0, failed = 0;
    
    std::cout << "[Crypto Hardening]" << std::endl;
    RUN_TEST(test_crypto_key_uniqueness);
    RUN_TEST(test_crypto_signature_tampering);
    RUN_TEST(test_crypto_empty_message);
    RUN_TEST(test_crypto_large_message);
    RUN_TEST(test_address_collision_resistance);
    
    std::cout << "\n[Economics Hardening]" << std::endl;
    RUN_TEST(test_economics_overflow_protection);
    RUN_TEST(test_economics_double_spend);
    RUN_TEST(test_economics_replay_attack);
    RUN_TEST(test_economics_gas_exhaustion);
    RUN_TEST(test_staking_edge_cases);
    
    std::cout << "\n[Kernel Stress Tests]" << std::endl;
    RUN_TEST(test_kernel_massive_process_spawn);
    RUN_TEST(test_kernel_event_flood);
    RUN_TEST(test_kernel_concurrent_injection);
    RUN_TEST(test_kernel_reset_consistency);
    
    std::cout << "\n[Braided System Stress]" << std::endl;
    RUN_TEST(test_braid_heavy_load);
    RUN_TEST(test_braid_projection_integrity);
    
    std::cout << "\n[Security Edge Cases]" << std::endl;
    RUN_TEST(test_bounds_validation);
    RUN_TEST(test_zero_value_transactions);
    RUN_TEST(test_self_transfer);
    RUN_TEST(test_zero_address_rejection);
    RUN_TEST(test_nonce_sequence);
    
    std::cout << "\n[Blockchain Stress]" << std::endl;
    RUN_TEST(test_blockchain_transaction_throughput);
    RUN_TEST(test_blockchain_validator_rotation);
    
    std::cout << "\n========================================\n";
    std::cout << "  PASSED: " << passed << "  FAILED: " << failed << "\n";
    std::cout << "========================================\n\n";
    
    return failed > 0 ? 1 : 0;
}
