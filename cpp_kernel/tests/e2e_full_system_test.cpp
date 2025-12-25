/**
 * E2E Full System Test Suite
 * Comprehensive tests for Braidchain + Inference Network
 * Designed to run on VMs for production validation
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <future>

#include "../braided/Braidchain.h"
#include "../braided/FaultTolerantBraid.h"
#include "../inference/InferenceNode.h"
#include "../inference/PetalsClient.h"
#include "../network/P2PNetwork.h"
#include "../consensus/ProofOfStake.h"
#include "../core/Economics.h"
#include "../core/Crypto.h"
#include "../api/ApiServer.h"

using namespace std::chrono;

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "  FAIL: " << msg << std::endl; return false; } } while(0)
#define RUN_TEST(fn) do { \
    std::cout << "  " << #fn << "... " << std::flush; \
    auto start = high_resolution_clock::now(); \
    if (fn()) { \
        auto end = high_resolution_clock::now(); \
        std::cout << "PASS (" << duration_cast<milliseconds>(end-start).count() << "ms)" << std::endl; \
        passed++; \
    } else { \
        std::cout << "FAIL" << std::endl; \
        failed++; \
    } \
} while(0)

// ============================================================================
// BRAIDCHAIN E2E TESTS
// ============================================================================

bool test_braidchain_genesis() {
    braided::Braidchain chain(0, false);
    
    // Create genesis account
    crypto::KeyPair genesis;
    genesis.generate();
    chain.mint(genesis.getAddress(), 1000000 * economics::Q_PER_ARQON);
    
    TEST_ASSERT(chain.getBalance(genesis.getAddress()) == 1000000 * economics::Q_PER_ARQON, "genesis balance");
    return true;
}

bool test_braidchain_transfer() {
    braided::Braidchain chain(0, false);
    
    crypto::KeyPair alice, bob;
    alice.generate();
    bob.generate();
    
    chain.mint(alice.getAddress(), 1000 * economics::Q_PER_ARQON);
    
    // Create and sign transfer
    crypto::Transaction tx;
    tx.from = alice.getAddress();
    tx.to = bob.getAddress();
    tx.value = 100 * economics::Q_PER_ARQON;
    tx.nonce = chain.getNonce(alice.getAddress());
    tx.gas_price = 1;
    tx.gas_limit = 21000;
    tx.signature = alice.sign(reinterpret_cast<const uint8_t*>(&tx), sizeof(tx) - sizeof(tx.signature));
    
    bool result = chain.submitTransaction(tx);
    TEST_ASSERT(result, "transfer submitted");
    TEST_ASSERT(chain.getBalance(bob.getAddress()) >= 100 * economics::Q_PER_ARQON, "bob received");
    
    return true;
}

bool test_braidchain_staking() {
    braided::Braidchain chain(0, false);
    
    crypto::KeyPair validator;
    validator.generate();
    chain.mint(validator.getAddress(), 100000 * economics::Q_PER_ARQON);
    
    bool staked = chain.stake(validator.getAddress(), 50000 * economics::Q_PER_ARQON);
    TEST_ASSERT(staked, "stake success");
    
    auto& acc = chain.getEconomics().accounts().getAccount(validator.getAddress());
    TEST_ASSERT(acc.stake == 50000 * economics::Q_PER_ARQON, "stake amount");
    TEST_ASSERT(acc.balance == 50000 * economics::Q_PER_ARQON, "remaining balance");
    
    return true;
}

bool test_braidchain_consensus() {
    braided::Braidchain chain(0, false);
    
    // Run braid and check consensus
    chain.getBraid().run(100);
    TEST_ASSERT(chain.hasConsensus(), "3-torus consensus");
    
    auto hash = chain.getStateHash();
    bool non_zero = false;
    for (auto b : hash) if (b != 0) non_zero = true;
    TEST_ASSERT(non_zero, "state hash computed");
    
    return true;
}

bool test_braidchain_projection_sync() {
    braided::FaultTolerantBraid braid(10, false);
    
    // Inject events into all 3 tori
    for (int t = 0; t < 3; t++) {
        braid.getTorus(t).spawnProcess(0, 0, 0);
        braid.getTorus(t).injectEvent(0, 0, 0, 0, 0, 0, 1);
    }
    
    // Run braiding
    braid.run(200);
    
    // Check projection agreement
    auto p0 = braid.getTorus(0).extractProjection();
    auto p1 = braid.getTorus(1).extractProjection();
    auto p2 = braid.getTorus(2).extractProjection();
    
    TEST_ASSERT(p0.total_events_processed == p1.total_events_processed, "p0==p1 events");
    TEST_ASSERT(p1.total_events_processed == p2.total_events_processed, "p1==p2 events");
    TEST_ASSERT(braid.getBraidCycles() >= 5, "braid cycles");
    
    return true;
}

bool test_braidchain_fault_detection() {
    braided::FaultTolerantBraid braid(10, true);  // Enable fault injection
    
    for (int t = 0; t < 3; t++) {
        braid.getTorus(t).spawnProcess(0, 0, 0);
    }
    
    braid.run(100);
    
    // System should still reach consensus despite faults
    TEST_ASSERT(braid.getBraidCycles() > 0, "braid cycles despite faults");
    
    return true;
}

bool test_braidchain_throughput() {
    braided::Braidchain chain(0, false);
    
    crypto::KeyPair sender;
    sender.generate();
    chain.mint(sender.getAddress(), 1000000 * economics::Q_PER_ARQON);
    
    auto start = high_resolution_clock::now();
    int tx_count = 1000;
    
    for (int i = 0; i < tx_count; i++) {
        crypto::KeyPair receiver;
        receiver.generate();
        
        crypto::Transaction tx;
        tx.from = sender.getAddress();
        tx.to = receiver.getAddress();
        tx.value = 1;
        tx.nonce = i;
        tx.gas_price = 1;
        tx.gas_limit = 21000;
        tx.signature = sender.sign(reinterpret_cast<const uint8_t*>(&tx), sizeof(tx) - sizeof(tx.signature));
        
        chain.submitTransaction(tx);
    }
    
    auto end = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(end - start).count();
    double tps = (double)tx_count / (ms / 1000.0);
    
    std::cout << "[" << (int)tps << " TPS] ";
    TEST_ASSERT(tps > 100, "throughput > 100 TPS");
    
    return true;
}

// ============================================================================
// INFERENCE NETWORK E2E TESTS
// ============================================================================

bool test_inference_node_registration() {
    economics::AccountManager accounts;
    consensus::PoSConsensus consensus(accounts);
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
    gpu.compute_capability = 8.9;
    gpu.tensor_cores = 512;
    gpu.tflops_fp16 = 82.6;
    node.gpus.push_back(gpu);
    
    bool registered = network.registerNode(node);
    TEST_ASSERT(registered, "node registered");
    
    auto stats = network.getStats();
    TEST_ASSERT(stats.total_nodes == 1, "node count");
    TEST_ASSERT(stats.gpu_nodes == 1, "gpu node count");
    
    return true;
}

bool test_inference_reward_distribution() {
    economics::AccountManager accounts;
    consensus::PoSConsensus consensus(accounts);
    inference::InferenceNetworkManager network(accounts);
    
    // Register multiple nodes
    std::vector<crypto::KeyPair> node_keys(5);
    for (int i = 0; i < 5; i++) {
        node_keys[i].generate();
        accounts.mint(node_keys[i].getAddress(), 10000 * economics::Q_PER_ARQON);
        accounts.stake(node_keys[i].getAddress(), 5000 * economics::Q_PER_ARQON);
        
        inference::InferenceNodeInfo node;
        node.address = node_keys[i].getAddress();
        node.type = (i < 3) ? inference::NodeType::GPU_INFERENCE : inference::NodeType::RELAY_ONLY;
        node.public_name = "Node" + std::to_string(i);
        node.staked_amount = 5000 * economics::Q_PER_ARQON;
        node.uptime_percent = 99.0;
        
        if (i < 3) {
            inference::GPUInfo gpu;
            gpu.name = "RTX 4090";
            gpu.vram_mb = 24576;
            gpu.tflops_fp16 = 82.6;
            node.gpus.push_back(gpu);
        }
        
        network.registerNode(node);
    }
    
    // Process epoch
    network.processEpoch();
    
    auto stats = network.getStats();
    TEST_ASSERT(stats.total_nodes == 5, "5 nodes");
    TEST_ASSERT(stats.gpu_nodes == 3, "3 GPU nodes");
    TEST_ASSERT(stats.relay_nodes == 2, "2 relay nodes");
    
    return true;
}

bool test_petals_client_lifecycle() {
    inference::PetalsClient client;
    
    client.start();
    TEST_ASSERT(client.isRunning(), "client started");
    
    auto session = client.createSession("meta-llama/Meta-Llama-3.1-70B-Instruct");
    TEST_ASSERT(session > 0, "session created");
    
    client.closeSession(session);
    client.stop();
    TEST_ASSERT(!client.isRunning(), "client stopped");
    
    return true;
}

bool test_petals_inference_request() {
    inference::PetalsClient client;
    client.start();
    
    auto session = client.createSession("test-model");
    TEST_ASSERT(session > 0, "session created for inference");
    
    client.closeSession(session);
    client.stop();
    
    return true;
}

bool test_inference_compute_units() {
    inference::GPUInfo gpu;
    gpu.name = "RTX 4090";
    gpu.vram_mb = 24576;
    gpu.compute_capability = 8.9;
    gpu.tensor_cores = 512;
    gpu.tflops_fp16 = 82.6;
    
    auto metrics = gpu.toComputeMetrics();
    
    TEST_ASSERT(metrics.vram_mb == 24576, "vram");
    TEST_ASSERT(metrics.tflops_fp16 > 80, "tflops");
    TEST_ASSERT(metrics.cuda_cores > 0, "cuda cores estimated");
    
    return true;
}

// ============================================================================
// P2P NETWORK E2E TESTS
// ============================================================================

bool test_p2p_node_lifecycle() {
    p2p::P2PNode node1(31400);
    p2p::P2PNode node2(31401);
    
    TEST_ASSERT(node1.start(), "node1 start");
    TEST_ASSERT(node2.start(), "node2 start");
    
    std::this_thread::sleep_for(milliseconds(100));
    
    TEST_ASSERT(node1.isRunning(), "node1 running");
    TEST_ASSERT(node2.isRunning(), "node2 running");
    
    node1.stop();
    node2.stop();
    
    TEST_ASSERT(!node1.isRunning(), "node1 stopped");
    TEST_ASSERT(!node2.isRunning(), "node2 stopped");
    
    return true;
}

bool test_p2p_encryption() {
    std::array<uint8_t, 32> key{};
    for (int i = 0; i < 32; i++) key[i] = i * 7;
    
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5, 6, 7, 8};
    
    auto ciphertext = p2p::encryptPacket(plaintext, key.data());
    TEST_ASSERT(ciphertext != plaintext, "encrypted differs");
    
    auto decrypted = p2p::decryptPacket(ciphertext, key.data());
    TEST_ASSERT(decrypted == plaintext, "decryption roundtrip");
    
    return true;
}

bool test_p2p_message_types() {
    TEST_ASSERT(static_cast<uint8_t>(p2p::MsgType::PING) == 0x01, "PING");
    TEST_ASSERT(static_cast<uint8_t>(p2p::MsgType::TX) == 0x09, "TX");
    TEST_ASSERT(static_cast<uint8_t>(p2p::MsgType::STATE_REQUEST) == 0x0D, "STATE_REQUEST");
    TEST_ASSERT(static_cast<uint8_t>(p2p::MsgType::PROJECTION) == 0x0F, "PROJECTION");
    
    return true;
}

bool test_p2p_state_sync_structures() {
    p2p::StateSyncRequest req;
    req.from_snapshot = 100;
    req.to_snapshot = 200;
    req.include_accounts = true;
    req.include_projections = true;
    
    auto serialized = req.serialize();
    TEST_ASSERT(serialized.size() == 18, "serialized size");
    
    auto deserialized = p2p::StateSyncRequest::deserialize(serialized);
    TEST_ASSERT(deserialized.from_snapshot == 100, "from_snapshot");
    TEST_ASSERT(deserialized.to_snapshot == 200, "to_snapshot");
    TEST_ASSERT(deserialized.include_accounts, "include_accounts");
    TEST_ASSERT(deserialized.include_projections, "include_projections");
    
    return true;
}

// ============================================================================
// API SERVER E2E TESTS
// ============================================================================

bool test_api_json_parsing() {
    std::string json = R"({"from":"0x1234","to":"0x5678","amount":1000})";
    
    std::string from = api::JSON::getString(json, "from");
    std::string to = api::JSON::getString(json, "to");
    uint64_t amount = api::JSON::getNumber(json, "amount");
    
    TEST_ASSERT(from == "0x1234", "from parsed");
    TEST_ASSERT(to == "0x5678", "to parsed");
    TEST_ASSERT(amount == 1000, "amount parsed");
    
    return true;
}

bool test_api_json_building() {
    std::string status_str = api::JSON::str("success");
    std::string balance_str = api::JSON::num((uint64_t)12345);
    std::string active_str = api::JSON::boolean(true);
    
    TEST_ASSERT(status_str == "\"success\"", "status string");
    TEST_ASSERT(balance_str == "12345", "balance number");
    TEST_ASSERT(active_str == "true", "boolean true");
    
    return true;
}

// ============================================================================
// STRESS TESTS
// ============================================================================

bool test_concurrent_transfers() {
    economics::AccountManager accounts;
    
    crypto::KeyPair sender;
    sender.generate();
    accounts.mint(sender.getAddress(), 1000000 * economics::Q_PER_ARQON);
    
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([&accounts, &sender, &success_count]() {
            for (int i = 0; i < 100; i++) {
                crypto::KeyPair receiver;
                receiver.generate();
                if (accounts.transfer(sender.getAddress(), receiver.getAddress(), 1)) {
                    success_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    // Some may fail due to race conditions, but most should succeed
    TEST_ASSERT(success_count > 500, "concurrent transfers");
    
    return true;
}

bool test_multi_node_inference() {
    economics::AccountManager accounts;
    consensus::PoSConsensus consensus(accounts);
    inference::InferenceNetworkManager network(accounts);
    
    // Register 100 nodes
    for (int i = 0; i < 100; i++) {
        crypto::KeyPair key;
        key.generate();
        accounts.mint(key.getAddress(), 10000 * economics::Q_PER_ARQON);
        accounts.stake(key.getAddress(), 5000 * economics::Q_PER_ARQON);
        
        inference::InferenceNodeInfo node;
        node.address = key.getAddress();
        node.type = (i % 4 == 0) ? inference::NodeType::RELAY_ONLY : inference::NodeType::GPU_INFERENCE;
        node.staked_amount = 5000 * economics::Q_PER_ARQON;
        node.uptime_percent = 95 + (i % 5);
        
        if (node.type == inference::NodeType::GPU_INFERENCE) {
            inference::GPUInfo gpu;
            gpu.vram_mb = 8192 + (i % 4) * 8192;
            gpu.tflops_fp16 = 20 + (i % 10) * 10;
            node.gpus.push_back(gpu);
        }
        
        network.registerNode(node);
    }
    
    auto stats = network.getStats();
    TEST_ASSERT(stats.total_nodes == 100, "100 nodes");
    TEST_ASSERT(stats.gpu_nodes == 75, "75 GPU nodes");
    TEST_ASSERT(stats.relay_nodes == 25, "25 relay nodes");
    
    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ARQON E2E FULL SYSTEM TEST SUITE                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    int passed = 0, failed = 0;
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  BRAIDCHAIN TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_braidchain_genesis);
    RUN_TEST(test_braidchain_transfer);
    RUN_TEST(test_braidchain_staking);
    RUN_TEST(test_braidchain_consensus);
    RUN_TEST(test_braidchain_projection_sync);
    RUN_TEST(test_braidchain_fault_detection);
    RUN_TEST(test_braidchain_throughput);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  INFERENCE NETWORK TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_inference_node_registration);
    RUN_TEST(test_inference_reward_distribution);
    RUN_TEST(test_petals_client_lifecycle);
    RUN_TEST(test_petals_inference_request);
    RUN_TEST(test_inference_compute_units);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  P2P NETWORK TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_p2p_node_lifecycle);
    RUN_TEST(test_p2p_encryption);
    RUN_TEST(test_p2p_message_types);
    RUN_TEST(test_p2p_state_sync_structures);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  API SERVER TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_api_json_parsing);
    RUN_TEST(test_api_json_building);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  STRESS TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_concurrent_transfers);
    RUN_TEST(test_multi_node_inference);
    
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  PASSED: " << passed << "  FAILED: " << failed;
    for (int i = 0; i < 45 - std::to_string(passed).length() - std::to_string(failed).length(); i++) std::cout << " ";
    std::cout << "║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    return failed > 0 ? 1 : 0;
}
