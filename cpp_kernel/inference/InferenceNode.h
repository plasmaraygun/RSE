#pragma once

/**
 * RSE Inference Network Integration
 * 
 * Integrates with Petals-style distributed LLM inference.
 * Provides tiered incentive structure:
 *   - Tier 1: Node operators (relay, routing, validation)
 *   - Tier 2: Node + GPU operators (inference compute)
 * 
 * Currency: Arqon (ARQN), base unit: Q
 *   1 Arqon = 1,000,000,000 Q (10^9 Q)
 * 
 * Petals integration: https://github.com/bigscience-workshop/petals
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <cmath>

namespace inference {

// ============================================================================
// Currency Helpers (uses Q_PER_ARQON from Economics.h)
// ============================================================================

inline uint64_t arqonToQ(double arqon) {
    return static_cast<uint64_t>(arqon * economics::Q_PER_ARQON);
}

inline double qToArqon(uint64_t q) {
    return static_cast<double>(q) / economics::Q_PER_ARQON;
}

// ============================================================================
// Reward Constants (in Q)
// ============================================================================

// Epoch rewards
constexpr uint64_t BASE_NODE_REWARD = 100000000;        // 0.1 Arqon/epoch for running node
constexpr uint64_t GPU_COMPUTE_BONUS = 500000000;       // 0.5 Arqon/epoch bonus for GPU nodes

// Per-action rewards
constexpr uint64_t RELAY_REWARD_PER_REQUEST = 1000;     // 1 kQ per request relayed
constexpr uint64_t INFERENCE_REWARD_PER_TOKEN = 10000;  // 10 kQ per token generated

// Staking requirements
constexpr uint64_t MIN_NODE_STAKE = 1000000000;         // 1 Arqon to run relay node
constexpr uint64_t MIN_GPU_STAKE = 5000000000;          // 5 Arqon to run GPU node

// Performance thresholds
constexpr double MIN_UPTIME_PERCENT = 95.0;             // Required uptime for rewards
constexpr uint64_t MIN_TOKENS_PER_SECOND = 1;           // Minimum inference speed
constexpr uint64_t EPOCH_DURATION_SECONDS = 3600;       // 1 hour epochs

// ============================================================================
// Node Types
// ============================================================================

enum class NodeType {
    RELAY_ONLY,      // Routes requests, validates, no GPU compute
    GPU_INFERENCE,   // Provides GPU compute for inference
    FULL_NODE        // Both relay and inference capabilities
};

enum class NodeStatus {
    OFFLINE,
    SYNCING,
    ONLINE,
    SERVING,         // Actively serving inference requests
    MAINTENANCE
};

// ============================================================================
// Compute Metrics - Measured contribution basis for rewards
// ============================================================================

struct ComputeMetrics {
    // Raw compute capacity
    uint64_t cuda_cores = 0;           // CUDA/shader cores
    uint64_t tensor_cores = 0;         // Tensor cores (if available)
    double tflops_fp16 = 0;            // FP16 TFLOPS
    double tflops_fp32 = 0;            // FP32 TFLOPS
    
    // Memory
    uint64_t vram_mb = 0;              // VRAM in MB
    uint64_t memory_bandwidth_gbps = 0; // Memory bandwidth GB/s
    
    // CPU contribution (for hybrid nodes)
    uint64_t cpu_cores = 0;            // Physical CPU cores
    uint64_t cpu_threads = 0;          // Logical threads
    double cpu_ghz = 0;                // Base frequency
    
    // Network
    uint64_t network_bandwidth_mbps = 0; // Network bandwidth Mbps
    
    // Calculate total compute units (normalized score)
    // 1 CU = roughly 1 TFLOP of FP16 capacity
    double computeUnits() const {
        double gpu_cu = tflops_fp16;                           // 1 CU per TFLOP FP16
        double tensor_bonus = tensor_cores * 0.01;             // Bonus for tensor cores
        double vram_cu = vram_mb / 8000.0;                     // 1 CU per 8GB VRAM
        double bw_cu = memory_bandwidth_gbps / 500.0;          // 1 CU per 500 GB/s bandwidth
        double cpu_cu = (cpu_cores * cpu_ghz) / 10.0;          // CPU contribution
        double net_cu = network_bandwidth_mbps / 10000.0;      // Network contribution
        
        return gpu_cu + tensor_bonus + vram_cu + bw_cu + cpu_cu + net_cu;
    }
    
    // Reward multiplier based on compute units
    double rewardMultiplier() const {
        double cu = computeUnits();
        // Linear scaling with diminishing returns above 50 CU
        if (cu <= 50) return 1.0 + (cu / 50.0);               // 1.0x to 2.0x
        return 2.0 + std::log2(cu / 50.0);                    // Logarithmic above
    }
};

struct GPUInfo {
    std::string name;              // e.g., "NVIDIA RTX 4090"
    uint64_t vram_mb;              // VRAM in MB
    uint64_t compute_capability;   // CUDA compute capability * 10 (e.g., 89 for 8.9)
    uint64_t tensor_cores;         // Number of tensor cores
    double tflops_fp16;            // FP16 performance in TFLOPS
    
    // Convert to compute metrics
    ComputeMetrics toComputeMetrics() const {
        ComputeMetrics m;
        m.vram_mb = vram_mb;
        m.tensor_cores = tensor_cores;
        m.tflops_fp16 = tflops_fp16;
        // Estimate other values from known specs
        m.cuda_cores = static_cast<uint64_t>(tflops_fp16 * 100);  // Rough estimate
        m.memory_bandwidth_gbps = vram_mb / 30;  // Rough estimate
        return m;
    }
    
    // Estimated tokens per second for different model sizes
    double estimatedTPS_7B() const { return tflops_fp16 * 2.0; }
    double estimatedTPS_70B() const { return tflops_fp16 * 0.3; }
    double estimatedTPS_405B() const { return tflops_fp16 * 0.05; }
    
    // Calculate reward multiplier based on compute units (not tiers)
    double rewardMultiplier() const {
        return toComputeMetrics().rewardMultiplier();
    }
};

// ============================================================================
// Inference Node Registration
// ============================================================================

struct InferenceNodeInfo {
    crypto::Address address;       // Node operator address
    NodeType type;
    NodeStatus status;
    
    // Network info
    std::string endpoint;          // HTTP/gRPC endpoint
    uint16_t port;
    std::string public_name;       // Optional display name (shown on health monitor)
    std::string region;            // Geographic region
    
    // Capabilities
    std::vector<GPUInfo> gpus;
    std::vector<std::string> supported_models;  // Model IDs this node can serve
    std::vector<uint32_t> served_layers;        // Which layers of the model (Petals-style)
    uint64_t max_batch_size;
    
    // Performance metrics (current epoch)
    uint64_t tokens_generated;
    uint64_t requests_served;
    uint64_t requests_relayed;
    double current_tps;
    double average_tps;
    double uptime_percent;
    
    // Lifetime stats
    uint64_t lifetime_tokens;
    uint64_t lifetime_requests;
    uint64_t lifetime_rewards_q;
    
    // Staking
    uint64_t staked_amount;
    
    // Timestamps
    uint64_t registered_at;
    uint64_t last_heartbeat;
    uint64_t last_inference;
    
    // Helper methods
    bool isGPUNode() const {
        return type == NodeType::GPU_INFERENCE || type == NodeType::FULL_NODE;
    }
    
    bool meetsUptimeRequirement() const {
        return uptime_percent >= MIN_UPTIME_PERCENT;
    }
    
    uint64_t getTotalVRAM() const {
        uint64_t total = 0;
        for (const auto& gpu : gpus) total += gpu.vram_mb;
        return total;
    }
    
    // Aggregate all compute resources
    ComputeMetrics getTotalCompute() const {
        ComputeMetrics total;
        for (const auto& gpu : gpus) {
            auto m = gpu.toComputeMetrics();
            total.cuda_cores += m.cuda_cores;
            total.tensor_cores += m.tensor_cores;
            total.tflops_fp16 += m.tflops_fp16;
            total.tflops_fp32 += m.tflops_fp32;
            total.vram_mb += m.vram_mb;
            total.memory_bandwidth_gbps += m.memory_bandwidth_gbps;
        }
        return total;
    }
    
    double getComputeUnits() const {
        return getTotalCompute().computeUnits();
    }
    
    double getGPUMultiplier() const {
        if (gpus.empty()) return 1.0;
        return getTotalCompute().rewardMultiplier();
    }
    
    bool meetsStakeRequirement() const {
        uint64_t required = isGPUNode() ? MIN_GPU_STAKE : MIN_NODE_STAKE;
        return staked_amount >= required;
    }
};

// ============================================================================
// Inference Request
// ============================================================================

struct InferenceRequest {
    uint64_t request_id;
    crypto::Address requester;
    std::string model_id;
    std::string prompt;
    uint32_t max_tokens;
    double temperature;
    double top_p;
    
    // Payment
    uint64_t max_cost_q;           // Maximum Q willing to pay
    uint64_t actual_cost_q;        // Actual cost charged
    
    // Routing
    std::vector<crypto::Address> route;  // Nodes in the inference path
    crypto::Address serving_node;        // Node that did the compute
    
    // Timing
    uint64_t submitted_at;
    uint64_t started_at;
    uint64_t completed_at;
    uint64_t latency_ms;
    
    // Results
    std::string response;
    uint32_t tokens_generated;
    double tokens_per_second;
    bool success;
    std::string error;
};

// ============================================================================
// Epoch Rewards
// ============================================================================

struct EpochRewards {
    uint64_t epoch_number;
    crypto::Address node_address;
    
    // Reward breakdown (all in Q)
    uint64_t base_reward;          // For running node
    uint64_t relay_reward;         // For relaying requests
    uint64_t inference_reward;     // For GPU inference
    uint64_t performance_bonus;    // High TPS bonus
    uint64_t gpu_bonus;            // For providing GPU
    uint64_t total_reward;
    
    // Stats that earned these rewards
    uint64_t requests_relayed;
    uint64_t tokens_generated;
    uint64_t requests_served;
    double avg_tps;
    double uptime;
    double gpu_multiplier;
};

// ============================================================================
// Rewards Calculator
// ============================================================================

class InferenceRewardsCalculator {
public:
    static EpochRewards calculate(const InferenceNodeInfo& node, uint64_t epoch) {
        EpochRewards rewards{};
        rewards.epoch_number = epoch;
        rewards.node_address = node.address;
        rewards.uptime = node.uptime_percent;
        rewards.gpu_multiplier = node.getGPUMultiplier();
        
        // Must meet uptime and stake requirements
        if (!node.meetsUptimeRequirement() || !node.meetsStakeRequirement()) {
            return rewards;
        }
        
        // Base reward for being online
        rewards.base_reward = BASE_NODE_REWARD;
        
        // Relay rewards
        rewards.requests_relayed = node.requests_relayed;
        rewards.relay_reward = node.requests_relayed * RELAY_REWARD_PER_REQUEST;
        
        // GPU inference rewards
        if (node.isGPUNode()) {
            rewards.tokens_generated = node.tokens_generated;
            rewards.requests_served = node.requests_served;
            rewards.avg_tps = node.average_tps;
            
            // Base inference reward
            rewards.inference_reward = node.tokens_generated * INFERENCE_REWARD_PER_TOKEN;
            
            // GPU contribution bonus
            rewards.gpu_bonus = GPU_COMPUTE_BONUS;
            
            // Performance bonus for high TPS
            if (node.average_tps > 10.0) {
                rewards.performance_bonus = GPU_COMPUTE_BONUS;  // 100% bonus
            } else if (node.average_tps > 5.0) {
                rewards.performance_bonus = GPU_COMPUTE_BONUS / 2;  // 50% bonus
            } else if (node.average_tps > 2.0) {
                rewards.performance_bonus = GPU_COMPUTE_BONUS / 4;  // 25% bonus
            }
            
            // Apply GPU multiplier to inference rewards
            rewards.inference_reward = static_cast<uint64_t>(
                rewards.inference_reward * rewards.gpu_multiplier
            );
        }
        
        rewards.total_reward = rewards.base_reward + rewards.relay_reward + 
                               rewards.inference_reward + rewards.performance_bonus +
                               rewards.gpu_bonus;
        
        return rewards;
    }
    
    static void printRewards(const EpochRewards& r) {
        std::cout << "\n[Inference Rewards] Epoch " << r.epoch_number << std::endl;
        std::cout << "  Base:        " << r.base_reward << " Q (" << qToArqon(r.base_reward) << " ARQN)" << std::endl;
        std::cout << "  Relay:       " << r.relay_reward << " Q (" << r.requests_relayed << " requests)" << std::endl;
        std::cout << "  Inference:   " << r.inference_reward << " Q (" << r.tokens_generated << " tokens)" << std::endl;
        std::cout << "  GPU Bonus:   " << r.gpu_bonus << " Q" << std::endl;
        std::cout << "  Perf Bonus:  " << r.performance_bonus << " Q (avg " << r.avg_tps << " TPS)" << std::endl;
        std::cout << "  TOTAL:       " << r.total_reward << " Q (" << qToArqon(r.total_reward) << " ARQN)" << std::endl;
    }
};

// ============================================================================
// Inference Network Manager
// ============================================================================

class InferenceNetworkManager {
private:
    std::unordered_map<std::string, InferenceNodeInfo> nodes_;
    std::vector<InferenceRequest> pending_requests_;
    std::vector<InferenceRequest> completed_requests_;
    
    economics::AccountManager* accounts_;
    
    mutable std::mutex mutex_;
    std::atomic<uint64_t> request_counter_{0};
    std::atomic<uint64_t> current_epoch_{0};
    
    std::string addressKey(const crypto::Address& addr) const {
        return std::string(reinterpret_cast<const char*>(addr.data()), crypto::ADDRESS_SIZE);
    }
    
    uint64_t now() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    
public:
    explicit InferenceNetworkManager(economics::AccountManager& accounts)
        : accounts_(&accounts) {}
    
    // ========================================================================
    // Node Registration
    // ========================================================================
    
    bool registerNode(const InferenceNodeInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Verify stake requirement
        if (!info.meetsStakeRequirement()) {
            std::cerr << "[Inference] Registration failed: insufficient stake" << std::endl;
            return false;
        }
        
        // Verify stake is actually locked in account
        const auto& account = accounts_->getAccount(info.address);
        uint64_t required = info.isGPUNode() ? MIN_GPU_STAKE : MIN_NODE_STAKE;
        if (account.stake < required) {
            std::cerr << "[Inference] Registration failed: stake not locked" << std::endl;
            return false;
        }
        
        auto node = info;
        node.registered_at = now();
        node.last_heartbeat = now();
        node.status = NodeStatus::ONLINE;
        
        nodes_[addressKey(info.address)] = node;
        
        std::cout << "[Inference] Node registered: " << info.public_name 
                  << " (" << (info.isGPUNode() ? "GPU" : "Relay") << ")" << std::endl;
        
        return true;
    }
    
    bool unregisterNode(const crypto::Address& address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodes_.find(addressKey(address));
        if (it != nodes_.end()) {
            std::cout << "[Inference] Node unregistered: " << it->second.public_name << std::endl;
            nodes_.erase(it);
            return true;
        }
        return false;
    }
    
    // ========================================================================
    // Heartbeat & Status
    // ========================================================================
    
    bool heartbeat(const crypto::Address& address, NodeStatus status, 
                   double current_tps = 0.0, double uptime = 100.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = nodes_.find(addressKey(address));
        if (it == nodes_.end()) return false;
        
        it->second.last_heartbeat = now();
        it->second.status = status;
        it->second.uptime_percent = uptime;
        
        if (status == NodeStatus::SERVING && current_tps > 0) {
            it->second.current_tps = current_tps;
            // Exponential moving average
            it->second.average_tps = it->second.average_tps * 0.9 + current_tps * 0.1;
        }
        
        return true;
    }
    
    // ========================================================================
    // Model & Layer Discovery (Petals-style)
    // ========================================================================
    
    std::vector<crypto::Address> findNodesForModel(const std::string& model_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<crypto::Address> result;
        
        for (const auto& [key, node] : nodes_) {
            if (node.status != NodeStatus::ONLINE && node.status != NodeStatus::SERVING) {
                continue;
            }
            
            for (const auto& model : node.supported_models) {
                if (model == model_id) {
                    result.push_back(node.address);
                    break;
                }
            }
        }
        
        return result;
    }
    
    std::vector<crypto::Address> findNodesForLayers(const std::string& model_id, 
                                                     uint32_t start_layer, 
                                                     uint32_t end_layer) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<crypto::Address> result;
        
        for (const auto& [key, node] : nodes_) {
            if (!node.isGPUNode()) continue;
            if (node.status != NodeStatus::SERVING) continue;
            
            bool has_model = false;
            for (const auto& model : node.supported_models) {
                if (model == model_id) {
                    has_model = true;
                    break;
                }
            }
            if (!has_model) continue;
            
            // Check if node serves any of the required layers
            for (uint32_t layer : node.served_layers) {
                if (layer >= start_layer && layer <= end_layer) {
                    result.push_back(node.address);
                    break;
                }
            }
        }
        
        // Sort by TPS (fastest first)
        std::sort(result.begin(), result.end(), [this](const auto& a, const auto& b) {
            auto it_a = nodes_.find(addressKey(a));
            auto it_b = nodes_.find(addressKey(b));
            if (it_a == nodes_.end() || it_b == nodes_.end()) return false;
            return it_a->second.average_tps > it_b->second.average_tps;
        });
        
        return result;
    }
    
    // ========================================================================
    // Request Handling
    // ========================================================================
    
    uint64_t submitRequest(InferenceRequest request) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        request.request_id = ++request_counter_;
        request.submitted_at = now();
        
        // Find route
        auto gpu_nodes = findNodesForModel(request.model_id);
        if (gpu_nodes.empty()) {
            request.success = false;
            request.error = "No nodes available for model: " + request.model_id;
            completed_requests_.push_back(request);
            return 0;
        }
        
        request.route = gpu_nodes;
        pending_requests_.push_back(request);
        
        return request.request_id;
    }
    
    bool completeRequest(uint64_t request_id, const std::string& response, 
                         uint32_t tokens_generated, const crypto::Address& serving_node,
                         double tps) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto it = pending_requests_.begin(); it != pending_requests_.end(); ++it) {
            if (it->request_id != request_id) continue;
            
            it->response = response;
            it->tokens_generated = tokens_generated;
            it->completed_at = now();
            it->latency_ms = it->completed_at - it->submitted_at;
            it->tokens_per_second = tps;
            it->success = true;
            it->serving_node = serving_node;
            
            // Calculate cost
            it->actual_cost_q = tokens_generated * INFERENCE_REWARD_PER_TOKEN;
            
            // Credit the serving node
            auto node_it = nodes_.find(addressKey(serving_node));
            if (node_it != nodes_.end()) {
                node_it->second.tokens_generated += tokens_generated;
                node_it->second.requests_served++;
                node_it->second.last_inference = it->completed_at;
                node_it->second.lifetime_tokens += tokens_generated;
                node_it->second.lifetime_requests++;
            }
            
            // Credit relay nodes
            for (const auto& relay : it->route) {
                if (relay != serving_node) {
                    auto relay_it = nodes_.find(addressKey(relay));
                    if (relay_it != nodes_.end()) {
                        relay_it->second.requests_relayed++;
                    }
                }
            }
            
            completed_requests_.push_back(*it);
            pending_requests_.erase(it);
            return true;
        }
        
        return false;
    }
    
    // ========================================================================
    // Epoch Processing & Rewards
    // ========================================================================
    
    void processEpoch() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t epoch = ++current_epoch_;
        uint64_t total_distributed = 0;
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "  Processing Epoch " << epoch << std::endl;
        std::cout << "========================================" << std::endl;
        
        for (auto& [key, node] : nodes_) {
            auto rewards = InferenceRewardsCalculator::calculate(node, epoch);
            
            if (rewards.total_reward > 0) {
                // Mint rewards to node operator
                accounts_->mint(node.address, rewards.total_reward);
                node.lifetime_rewards_q += rewards.total_reward;
                total_distributed += rewards.total_reward;
                
                std::cout << "  " << node.public_name << ": " 
                          << qToArqon(rewards.total_reward) << " ARQN" << std::endl;
            }
            
            // Reset epoch counters
            node.requests_relayed = 0;
            node.tokens_generated = 0;
            node.requests_served = 0;
        }
        
        std::cout << "  Total Distributed: " << qToArqon(total_distributed) << " ARQN" << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
    
    uint64_t getCurrentEpoch() const { return current_epoch_.load(); }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct NetworkStats {
        uint64_t total_nodes;
        uint64_t gpu_nodes;
        uint64_t relay_nodes;
        uint64_t online_nodes;
        uint64_t serving_nodes;
        uint64_t total_vram_mb;
        uint64_t total_staked_q;
        uint64_t pending_requests;
        uint64_t completed_requests;
        uint64_t lifetime_tokens;
        double network_avg_tps;
        uint64_t current_epoch;
    };
    
    NetworkStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        NetworkStats stats{};
        stats.current_epoch = current_epoch_.load();
        
        double total_tps = 0;
        for (const auto& [key, node] : nodes_) {
            stats.total_nodes++;
            stats.total_staked_q += node.staked_amount;
            stats.lifetime_tokens += node.lifetime_tokens;
            
            if (node.isGPUNode()) {
                stats.gpu_nodes++;
                stats.total_vram_mb += node.getTotalVRAM();
            } else {
                stats.relay_nodes++;
            }
            
            if (node.status == NodeStatus::ONLINE) {
                stats.online_nodes++;
            } else if (node.status == NodeStatus::SERVING) {
                stats.online_nodes++;
                stats.serving_nodes++;
                total_tps += node.average_tps;
            }
        }
        
        stats.pending_requests = pending_requests_.size();
        stats.completed_requests = completed_requests_.size();
        stats.network_avg_tps = stats.serving_nodes > 0 ? total_tps / stats.serving_nodes : 0;
        
        return stats;
    }
    
    void printStats() const {
        auto stats = getStats();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "  Inference Network Statistics" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  Epoch: " << stats.current_epoch << std::endl;
        std::cout << "  Total Nodes: " << stats.total_nodes << std::endl;
        std::cout << "    GPU Nodes: " << stats.gpu_nodes << std::endl;
        std::cout << "    Relay Nodes: " << stats.relay_nodes << std::endl;
        std::cout << "  Online: " << stats.online_nodes << " (Serving: " << stats.serving_nodes << ")" << std::endl;
        std::cout << "  Total VRAM: " << stats.total_vram_mb << " MB" << std::endl;
        std::cout << "  Total Staked: " << qToArqon(stats.total_staked_q) << " ARQN" << std::endl;
        std::cout << "  Network Avg TPS: " << stats.network_avg_tps << std::endl;
        std::cout << "  Lifetime Tokens: " << stats.lifetime_tokens << std::endl;
        std::cout << "  Pending Requests: " << stats.pending_requests << std::endl;
        std::cout << "  Completed Requests: " << stats.completed_requests << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
    
    // ========================================================================
    // Node Access
    // ========================================================================
    
    const InferenceNodeInfo* getNode(const crypto::Address& addr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodes_.find(addressKey(addr));
        return it != nodes_.end() ? &it->second : nullptr;
    }
    
    size_t getNodeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_.size();
    }
};

} // namespace inference
