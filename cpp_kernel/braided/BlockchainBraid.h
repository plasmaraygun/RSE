#pragma once

#include "BraidedKernel.h"
#include "FaultTolerantBraid.h"
#include "../core/Economics.h"
#include "../network/P2PNode.h"
#include "../network/ProjectionSync.h"

#include <memory>
#include <vector>

/**
 * Blockchain-Enabled Braided Kernel
 * 
 * Extends FaultTolerantBraid with:
 * - Cryptographic transaction verification
 * - Economic incentives (gas, fees, staking)
 * - P2P networking for distributed operation
 */

namespace braided {

class BlockchainBraid {
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
    BlockchainBraid(uint32_t torus_id = 0, bool enable_network = false)
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
            
            std::cout << "[BlockchainBraid] Torus " << torus_id 
                      << " initialized with network support" << std::endl;
            std::cout << "[BlockchainBraid] Validator address: " 
                      << crypto::AddressUtil::toHex(validator_keypair_.getAddress()) << std::endl;
        }
    }
    
    ~BlockchainBraid() {
        if (p2p_node_) {
            p2p_node_->stop();
        }
    }
    
    // ========== Blockchain Operations ==========
    
    /**
     * Submit signed transaction
     */
    bool submitTransaction(const crypto::Transaction& tx) {
        // Verify signature
        if (!tx.verify()) {
            std::cerr << "[BlockchainBraid] Invalid transaction signature" << std::endl;
            return false;
        }
        
        // Process through economic system
        auto result = economics_->processor().process(tx);
        
        if (result != economics::TransactionProcessor::Result::SUCCESS) {
            std::cerr << "[BlockchainBraid] Transaction failed: " 
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
            // (simplified - production would use proper serialization)
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
        
        // Parse IP address (simplified)
        uint32_t ip_addr = 0;  // Would parse from string
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
        
        // Get all remote projections
        auto projections = proj_sync_->getAllProjections();
        
        // Apply to local braid (for consistency verification)
        for (const auto& proj : projections) {
            if (proj.torus_id != local_torus_id_) {
                // Verify consistency with remote projection
                braid_->getTorus(local_torus_id_).verifyConsistency(proj);
            }
        }
    }
    
    // ========== Braid Interval (Block Production) ==========
    
    /**
     * Execute one braid interval (like mining a block)
     */
    void executeBraidInterval(uint64_t events_per_interval = 1000) {
        // Run events on local torus
        braid_->getTorus(local_torus_id_).run(events_per_interval);
        
        // Broadcast projection to network
        if (network_enabled_) {
            broadcastProjection();
            syncProjections();
        }
        
        // Distribute rewards to validators
        economics_->onBraidInterval();
        
        std::cout << "[BlockchainBraid] Braid interval " 
                  << economics_->getBraidIntervalCount() << " completed" << std::endl;
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
        uint64_t braid_intervals;
        uint64_t messages_sent;
        uint64_t messages_received;
    };
    
    Stats getStats() const {
        Stats stats;
        stats.total_supply = economics_->accounts().getTotalSupply();
        stats.account_count = economics_->accounts().getAccountCount();
        stats.validator_count = economics_->accounts().getValidators().size();
        stats.peer_count = network_enabled_ ? p2p_node_->getPeerCount() : 0;
        stats.braid_intervals = economics_->getBraidIntervalCount();
        stats.messages_sent = network_enabled_ ? p2p_node_->getMessagesSent() : 0;
        stats.messages_received = network_enabled_ ? p2p_node_->getMessagesReceived() : 0;
        
        return stats;
    }
    
    void printStats() const {
        auto stats = getStats();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "  Blockchain Braid Statistics" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  Torus ID: " << local_torus_id_ << std::endl;
        std::cout << "  Validator: " << crypto::AddressUtil::toHex(validator_keypair_.getAddress()) << std::endl;
        std::cout << "  Total Supply: " << stats.total_supply << " wei" << std::endl;
        std::cout << "  Accounts: " << stats.account_count << std::endl;
        std::cout << "  Validators: " << stats.validator_count << std::endl;
        std::cout << "  Peers: " << stats.peer_count << std::endl;
        std::cout << "  Braid Intervals: " << stats.braid_intervals << std::endl;
        std::cout << "  Messages Sent: " << stats.messages_sent << std::endl;
        std::cout << "  Messages Received: " << stats.messages_received << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
};

} // namespace braided
