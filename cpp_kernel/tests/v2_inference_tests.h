#pragma once
#include "../inference/InferenceNode.h"
#include "../inference/PetalsClient.h"

using namespace inference;

// Inference Tests 71-85
bool test_inf_71_network() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    TEST_ASSERT(network.getNodeCount() == 0, "Empty");
    return true;
}

bool test_inf_72_register() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    KeyPair op;
    accounts.mint(op.getAddress(), arqonToQ(10));
    accounts.stake(op.getAddress(), MIN_GPU_STAKE);
    InferenceNodeInfo node;
    node.address = op.getAddress();
    node.type = NodeType::GPU_INFERENCE;
    node.public_name = "Test";
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    TEST_ASSERT(network.registerNode(node), "Register");
    return true;
}

bool test_inf_73_stake_req() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    KeyPair op;
    InferenceNodeInfo node;
    node.address = op.getAddress();
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = 0;
    TEST_ASSERT(!network.registerNode(node), "NoStake");
    return true;
}

bool test_inf_74_compute_units() {
    ComputeMetrics m;
    m.tflops_fp16 = 80.0;
    m.vram_mb = 24000;
    m.tensor_cores = 500;
    TEST_ASSERT(m.computeUnits() > 80, "CU");
    return true;
}

bool test_inf_75_multiplier() {
    ComputeMetrics low, high;
    low.tflops_fp16 = 10.0;
    high.tflops_fp16 = 100.0;
    TEST_ASSERT(high.rewardMultiplier() > low.rewardMultiplier(), "Mult");
    return true;
}

bool test_inf_76_epoch() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    KeyPair op;
    accounts.mint(op.getAddress(), arqonToQ(10));
    accounts.stake(op.getAddress(), MIN_GPU_STAKE);
    InferenceNodeInfo node;
    node.address = op.getAddress();
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    node.tokens_generated = 1000;
    network.registerNode(node);
    uint64_t before = accounts.getAccount(op.getAddress()).balance;
    network.processEpoch();
    TEST_ASSERT(accounts.getAccount(op.getAddress()).balance > before, "Epoch");
    return true;
}

bool test_inf_77_uptime() {
    InferenceNodeInfo node;
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 90.0;
    auto r = InferenceRewardsCalculator::calculate(node, 1);
    TEST_ASSERT(r.total_reward == 0, "LowUptime");
    return true;
}

bool test_inf_78_relay_gpu() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    KeyPair r_op, g_op;
    accounts.mint(r_op.getAddress(), arqonToQ(5));
    accounts.stake(r_op.getAddress(), MIN_NODE_STAKE);
    InferenceNodeInfo relay;
    relay.address = r_op.getAddress();
    relay.type = NodeType::RELAY_ONLY;
    relay.staked_amount = MIN_NODE_STAKE;
    relay.uptime_percent = 99.0;
    network.registerNode(relay);
    accounts.mint(g_op.getAddress(), arqonToQ(10));
    accounts.stake(g_op.getAddress(), MIN_GPU_STAKE);
    InferenceNodeInfo gpu;
    gpu.address = g_op.getAddress();
    gpu.type = NodeType::GPU_INFERENCE;
    gpu.staked_amount = MIN_GPU_STAKE;
    gpu.uptime_percent = 99.0;
    network.registerNode(gpu);
    auto stats = network.getStats();
    TEST_ASSERT(stats.relay_nodes == 1 && stats.gpu_nodes == 1, "Types");
    return true;
}

bool test_inf_79_multi_gpu() {
    InferenceNodeInfo node;
    node.type = NodeType::GPU_INFERENCE;
    GPUInfo g1, g2;
    g1.tflops_fp16 = 80.0; g1.vram_mb = 24000;
    g2.tflops_fp16 = 80.0; g2.vram_mb = 24000;
    node.gpus.push_back(g1);
    node.gpus.push_back(g2);
    auto c = node.getTotalCompute();
    TEST_ASSERT(c.tflops_fp16 == 160.0 && c.vram_mb == 48000, "MultiGPU");
    return true;
}

bool test_inf_80_client() {
    PetalsClient client;
    TEST_ASSERT(!client.isRunning(), "Init");
    client.start();
    TEST_ASSERT(client.isRunning(), "Start");
    client.stop();
    TEST_ASSERT(!client.isRunning(), "Stop");
    return true;
}

bool test_inf_81_session() {
    PetalsClient client;
    client.start();
    uint64_t id = client.createSession("model");
    TEST_ASSERT(id > 0, "Session");
    client.closeSession(id);
    auto* s = client.getSession(id);
    TEST_ASSERT(!s->active, "Closed");
    client.stop();
    return true;
}

bool test_inf_82_stats() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    auto stats = network.getStats();
    TEST_ASSERT(stats.total_nodes == 0 && stats.current_epoch == 0, "Stats");
    return true;
}

bool test_inf_83_reward_calc() {
    InferenceNodeInfo node;
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    node.tokens_generated = 10000;
    auto r = InferenceRewardsCalculator::calculate(node, 1);
    TEST_ASSERT(r.base_reward == BASE_NODE_REWARD && r.total_reward > r.base_reward, "Calc");
    return true;
}

bool test_inf_84_diminishing() {
    ComputeMetrics small, large;
    small.tflops_fp16 = 10.0;
    large.tflops_fp16 = 1000.0;
    TEST_ASSERT(large.rewardMultiplier() < small.rewardMultiplier() * 10, "Diminish");
    return true;
}

bool test_inf_85_heartbeat() {
    AccountManager accounts;
    InferenceNetworkManager network(accounts);
    KeyPair op;
    accounts.mint(op.getAddress(), arqonToQ(10));
    accounts.stake(op.getAddress(), MIN_GPU_STAKE);
    InferenceNodeInfo node;
    node.address = op.getAddress();
    node.type = NodeType::GPU_INFERENCE;
    node.staked_amount = MIN_GPU_STAKE;
    node.uptime_percent = 99.0;
    network.registerNode(node);
    TEST_ASSERT(network.heartbeat(op.getAddress(), NodeStatus::SERVING, 5.0), "HB");
    return true;
}
