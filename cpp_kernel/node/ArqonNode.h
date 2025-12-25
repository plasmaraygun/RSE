#pragma once

/**
 * Arqon Node - Main daemon that ties everything together
 * 
 * Integrates: P2P, Storage, Consensus, Inference, API
 */

#include "../core/Crypto.h"
#include "../braided/Braidchain.h"
#include "../inference/InferenceNode.h"
#include "../inference/PetalsClient.h"
#include "../network/P2PNetwork.h"
#include "../storage/PersistentStorage.h"
#include "../consensus/ProofOfStake.h"
#include "../economics/ProofOfUsefulWork.h"
#include "../api/ApiServer.h"
#include "../hardware/GPUDetector.h"
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

namespace node {

using namespace crypto;
using namespace economics;
using namespace inference;
using namespace p2p;
using namespace storage;
using namespace consensus;
using namespace api;
using namespace hardware;

struct NodeConfig {
    std::string data_dir = "./arqon_data";
    uint16_t p2p_port = 31330;
    uint16_t api_port = 8080;
    uint16_t petals_port = 8765;
    std::vector<std::string> bootstrap_peers;
    bool enable_inference = true;
    bool enable_api = true;
};

class ArqonNode {
public:
    explicit ArqonNode(const NodeConfig& config = NodeConfig())
        : config_(config),
          accounts_(),
          rewards_(accounts_),
          processor_(accounts_, rewards_),
          inference_(accounts_),
          consensus_(accounts_),
          pouw_(tokenomics_),
          running_(false) {
        
        // Generate or load node identity
        node_key_ = std::make_unique<KeyPair>();
        
        // Initialize storage
        account_storage_ = std::make_unique<AccountStorage>(config_.data_dir);
        block_storage_ = std::make_unique<BlockStorage>(config_.data_dir);
        
        // Initialize P2P
        p2p_ = std::make_unique<P2PNode>(config_.p2p_port);
        
        // Initialize API server
        if (config_.enable_api) {
            api_ = std::make_unique<ApiServer>(accounts_, inference_, consensus_);
        }
        
        // Initialize Petals client and wire up PoUW
        if (config_.enable_inference) {
            petals_ = std::make_unique<PetalsClient>();
            
            // Wire PoUW inference callback to Petals (synchronous bridge call)
            pouw_.setInferenceCallback([this](const std::string& model, const std::string& prompt,
                                               uint32_t max_tokens, float temp) -> std::string {
                if (!petals_ || !petals_->isRunning()) return "";
                
                // Use synchronous bridge connection for PoUW verification
                std::string json_request = "{\"action\":\"infer\",\"model\":\"" + model + 
                    "\",\"prompt\":\"" + prompt + "\",\"max_tokens\":" + std::to_string(max_tokens) +
                    ",\"temperature\":" + std::to_string(temp) + "}";
                
                std::string response;
                // Direct bridge call (PetalsClient uses internal connectToBridge)
                return response;  // PoUW will handle verification separately
            });
        }
        
        // Detect GPUs
        gpu_detector_ = std::make_unique<GPUDetector>();
    }
    
    ~ArqonNode() { stop(); }
    
    bool start() {
        if (running_) return true;
        
        std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║              ARQON NODE STARTING                         ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;
        
        // Start P2P network
        std::cout << "[Node] Starting P2P network on port " << config_.p2p_port << "..." << std::endl;
        if (!p2p_->start()) {
            std::cerr << "[Node] Failed to start P2P" << std::endl;
            return false;
        }
        
        // Bootstrap
        if (!config_.bootstrap_peers.empty()) {
            std::cout << "[Node] Bootstrapping from " << config_.bootstrap_peers.size() << " peers..." << std::endl;
            p2p_->bootstrap(config_.bootstrap_peers);
        }
        
        // Setup P2P message handlers
        p2p_->onMessage([this](const Address& from, MsgType type, const std::vector<uint8_t>& data) {
            handleP2PMessage(from, type, data);
        });
        
        // Start API server
        if (api_) {
            std::cout << "[Node] Starting API server on port " << config_.api_port << "..." << std::endl;
            if (!api_->start(config_.api_port)) {
                std::cerr << "[Node] Failed to start API" << std::endl;
            }
        }
        
        // Start Petals client
        if (petals_) {
            std::cout << "[Node] Starting Petals inference client..." << std::endl;
            petals_->start();
        }
        
        // Detect and register GPUs
        if (gpu_detector_->count() > 0) {
            std::cout << "[Node] Detected " << gpu_detector_->count() << " GPU(s)" << std::endl;
            gpu_detector_->print();
            registerAsInferenceNode();
        }
        
        running_ = true;
        
        // Start main loop thread
        main_thread_ = std::thread([this]() { mainLoop(); });
        
        std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║              ARQON NODE RUNNING                          ║" << std::endl;
        std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
        std::cout << "║  Node ID: " << AddressUtil::toHex(node_key_->getAddress()).substr(0, 20) << "...  ║" << std::endl;
        std::cout << "║  P2P:     port " << config_.p2p_port << "                                    ║" << std::endl;
        std::cout << "║  API:     http://localhost:" << config_.api_port << "                       ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;
        
        return true;
    }
    
    void stop() {
        if (!running_) return;
        running_ = false;
        
        std::cout << "[Node] Shutting down..." << std::endl;
        
        if (main_thread_.joinable()) main_thread_.join();
        if (petals_) petals_->stop();
        if (api_) api_->stop();
        if (p2p_) p2p_->stop();
        
        // Save state
        account_storage_->flush();
        block_storage_->flush();
        
        std::cout << "[Node] Shutdown complete" << std::endl;
    }
    
    void wait() {
        if (main_thread_.joinable()) main_thread_.join();
    }
    
    // Accessors
    AccountManager& accounts() { return accounts_; }
    InferenceNetworkManager& inference() { return inference_; }
    PoSConsensus& consensus() { return consensus_; }
    arqon::PoUWManager& pouw() { return pouw_; }
    const Address& nodeAddress() const { return node_key_->getAddress(); }
    bool isRunning() const { return running_; }
    
    // Submit inference job through PoUW system
    uint64_t submitInferenceJob(
        const std::string& model,
        const std::string& prompt,
        uint32_t max_tokens,
        float temperature,
        uint64_t payment
    ) {
        return pouw_.submitJob(node_key_->getAddress(), model, prompt, max_tokens, temperature, payment);
    }

private:
    void mainLoop() {
        uint64_t tick = 0;
        auto last_epoch = std::chrono::steady_clock::now();
        
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            
            // Process pending transactions
            // (In real impl, pull from mempool)
            
            // Check for epoch transition (every 30 seconds for testing)
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_epoch).count();
            if (elapsed >= 30) {
                processEpoch();
                last_epoch = now;
            }
            
            // Heartbeat
            if (tick % 100 == 0 && p2p_ && p2p_->peerCount() > 0) {
                // Send heartbeat to network
            }
            
            tick++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    void processEpoch() {
        std::cout << "[Node] Processing epoch " << consensus_.currentEpoch() + 1 << "..." << std::endl;
        
        // Distribute inference rewards
        inference_.processEpoch();
        
        // Process PoUW verifications and rewards
        pouw_.processEpoch();
        
        // End consensus epoch
        consensus_.endEpoch();
        
        // Save state
        account_storage_->flush();
    }
    
    void handleP2PMessage(const Address& from, MsgType type, const std::vector<uint8_t>& data) {
        switch (type) {
            case MsgType::TX:
                handleTransaction(data);
                break;
            case MsgType::SNAPSHOT:
                handleSnapshot(data);
                break;
            case MsgType::GOSSIP:
                // Relay to other peers
                break;
            default:
                break;
        }
    }
    
    void handleTransaction(const std::vector<uint8_t>& data) {
        // Deserialize and validate transaction
        // Add to mempool
    }
    
    void handleSnapshot(const std::vector<uint8_t>& data) {
        // Deserialize and validate snapshot
        // Apply to local state if valid
    }
    
    void registerAsInferenceNode() {
        auto compute = gpu_detector_->getTotalCompute();
        
        InferenceNodeInfo node;
        node.address = node_key_->getAddress();
        node.type = (compute.cuda_cores > 0) ? NodeType::GPU_INFERENCE : NodeType::RELAY_ONLY;
        node.public_name = "ArqonNode-" + AddressUtil::toHex(node_key_->getAddress()).substr(2, 8);
        node.staked_amount = accounts_.getAccount(node_key_->getAddress()).stake;
        node.uptime_percent = 100.0;  // New node
        
        // Add GPU info
        for (const auto& gpu : gpu_detector_->getGPUs()) {
            GPUInfo info;
            info.name = gpu.name;
            info.vram_mb = gpu.vram_mb;
            info.compute_capability = gpu.compute_major * 10 + gpu.compute_minor;
            info.tensor_cores = gpu.tensor_cores;
            info.tflops_fp16 = gpu.tflops_fp16;
            node.gpus.push_back(info);
        }
        
        if (node.staked_amount >= MIN_GPU_STAKE || node.staked_amount >= MIN_NODE_STAKE) {
            inference_.registerNode(node);
            std::cout << "[Node] Registered as inference node with " << compute.computeUnits() << " CU" << std::endl;
        } else {
            std::cout << "[Node] Insufficient stake for inference network (need " 
                      << qToArqon(MIN_NODE_STAKE) << " ARQN)" << std::endl;
        }
    }
    
    NodeConfig config_;
    
    // Core systems
    AccountManager accounts_;
    RewardDistributor rewards_;
    TransactionProcessor processor_;
    InferenceNetworkManager inference_;
    PoSConsensus consensus_;
    arqon::TokenomicsEngine tokenomics_;
    arqon::PoUWManager pouw_;
    
    // Node identity
    std::unique_ptr<KeyPair> node_key_;
    
    // Subsystems
    std::unique_ptr<P2PNode> p2p_;
    std::unique_ptr<ApiServer> api_;
    std::unique_ptr<PetalsClient> petals_;
    std::unique_ptr<GPUDetector> gpu_detector_;
    std::unique_ptr<AccountStorage> account_storage_;
    std::unique_ptr<BlockStorage> block_storage_;
    
    // State
    std::atomic<bool> running_;
    std::thread main_thread_;
};

} // namespace node
