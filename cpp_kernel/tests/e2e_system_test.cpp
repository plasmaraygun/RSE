/**
 * E2E System Test Suite - Production Ready
 * Tests core Braidchain + Inference Network functionality
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

#include "../braided/FaultTolerantBraid.h"
#include "../inference/InferenceNode.h"
#include "../inference/PetalsClient.h"
#include "../network/P2PNetwork.h"
#include "../consensus/ProofOfStake.h"
#include "../core/Economics.h"
#include "../core/Crypto.h"

using namespace std::chrono;

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "  FAIL: " << msg << std::endl; return false; } } while(0)
#define RUN_TEST(fn) do { \
    std::cout << "  " << #fn << "... " << std::flush; \
    auto start = high_resolution_clock::now(); \
    bool result = fn(); \
    auto end = high_resolution_clock::now(); \
    if (result) { \
        std::cout << "PASS (" << duration_cast<milliseconds>(end-start).count() << "ms)" << std::endl; \
        passed++; \
    } else { \
        std::cout << "FAIL" << std::endl; \
        failed++; \
    } \
} while(0)

// ============================================================================
// BRAIDCHAIN CORE TESTS
// ============================================================================

bool test_three_torus_init() {
    braided::FaultTolerantBraid braid(10, false);
    TEST_ASSERT(braid.getTorusA().getTorusId() == 0, "torus A");
    TEST_ASSERT(braid.getTorusB().getTorusId() == 1, "torus B");
    TEST_ASSERT(braid.getTorusC().getTorusId() == 2, "torus C");
    return true;
}

bool test_process_spawn() {
    braided::FaultTolerantBraid braid(10, false);
    for (int t = 0; t < 3; t++) {
        TEST_ASSERT(braid.getTorus(t).spawnProcess(0, 0, 0), "spawn t" + std::to_string(t));
    }
    return true;
}

bool test_event_injection() {
    braided::FaultTolerantBraid braid(10, false);
    for (int t = 0; t < 3; t++) {
        braid.getTorus(t).spawnProcess(0, 0, 0);
        braid.getTorus(t).injectEvent(0, 0, 0, 0, 0, 0, 1);
    }
    braid.run(50);
    TEST_ASSERT(braid.getTorus(0).getEventsProcessed() > 0, "events processed");
    return true;
}

bool test_projection_extraction() {
    braided::FaultTolerantBraid braid(10, false);
    braid.getTorus(0).spawnProcess(0, 0, 0);
    braid.getTorus(0).injectEvent(0, 0, 0, 0, 0, 0, 1);
    braid.run(20);
    
    auto proj = braid.getTorus(0).extractProjection();
    TEST_ASSERT(proj.torus_id == 0, "torus id");
    TEST_ASSERT(proj.verify(), "projection integrity");
    return true;
}

bool test_projection_tamper_detection() {
    braided::FaultTolerantBraid braid(10, false);
    auto proj = braid.getTorus(0).extractProjection();
    TEST_ASSERT(proj.verify(), "original valid");
    proj.total_events_processed++;
    TEST_ASSERT(!proj.verify(), "tamper detected");
    return true;
}

bool test_braid_exchange() {
    braided::FaultTolerantBraid braid(10, false);
    for (int t = 0; t < 3; t++) {
        braid.getTorus(t).spawnProcess(0, 0, 0);
        braid.getTorus(t).injectEvent(0, 0, 0, 0, 0, 0, 1);
    }
    braid.run(200);
    TEST_ASSERT(braid.getBraidCycles() >= 5, "braid cycles");
    return true;
}

bool test_consensus_via_projections() {
    braided::FaultTolerantBraid braid(10, false);
    for (int t = 0; t < 3; t++) {
        braid.getTorus(t).spawnProcess(0, 0, 0);
        braid.getTorus(t).injectEvent(0, 0, 0, 0, 0, 0, 1);
    }
    braid.run(300);
    
    auto p0 = braid.getTorus(0).extractProjection();
    auto p1 = braid.getTorus(1).extractProjection();
    auto p2 = braid.getTorus(2).extractProjection();
    
    TEST_ASSERT(p0.total_events_processed == p1.total_events_processed, "p0==p1");
    TEST_ASSERT(p1.total_events_processed == p2.total_events_processed, "p1==p2");
    return true;
}

bool test_fault_tolerance() {
    braided::FaultTolerantBraid braid(10, true);  // Enable fault injection
    for (int t = 0; t < 3; t++) {
        braid.getTorus(t).spawnProcess(0, 0, 0);
    }
    braid.run(100);
    TEST_ASSERT(braid.getBraidCycles() > 0, "survived faults");
    return true;
}

// ============================================================================
// ECONOMICS TESTS
// ============================================================================

bool test_account_creation() {
    economics::AccountManager accounts;
    crypto::KeyPair key;
    key.generate();
    accounts.mint(key.getAddress(), 1000 * economics::Q_PER_ARQON);
    TEST_ASSERT(accounts.getAccount(key.getAddress()).balance == 1000 * economics::Q_PER_ARQON, "balance");
    return true;
}

bool test_transfer() {
    economics::AccountManager accounts;
    crypto::KeyPair alice, bob;
    alice.generate();
    bob.generate();
    accounts.mint(alice.getAddress(), 1000 * economics::Q_PER_ARQON);
    
    bool result = accounts.transfer(alice.getAddress(), bob.getAddress(), 100 * economics::Q_PER_ARQON);
    TEST_ASSERT(result, "transfer success");
    TEST_ASSERT(accounts.getAccount(bob.getAddress()).balance == 100 * economics::Q_PER_ARQON, "bob balance");
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).balance == 900 * economics::Q_PER_ARQON, "alice balance");
    return true;
}

bool test_staking() {
    economics::AccountManager accounts;
    crypto::KeyPair validator;
    validator.generate();
    accounts.mint(validator.getAddress(), 10000 * economics::Q_PER_ARQON);
    
    bool staked = accounts.stake(validator.getAddress(), 5000 * economics::Q_PER_ARQON);
    TEST_ASSERT(staked, "stake success");
    TEST_ASSERT(accounts.getAccount(validator.getAddress()).stake == 5000 * economics::Q_PER_ARQON, "stake amount");
    TEST_ASSERT(accounts.getAccount(validator.getAddress()).balance == 5000 * economics::Q_PER_ARQON, "remaining");
    return true;
}

bool test_unstaking() {
    economics::AccountManager accounts;
    crypto::KeyPair validator;
    validator.generate();
    accounts.mint(validator.getAddress(), 10000 * economics::Q_PER_ARQON);
    accounts.stake(validator.getAddress(), 5000 * economics::Q_PER_ARQON);
    
    bool unstaked = accounts.unstake(validator.getAddress(), 2000 * economics::Q_PER_ARQON);
    TEST_ASSERT(unstaked, "unstake success");
    TEST_ASSERT(accounts.getAccount(validator.getAddress()).stake == 3000 * economics::Q_PER_ARQON, "remaining stake");
    return true;
}

// ============================================================================
// CONSENSUS TESTS
// ============================================================================

bool test_validator_registration() {
    economics::AccountManager accounts;
    consensus::PoSConsensus consensus(accounts);
    
    crypto::KeyPair validator;
    validator.generate();
    accounts.mint(validator.getAddress(), 100000 * economics::Q_PER_ARQON);
    accounts.stake(validator.getAddress(), 50000 * economics::Q_PER_ARQON);
    
    bool registered = consensus.registerValidator(validator.getAddress());
    TEST_ASSERT(registered, "registered");
    TEST_ASSERT(consensus.validatorCount() == 1, "count");
    return true;
}

bool test_proposer_selection() {
    economics::AccountManager accounts;
    consensus::PoSConsensus consensus(accounts);
    
    for (int i = 0; i < 5; i++) {
        crypto::KeyPair v;
        v.generate();
        accounts.mint(v.getAddress(), 100000 * economics::Q_PER_ARQON);
        accounts.stake(v.getAddress(), 50000 * economics::Q_PER_ARQON);
        consensus.registerValidator(v.getAddress());
    }
    
    auto proposer = consensus.selectProposer(1);
    bool valid = false;
    for (size_t i = 0; i < crypto::ADDRESS_SIZE; i++) {
        if (proposer[i] != 0) { valid = true; break; }
    }
    TEST_ASSERT(valid, "proposer selected");
    return true;
}

// ============================================================================
// INFERENCE NETWORK TESTS
// ============================================================================

bool test_inference_node_registration() {
    economics::AccountManager accounts;
    inference::InferenceNetworkManager network(accounts);
    
    crypto::KeyPair node_key;
    node_key.generate();
    accounts.mint(node_key.getAddress(), 10000 * economics::Q_PER_ARQON);
    accounts.stake(node_key.getAddress(), 5000 * economics::Q_PER_ARQON);
    
    inference::InferenceNodeInfo node;
    node.address = node_key.getAddress();
    node.type = inference::NodeType::GPU_INFERENCE;
    node.public_name = "TestGPU";
    node.staked_amount = 5000 * economics::Q_PER_ARQON;
    node.uptime_percent = 99.9;
    
    inference::GPUInfo gpu;
    gpu.name = "RTX 4090";
    gpu.vram_mb = 24576;
    gpu.tflops_fp16 = 82.6;
    node.gpus.push_back(gpu);
    
    bool registered = network.registerNode(node);
    TEST_ASSERT(registered, "node registered");
    
    auto stats = network.getStats();
    TEST_ASSERT(stats.total_nodes == 1, "node count");
    TEST_ASSERT(stats.gpu_nodes == 1, "gpu count");
    return true;
}

bool test_relay_node() {
    economics::AccountManager accounts;
    inference::InferenceNetworkManager network(accounts);
    
    crypto::KeyPair node_key;
    node_key.generate();
    accounts.mint(node_key.getAddress(), 1000 * economics::Q_PER_ARQON);
    accounts.stake(node_key.getAddress(), 500 * economics::Q_PER_ARQON);
    
    inference::InferenceNodeInfo node;
    node.address = node_key.getAddress();
    node.type = inference::NodeType::RELAY_ONLY;
    node.public_name = "Relay";
    node.staked_amount = 500 * economics::Q_PER_ARQON;
    
    bool registered = network.registerNode(node);
    TEST_ASSERT(registered, "relay registered");
    
    auto stats = network.getStats();
    TEST_ASSERT(stats.relay_nodes == 1, "relay count");
    return true;
}

bool test_epoch_processing() {
    economics::AccountManager accounts;
    inference::InferenceNetworkManager network(accounts);
    
    // Register nodes
    for (int i = 0; i < 5; i++) {
        crypto::KeyPair key;
        key.generate();
        accounts.mint(key.getAddress(), 10000 * economics::Q_PER_ARQON);
        accounts.stake(key.getAddress(), 5000 * economics::Q_PER_ARQON);
        
        inference::InferenceNodeInfo node;
        node.address = key.getAddress();
        node.type = inference::NodeType::GPU_INFERENCE;
        node.staked_amount = 5000 * economics::Q_PER_ARQON;
        node.uptime_percent = 99.0;
        
        inference::GPUInfo gpu;
        gpu.vram_mb = 24576;
        gpu.tflops_fp16 = 82.6;
        node.gpus.push_back(gpu);
        
        network.registerNode(node);
    }
    
    network.processEpoch();
    auto stats = network.getStats();
    TEST_ASSERT(stats.current_epoch == 1, "epoch advanced");
    return true;
}

bool test_petals_client() {
    inference::PetalsClient client;
    client.start();
    TEST_ASSERT(client.isRunning(), "started");
    
    auto session = client.createSession("test-model");
    TEST_ASSERT(session > 0, "session created");
    
    client.closeSession(session);
    client.stop();
    TEST_ASSERT(!client.isRunning(), "stopped");
    return true;
}

// ============================================================================
// P2P NETWORK TESTS
// ============================================================================

bool test_p2p_encryption() {
    std::array<uint8_t, 32> key{};
    for (int i = 0; i < 32; i++) key[i] = i * 7;
    
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5, 6, 7, 8};
    auto ciphertext = p2p::encryptPacket(plaintext, key.data());
    TEST_ASSERT(ciphertext != plaintext, "encrypted");
    
    auto decrypted = p2p::decryptPacket(ciphertext, key.data());
    TEST_ASSERT(decrypted == plaintext, "decrypted");
    return true;
}

bool test_state_sync_serialization() {
    p2p::StateSyncRequest req;
    req.from_snapshot = 100;
    req.to_snapshot = 200;
    req.include_accounts = true;
    req.include_projections = true;
    
    auto data = req.serialize();
    auto parsed = p2p::StateSyncRequest::deserialize(data);
    
    TEST_ASSERT(parsed.from_snapshot == 100, "from");
    TEST_ASSERT(parsed.to_snapshot == 200, "to");
    TEST_ASSERT(parsed.include_accounts, "accounts");
    TEST_ASSERT(parsed.include_projections, "projections");
    return true;
}

bool test_message_types() {
    TEST_ASSERT((int)p2p::MsgType::TX == 0x09, "TX");
    TEST_ASSERT((int)p2p::MsgType::STATE_REQUEST == 0x0D, "STATE_REQUEST");
    TEST_ASSERT((int)p2p::MsgType::PROJECTION == 0x0F, "PROJECTION");
    return true;
}

// ============================================================================
// CRYPTO TESTS
// ============================================================================

bool test_keypair_generation() {
    crypto::KeyPair kp;
    kp.generate();
    
    auto addr = kp.getAddress();
    bool non_zero = false;
    for (auto b : addr) if (b != 0) non_zero = true;
    TEST_ASSERT(non_zero, "address generated");
    return true;
}

bool test_signing() {
    crypto::KeyPair kp;
    kp.generate();
    
    uint8_t msg[] = "Hello, Arqon!";
    auto sig = kp.sign(msg, sizeof(msg));
    
    // Signature should be non-zero
    bool non_zero = false;
    for (auto b : sig) if (b != 0) non_zero = true;
    TEST_ASSERT(non_zero, "signature generated");
    return true;
}

bool test_address_hex() {
    crypto::KeyPair kp;
    kp.generate();
    
    std::string hex = crypto::AddressUtil::toHex(kp.getAddress());
    TEST_ASSERT(hex.substr(0, 2) == "0x", "prefix");
    TEST_ASSERT(hex.length() == 42, "length");
    
    crypto::Address parsed;
    bool ok = crypto::AddressUtil::fromHex(hex, parsed);
    TEST_ASSERT(ok, "parsed");
    TEST_ASSERT(parsed == kp.getAddress(), "roundtrip");
    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ARQON E2E SYSTEM TEST SUITE                            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    int passed = 0, failed = 0;
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  BRAIDCHAIN CORE\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_three_torus_init);
    RUN_TEST(test_process_spawn);
    RUN_TEST(test_event_injection);
    RUN_TEST(test_projection_extraction);
    RUN_TEST(test_projection_tamper_detection);
    RUN_TEST(test_braid_exchange);
    RUN_TEST(test_consensus_via_projections);
    RUN_TEST(test_fault_tolerance);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  ECONOMICS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_account_creation);
    RUN_TEST(test_transfer);
    RUN_TEST(test_staking);
    RUN_TEST(test_unstaking);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CONSENSUS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_validator_registration);
    RUN_TEST(test_proposer_selection);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  INFERENCE NETWORK\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_inference_node_registration);
    RUN_TEST(test_relay_node);
    RUN_TEST(test_epoch_processing);
    RUN_TEST(test_petals_client);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  P2P NETWORK\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_p2p_encryption);
    RUN_TEST(test_state_sync_serialization);
    RUN_TEST(test_message_types);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CRYPTO\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_keypair_generation);
    RUN_TEST(test_signing);
    RUN_TEST(test_address_hex);
    
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  PASSED: " << passed << "  FAILED: " << failed << "                                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    return failed > 0 ? 1 : 0;
}
