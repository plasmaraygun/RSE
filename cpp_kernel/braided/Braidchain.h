#pragma once

#include "BraidedKernel.h"
#include "FaultTolerantBraid.h"
#include "../core/Economics.h"
#include "../network/P2PNode.h"
#include "../network/ProjectionSync.h"

#include <memory>
#include <vector>

/**
 * Braidchain: Topologically-Secured Distributed Ledger
 * 
 * Extends FaultTolerantBraid with:
 * - Cryptographic transaction verification
 * - Economic incentives (fees, staking)
 * - P2P networking for distributed operation
 * 
 * Unlike traditional blockchains:
 * - No sequential blocks - continuous event flow with state snapshots
 * - No mining - consensus via 3-way topological braiding
 * - O(1) coordination overhead regardless of network size
 * - State lives in toroidal space, not a linear chain
 */

namespace braided {

class Braidchain {
private:
    // Core braided system
    std::unique_ptr<FaultTolerantBraid> braid_;
    
    // Economic system
    std::unique_ptr<economics::EconomicSystem> economics_;
    
    // P2P networking
    std::unique_ptr<network::P2PNode> p2p_node_;
    std::unique_ptr<network::ProjectionSync> proj_sync_;
    
    // Local torus identity
    crypto::KeyPair validator_keypair_;
    uint32_t local_torus_id_;
    
    // Network enabled flag
    bool network_enabled_;
    
public:
    Braidchain(uint32_t torus_id = 0, bool enable_network = false)
        : local_torus_id_(torus_id), network_enabled_(enable_network) {
        
        // Initialize core braid
        braid_ = std::make_unique<FaultTolerantBraid>();
        
        // Initialize economic system
        economics_ = std::make_unique<economics::EconomicSystem>();
        
        // Initialize P2P if enabled
        if (network_enabled_) {
            validator_keypair_.generate();
            
            p2p_node_ = std::make_unique<network::P2PNode>(
                validator_keypair_.getAddress(),
                torus_id,
                network::DEFAULT_PORT + torus_id  // Different port per torus
            );
            
            proj_sync_ = std::make_unique<network::ProjectionSync>(
                *p2p_node_,
                torus_id
            );
            
            p2p_node_->start();
            
            std::cout << "[Braidchain] Torus " << torus_id 
                      << " initialized with network support" << std::endl;
            std::cout << "[Braidchain] Validator address: " 
                      << crypto::AddressUtil::toHex(validator_keypair_.getAddress()) << std::endl;
        }
    }
    
    ~Braidchain() {
        if (p2p_node_) {
            p2p_node_->stop();
        }
    }
    
    // ========== Transaction Operations ==========
    
    /**
     * Submit signed transaction
     */
    bool submitTransaction(const crypto::Transaction& tx) {
        // Verify signature
        if (!tx.verify()) {
            std::cerr << "[Braidchain] Invalid transaction signature" << std::endl;
            return false;
        }
        
        // Process through economic system
        auto result = economics_->processor().process(tx);
        
        if (result != economics::TransactionProcessor::Result::SUCCESS) {
            std::cerr << "[Braidchain] Transaction failed: " 
                      << economics::TransactionProcessor::resultString(result) << std::endl;
            return false;
        }
        
        // Convert to RDLEvent and inject into kernel
        RDLEvent event;
        event.timestamp = braid_->getTorus(local_torus_id_).getCurrentTime();
        event.from = tx.from;
        event.to = tx.to;
        event.gas_price = tx.gas_price;
        event.gas_limit = tx.gas_limit;
        event.nonce = tx.nonce;
        event.signature = tx.signature;
        event.verified = true;
        event.payload = static_cast<int>(tx.value);
        
        // Inject into local torus (with full coordinates)
        braid_->getTorus(local_torus_id_).injectEvent(0, 0, 0, 1, 1, 1, event.payload);
        
        // Broadcast to network if enabled
        if (network_enabled_ && p2p_node_) {
            // Serialize transaction and broadcast
            std::vector<uint8_t> tx_data(sizeof(crypto::Transaction));
            std::memcpy(tx_data.data(), &tx, sizeof(crypto::Transaction));
            
            network::Message msg(network::MessageType::TX, tx_data);
            p2p_node_->broadcast(msg);
        }
        
        return true;
    }
    
    /**
     * Stake tokens to become validator
     */
    bool stake(const crypto::Address& addr, uint64_t amount) {
        return economics_->accounts().stake(addr, amount);
    }
    
    /**
     * Get account balance
     */
    uint64_t getBalance(const crypto::Address& addr) const {
        return economics_->accounts().getAccount(addr).balance;
    }
    
    /**
     * Get account nonce
     */
    uint64_t getNonce(const crypto::Address& addr) const {
        return economics_->accounts().getAccount(addr).nonce;
    }
    
    /**
     * Mint tokens (genesis only)
     */
    void mint(const crypto::Address& addr, uint64_t amount) {
        economics_->accounts().mint(addr, amount);
    }
    
    // ========== Network Operations ==========
    
    /**
     * Connect to peer
     */
    bool connectPeer(const std::string& ip, uint16_t port) {
        if (!network_enabled_ || !p2p_node_) {
            return false;
        }
        
        uint32_t ip_addr = 0;
        network::NetAddr addr(ip_addr, port);
        
        return p2p_node_->connectPeer(addr);
    }
    
    /**
     * Get peer count
     */
    size_t getPeerCount() const {
        if (!network_enabled_ || !p2p_node_) {
            return 0;
        }
        
        return p2p_node_->getPeerCount();
    }
    
    /**
     * Broadcast local projection to network
     */
    void broadcastProjection() {
        if (!network_enabled_ || !proj_sync_) {
            return;
        }
        
        Projection proj = braid_->getTorus(local_torus_id_).extractProjection();
        proj_sync_->broadcastProjection(proj);
    }
    
    /**
     * Sync projections from network
     */
    void syncProjections() {
        if (!network_enabled_ || !proj_sync_) {
            return;
        }
        
        auto projections = proj_sync_->getAllProjections();
        
        for (const auto& proj : projections) {
            if (proj.torus_id != local_torus_id_) {
                braid_->getTorus(local_torus_id_).verifyConsistency(proj);
            }
        }
    }
    
    // ========== Braid Snapshot (State Checkpoint) ==========
    
    /**
     * Execute one braid snapshot interval
     * 
     * Unlike blockchain "blocks", this:
     * - Processes continuous event flow
     * - Creates state checkpoint via projection exchange
     * - Achieves consensus through topological agreement
     */
    void executeSnapshot(uint64_t events_per_snapshot = 1000) {
        // Run events on local torus
        braid_->getTorus(local_torus_id_).run(events_per_snapshot);
        
        // Broadcast projection to network (topological consensus)
        if (network_enabled_) {
            broadcastProjection();
            syncProjections();
        }
        
        // Distribute rewards to validators
        economics_->onBraidInterval();
        
        std::cout << "[Braidchain] Snapshot " 
                  << economics_->getBraidIntervalCount() << " completed" << std::endl;
    }
    
    // ========== Topological Consensus ==========
    
    /**
     * Check if all 3 tori agree on state (topological consensus)
     */
    bool hasConsensus() const {
        auto proj0 = braid_->getTorus(0).extractProjection();
        auto proj1 = braid_->getTorus(1).extractProjection();
        auto proj2 = braid_->getTorus(2).extractProjection();
        
        // Consensus via projection agreement (total events processed)
        return proj0.total_events_processed == proj1.total_events_processed && 
               proj1.total_events_processed == proj2.total_events_processed;
    }
    
    /**
     * Get combined state hash (from all 3 tori)
     */
    std::array<uint8_t, 32> getStateHash() const {
        std::array<uint8_t, 32> combined{};
        for (int i = 0; i < 3; i++) {
            auto proj = braid_->getTorus(i).extractProjection();
            // XOR the state_hash into combined (spread across 8 bytes, repeat for 32)
            for (size_t j = 0; j < 32; j++) {
                combined[j] ^= (proj.state_hash >> ((j % 8) * 8)) & 0xFF;
            }
        }
        return combined;
    }
    
    // ========== Accessors ==========
    
    FaultTolerantBraid& getBraid() { return *braid_; }
    const FaultTolerantBraid& getBraid() const { return *braid_; }
    
    economics::EconomicSystem& getEconomics() { return *economics_; }
    const economics::EconomicSystem& getEconomics() const { return *economics_; }
    
    network::P2PNode* getP2PNode() { return p2p_node_.get(); }
    network::ProjectionSync* getProjectionSync() { return proj_sync_.get(); }
    
    const crypto::KeyPair& getValidatorKey() const { return validator_keypair_; }
    uint32_t getTorusId() const { return local_torus_id_; }
    
    bool isNetworkEnabled() const { return network_enabled_; }
    
    // ========== Statistics ==========
    
    struct Stats {
        uint64_t total_supply;
        size_t account_count;
        size_t validator_count;
        size_t peer_count;
        uint64_t snapshots;          // Renamed from braid_intervals
        uint64_t messages_sent;
        uint64_t messages_received;
    };
    
    Stats getStats() const {
        Stats stats;
        stats.total_supply = economics_->accounts().getTotalSupply();
        stats.account_count = economics_->accounts().getAccountCount();
        stats.validator_count = economics_->accounts().getValidators().size();
        stats.peer_count = network_enabled_ ? p2p_node_->getPeerCount() : 0;
        stats.snapshots = economics_->getBraidIntervalCount();
        stats.messages_sent = network_enabled_ ? p2p_node_->getMessagesSent() : 0;
        stats.messages_received = network_enabled_ ? p2p_node_->getMessagesReceived() : 0;
        
        return stats;
    }
    
    void printStats() const {
        auto stats = getStats();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "  Braidchain Statistics" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  Torus ID: " << local_torus_id_ << std::endl;
        std::cout << "  Validator: " << crypto::AddressUtil::toHex(validator_keypair_.getAddress()) << std::endl;
        std::cout << "  Total Supply: " << stats.total_supply << " Q" << std::endl;
        std::cout << "  Accounts: " << stats.account_count << std::endl;
        std::cout << "  Validators: " << stats.validator_count << std::endl;
        std::cout << "  Peers: " << stats.peer_count << std::endl;
        std::cout << "  Snapshots: " << stats.snapshots << std::endl;
        std::cout << "  Messages Sent: " << stats.messages_sent << std::endl;
        std::cout << "  Messages Received: " << stats.messages_received << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
};

// Backwards compatibility alias
using BlockchainBraid = Braidchain;

} // namespace braided
