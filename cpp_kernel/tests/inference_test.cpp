/**
 * RSE Inference Network Tests
 * 
 * Tests for the Petals-integrated inference network with Arqon/Q rewards.
 */

#include "../inference/InferenceNode.h"
#include "../inference/PetalsClient.h"
#include "../core/Economics.h"
#include "../core/Crypto.h"

#include <iostream>
#include <cassert>
#include <memory>
#include <thread>
#include <chrono>

using namespace inference;
using namespace economics;
using namespace crypto;

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << std::endl; return false; } } while(0)
#define RUN_TEST(fn) do { std::cout << "  " << #fn << "... " << std::flush; if (fn()) { std::cout << "PASS" << std::endl; passed++; } else { std::cout << "FAIL" << std::endl; failed++; } } while(0)

// ============================================================================
// Currency Tests
// ============================================================================

bool test_currency_conversion() {
    using economics::Q_PER_ARQON;
    
    // Test Q to Arqon conversion
    TEST_ASSERT(qToArqon(Q_PER_ARQON) == 1.0, "1 Arqon = 10^9 Q");
    TEST_ASSERT(qToArqon(Q_PER_ARQON * 5) == 5.0, "5 Arqon conversion");
    TEST_ASSERT(qToArqon(500000000) == 0.5, "0.5 Arqon conversion");
    
    // Test Arqon to Q conversion
    TEST_ASSERT(arqonToQ(1.0) == Q_PER_ARQON, "1 Arqon to Q");
    TEST_ASSERT(arqonToQ(0.1) == 100000000, "0.1 Arqon to Q");
    TEST_ASSERT(arqonToQ(2.5) == 2500000000, "2.5 Arqon to Q");
    
    return true;
}

// ============================================================================
// Node Registration Tests
// ============================================================================

bool test_node_registration() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    
    KeyPair operator_key;
    
    // Mint and stake enough for GPU node
    accounts.mint(operator_key.getAddress(), arqonToQ(10));  // 10 Arqon
    accounts.stake(operator_key.getAddress(), MIN_GPU_STAKE);  // 5 Arqon
    
    // Create node info
    InferenceNodeInfo node;
    node.address = operator_key.getAddress();
    node.type = NodeType::GPU_INFERENCE;
    node.public_name = "Test GPU Node";
    node.endpoint = "http://localhost";
    node.port = 31330;
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    
    // Add GPU info
    GPUInfo gpu;
    gpu.name = "NVIDIA RTX 4090";
    gpu.vram_mb = 24000;
    gpu.tflops_fp16 = 82.6;
    node.gpus.push_back(gpu);
    
    // Register node
    TEST_ASSERT(network.registerNode(node), "Node should register");
    TEST_ASSERT(network.getNodeCount() == 1, "Should have 1 node");
    
    // Verify node info
    auto* registered = network.getNode(operator_key.getAddress());
    TEST_ASSERT(registered != nullptr, "Should find registered node");
    TEST_ASSERT(registered->public_name == "Test GPU Node", "Name should match");
    TEST_ASSERT(registered->isGPUNode(), "Should be GPU node");
    
    return true;
}

bool test_node_stake_requirement() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    
    KeyPair operator_key;
    
    // Try to register without stake
    InferenceNodeInfo node;
    node.address = operator_key.getAddress();
    node.type = NodeType::GPU_INFERENCE;
    node.public_name = "Unstaked Node";
    node.staked_amount = 0;
    
    TEST_ASSERT(!network.registerNode(node), "Should reject unstaked node");
    
    // Mint but don't stake
    accounts.mint(operator_key.getAddress(), arqonToQ(10));
    node.staked_amount = MIN_GPU_STAKE;  // Claim stake but don't actually stake
    
    TEST_ASSERT(!network.registerNode(node), "Should reject node without locked stake");
    
    // Actually stake
    accounts.stake(operator_key.getAddress(), MIN_GPU_STAKE);
    TEST_ASSERT(network.registerNode(node), "Should accept properly staked node");
    
    return true;
}

bool test_relay_node_registration() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    
    KeyPair operator_key;
    
    // Relay nodes need less stake
    accounts.mint(operator_key.getAddress(), arqonToQ(2));
    accounts.stake(operator_key.getAddress(), MIN_NODE_STAKE);  // 1 Arqon
    
    InferenceNodeInfo node;
    node.address = operator_key.getAddress();
    node.type = NodeType::RELAY_ONLY;
    node.public_name = "Relay Node";
    node.staked_amount = MIN_NODE_STAKE;
    node.uptime_percent = 98.0;
    
    TEST_ASSERT(network.registerNode(node), "Relay node should register with 1 Arqon stake");
    
    auto* registered = network.getNode(operator_key.getAddress());
    TEST_ASSERT(!registered->isGPUNode(), "Should not be GPU node");
    
    return true;
}

// ============================================================================
// Rewards Tests
// ============================================================================

bool test_reward_calculation() {
    InferenceNodeInfo node;
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    node.requests_relayed = 100;
    node.tokens_generated = 10000;
    node.requests_served = 50;
    node.average_tps = 8.0;
    
    GPUInfo gpu;
    gpu.tflops_fp16 = 80.0;  // High-end GPU
    gpu.vram_mb = 24000;
    gpu.tensor_cores = 500;
    node.gpus.push_back(gpu);
    
    auto rewards = InferenceRewardsCalculator::calculate(node, 1);
    
    // Base reward
    TEST_ASSERT(rewards.base_reward == BASE_NODE_REWARD, "Base reward should be 0.1 Arqon");
    
    // Relay reward: 100 requests * 1000 Q
    TEST_ASSERT(rewards.relay_reward == 100 * RELAY_REWARD_PER_REQUEST, "Relay reward");
    
    // Inference reward: tokens * rate * compute multiplier (computed dynamically)
    double multiplier = node.getGPUMultiplier();
    uint64_t expected_inference = static_cast<uint64_t>(10000 * INFERENCE_REWARD_PER_TOKEN * multiplier);
    TEST_ASSERT(rewards.inference_reward == expected_inference, "Inference reward with compute multiplier");
    TEST_ASSERT(multiplier > 1.5, "High-compute GPU should get >1.5x multiplier");
    
    // GPU bonus
    TEST_ASSERT(rewards.gpu_bonus == GPU_COMPUTE_BONUS, "GPU bonus should be 0.5 Arqon");
    
    // Performance bonus (8 TPS > 5, so 50% bonus)
    TEST_ASSERT(rewards.performance_bonus == GPU_COMPUTE_BONUS / 2, "Performance bonus for 8 TPS");
    
    // Total should be sum of all
    uint64_t expected_total = rewards.base_reward + rewards.relay_reward + 
                              rewards.inference_reward + rewards.gpu_bonus + 
                              rewards.performance_bonus;
    TEST_ASSERT(rewards.total_reward == expected_total, "Total reward calculation");
    
    return true;
}

bool test_uptime_requirement() {
    InferenceNodeInfo node;
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = MIN_GPU_STAKE;
    node.tokens_generated = 10000;
    
    // Below 95% uptime
    node.uptime_percent = 90.0;
    auto rewards = InferenceRewardsCalculator::calculate(node, 1);
    TEST_ASSERT(rewards.total_reward == 0, "Should get no rewards below 95% uptime");
    
    // At 95% uptime
    node.uptime_percent = 95.0;
    rewards = InferenceRewardsCalculator::calculate(node, 1);
    TEST_ASSERT(rewards.total_reward > 0, "Should get rewards at 95% uptime");
    
    return true;
}

bool test_epoch_distribution() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    
    // Register two nodes
    KeyPair gpu_operator, relay_operator;
    
    // GPU node
    accounts.mint(gpu_operator.getAddress(), arqonToQ(10));
    accounts.stake(gpu_operator.getAddress(), MIN_GPU_STAKE);
    
    InferenceNodeInfo gpu_node;
    gpu_node.address = gpu_operator.getAddress();
    gpu_node.type = NodeType::GPU_INFERENCE;
    gpu_node.public_name = "GPU-1";
    gpu_node.staked_amount = MIN_GPU_STAKE;
    gpu_node.uptime_percent = 99.0;
    gpu_node.tokens_generated = 5000;
    network.registerNode(gpu_node);
    
    // Relay node
    accounts.mint(relay_operator.getAddress(), arqonToQ(2));
    accounts.stake(relay_operator.getAddress(), MIN_NODE_STAKE);
    
    InferenceNodeInfo relay_node;
    relay_node.address = relay_operator.getAddress();
    relay_node.type = NodeType::RELAY_ONLY;
    relay_node.public_name = "Relay-1";
    relay_node.staked_amount = MIN_NODE_STAKE;
    relay_node.uptime_percent = 99.0;
    relay_node.requests_relayed = 50;
    network.registerNode(relay_node);
    
    // Get balances before
    uint64_t gpu_balance_before = accounts.getAccount(gpu_operator.getAddress()).balance;
    uint64_t relay_balance_before = accounts.getAccount(relay_operator.getAddress()).balance;
    
    // Process epoch
    network.processEpoch();
    
    // Check balances increased
    uint64_t gpu_balance_after = accounts.getAccount(gpu_operator.getAddress()).balance;
    uint64_t relay_balance_after = accounts.getAccount(relay_operator.getAddress()).balance;
    
    TEST_ASSERT(gpu_balance_after > gpu_balance_before, "GPU node should receive rewards");
    TEST_ASSERT(relay_balance_after > relay_balance_before, "Relay node should receive rewards");
    TEST_ASSERT(gpu_balance_after - gpu_balance_before > relay_balance_after - relay_balance_before,
                "GPU node should receive more rewards than relay");
    
    return true;
}

// ============================================================================
// Compute Units Tests
// ============================================================================

bool test_compute_units() {
    // Test compute metrics calculation
    ComputeMetrics m;
    m.tflops_fp16 = 80.0;        // 80 CU from TFLOPS
    m.tensor_cores = 500;         // 5 CU bonus
    m.vram_mb = 24000;            // 3 CU from VRAM
    m.memory_bandwidth_gbps = 1000; // 2 CU from bandwidth
    m.cpu_cores = 8;
    m.cpu_ghz = 4.0;              // 3.2 CU from CPU
    m.network_bandwidth_mbps = 10000; // 1 CU from network
    
    double cu = m.computeUnits();
    // Total should be ~94 CU
    TEST_ASSERT(cu > 90 && cu < 100, "Compute units calculation");
    
    // Multiplier should be > 2.0 (logarithmic above 50 CU)
    double mult = m.rewardMultiplier();
    TEST_ASSERT(mult > 2.0, "High compute should get > 2x multiplier");
    
    return true;
}

bool test_compute_scaling() {
    // Entry level: ~10 CU
    ComputeMetrics entry;
    entry.tflops_fp16 = 10.0;
    entry.vram_mb = 8000;
    double entry_mult = entry.rewardMultiplier();
    TEST_ASSERT(entry_mult >= 1.0 && entry_mult < 1.5, "Entry level ~1.2x");
    
    // Mid level: ~25 CU
    ComputeMetrics mid;
    mid.tflops_fp16 = 35.0;
    mid.vram_mb = 12000;
    mid.tensor_cores = 200;
    double mid_mult = mid.rewardMultiplier();
    TEST_ASSERT(mid_mult >= 1.5 && mid_mult < 2.0, "Mid level ~1.7x");
    
    // High level: ~88 CU (80 TFLOPS + 3 VRAM + 5 tensor)
    ComputeMetrics high;
    high.tflops_fp16 = 80.0;
    high.vram_mb = 24000;
    high.tensor_cores = 500;
    double high_mult = high.rewardMultiplier();
    // Above 50 CU uses logarithmic scaling: 2.0 + log2(88/50) ≈ 2.8
    TEST_ASSERT(high_mult > 2.5 && high_mult < 3.5, "High level ~2.8x (logarithmic)");
    
    // Data center: ~150 CU
    ComputeMetrics datacenter;
    datacenter.tflops_fp16 = 200.0;
    datacenter.vram_mb = 80000;
    datacenter.tensor_cores = 1000;
    datacenter.memory_bandwidth_gbps = 3000;
    double dc_mult = datacenter.rewardMultiplier();
    TEST_ASSERT(dc_mult > 2.5, "Data center should get 2.5x+ (diminishing returns)");
    
    // Verify diminishing returns
    TEST_ASSERT(dc_mult < entry_mult * 4, "Diminishing returns should limit max multiplier");
    
    return true;
}

bool test_multi_gpu_aggregation() {
    InferenceNodeInfo node;
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    
    // Add two GPUs
    GPUInfo gpu1;
    gpu1.name = "RTX 4090 #1";
    gpu1.tflops_fp16 = 82.0;
    gpu1.vram_mb = 24000;
    gpu1.tensor_cores = 512;
    node.gpus.push_back(gpu1);
    
    GPUInfo gpu2;
    gpu2.name = "RTX 4090 #2";
    gpu2.tflops_fp16 = 82.0;
    gpu2.vram_mb = 24000;
    gpu2.tensor_cores = 512;
    node.gpus.push_back(gpu2);
    
    // Aggregate compute
    auto total = node.getTotalCompute();
    TEST_ASSERT(total.tflops_fp16 == 164.0, "Should sum TFLOPS");
    TEST_ASSERT(total.vram_mb == 48000, "Should sum VRAM");
    TEST_ASSERT(total.tensor_cores == 1024, "Should sum tensor cores");
    
    // Compute units should be higher than single GPU
    double cu = node.getComputeUnits();
    TEST_ASSERT(cu > 150, "Dual GPU should have > 150 CU");
    
    return true;
}

// ============================================================================
// Petals Client Tests
// ============================================================================

bool test_petals_client_lifecycle() {
    PetalsClient client;
    
    TEST_ASSERT(!client.isRunning(), "Should not be running initially");
    
    client.start();
    TEST_ASSERT(client.isRunning(), "Should be running after start");
    
    client.stop();
    TEST_ASSERT(!client.isRunning(), "Should not be running after stop");
    
    return true;
}

bool test_petals_session_management() {
    PetalsClient client;
    client.start();
    
    uint64_t session_id = client.createSession("meta-llama/Meta-Llama-3.1-70B-Instruct");
    TEST_ASSERT(session_id > 0, "Should create session with valid ID");
    
    const InferenceSession* session = client.getSession(session_id);
    TEST_ASSERT(session != nullptr, "Should find session");
    TEST_ASSERT(session->active, "Session should be active");
    TEST_ASSERT(session->model_id == "meta-llama/Meta-Llama-3.1-70B-Instruct", "Model ID should match");
    
    client.closeSession(session_id);
    session = client.getSession(session_id);
    TEST_ASSERT(session != nullptr && !session->active, "Session should be inactive after close");
    
    client.stop();
    return true;
}

bool test_petals_inference() {
    PetalsClient client;
    client.start();
    
    uint64_t session_id = client.createSession("meta-llama/Meta-Llama-3.1-70B-Instruct");
    
    // Synchronous generation
    std::string response = client.generateSync(session_id, "Hello", 20);
    
    // Response should not be empty - either real inference or fallback message
    TEST_ASSERT(!response.empty(), "Should generate response");
    
    const InferenceSession* session = client.getSession(session_id);
    
    // Check if bridge is running (real inference) or fallback
    if (response.find("bridge not running") != std::string::npos) {
        // Fallback mode - bridge not available
        std::cout << "(bridge offline, fallback OK) ";
        TEST_ASSERT(session->tokens_generated == 0, "Fallback should have 0 tokens");
    } else {
        // Real inference from Petals network
        std::cout << "(bridge connected!) ";
        TEST_ASSERT(session->tokens_generated > 0, "Real inference should have tokens");
    }
    
    client.stop();
    return true;
}

bool test_petals_swarm_status() {
    PetalsClient client;
    
    auto status = client.getSwarmStatus("meta-llama/Meta-Llama-3.1-405B-Instruct");
    
    TEST_ASSERT(status.model_id == "meta-llama/Meta-Llama-3.1-405B-Instruct", "Model ID should match");
    TEST_ASSERT(status.total_blocks > 0, "Should have blocks");
    TEST_ASSERT(status.coveragePercent() > 0, "Should have coverage");
    
    return true;
}

// ============================================================================
// Network Stats Tests
// ============================================================================

bool test_network_stats() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    
    // Register a node
    KeyPair op;
    accounts.mint(op.getAddress(), arqonToQ(10));
    accounts.stake(op.getAddress(), MIN_GPU_STAKE);
    
    InferenceNodeInfo node;
    node.address = op.getAddress();
    node.type = NodeType::GPU_INFERENCE;
    node.public_name = "Stats Test Node";
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    
    GPUInfo gpu;
    gpu.vram_mb = 24000;
    node.gpus.push_back(gpu);
    
    network.registerNode(node);
    
    auto stats = network.getStats();
    
    TEST_ASSERT(stats.total_nodes == 1, "Should have 1 node");
    TEST_ASSERT(stats.gpu_nodes == 1, "Should have 1 GPU node");
    TEST_ASSERT(stats.relay_nodes == 0, "Should have 0 relay nodes");
    TEST_ASSERT(stats.total_vram_mb == 24000, "VRAM should be 24GB");
    TEST_ASSERT(stats.total_staked_q == MIN_GPU_STAKE, "Staked amount should match");
    
    return true;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n========================================\n";
    std::cout << "   RSE INFERENCE NETWORK TESTS\n";
    std::cout << "========================================\n\n";
    
    int passed = 0, failed = 0;
    
    std::cout << "[Currency]" << std::endl;
    RUN_TEST(test_currency_conversion);
    
    std::cout << "\n[Node Registration]" << std::endl;
    RUN_TEST(test_node_registration);
    RUN_TEST(test_node_stake_requirement);
    RUN_TEST(test_relay_node_registration);
    
    std::cout << "\n[Rewards]" << std::endl;
    RUN_TEST(test_reward_calculation);
    RUN_TEST(test_uptime_requirement);
    RUN_TEST(test_epoch_distribution);
    
    std::cout << "\n[Compute Units]" << std::endl;
    RUN_TEST(test_compute_units);
    RUN_TEST(test_compute_scaling);
    RUN_TEST(test_multi_gpu_aggregation);
    
    std::cout << "\n[Petals Client]" << std::endl;
    RUN_TEST(test_petals_client_lifecycle);
    RUN_TEST(test_petals_session_management);
    RUN_TEST(test_petals_inference);
    RUN_TEST(test_petals_swarm_status);
    
    std::cout << "\n[Network Stats]" << std::endl;
    RUN_TEST(test_network_stats);
    
    std::cout << "\n========================================\n";
    std::cout << "  PASSED: " << passed << "  FAILED: " << failed << "\n";
    std::cout << "========================================\n\n";
    
    return failed > 0 ? 1 : 0;
}
