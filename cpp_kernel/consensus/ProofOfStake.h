#pragma once

/**
 * F: Proof of Stake Consensus
 * 
 * Implements finality-focused PoS with:
 * - Validator selection by stake weight
 * - Block proposal and voting
 * - Slashing for misbehavior
 * - Finality after 2/3 majority
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <random>
#include <chrono>
#include <unordered_set>

namespace consensus {

using namespace crypto;
using namespace economics;

// Consensus parameters
constexpr uint32_t EPOCH_LENGTH = 32;           // Blocks per epoch
constexpr uint32_t MIN_VALIDATORS = 4;          // Minimum validators
constexpr uint32_t MAX_VALIDATORS = 100;        // Maximum validators
constexpr double FINALITY_THRESHOLD = 0.67;     // 2/3 majority
constexpr uint64_t SLASH_DOUBLE_SIGN = 5;       // 5% slash for double signing
constexpr uint64_t SLASH_OFFLINE = 1;           // 1% slash for being offline

// Block header
struct BlockHeader {
    uint64_t height;
    uint64_t timestamp;
    std::array<uint8_t, 32> prev_hash;
    std::array<uint8_t, 32> state_root;
    std::array<uint8_t, 32> tx_root;
    Address proposer;
    Signature proposer_sig;
    uint64_t epoch;
    
    std::array<uint8_t, 32> hash() const {
        std::array<uint8_t, 32> h{};
        // Simple hash: XOR all fields
        for (size_t i = 0; i < 32; i++) {
            h[i] = prev_hash[i] ^ state_root[i] ^ tx_root[i];
        }
        h[0] ^= (height & 0xFF);
        h[1] ^= ((height >> 8) & 0xFF);
        return h;
    }
};

// Vote for block
struct Vote {
    uint64_t height;
    std::array<uint8_t, 32> block_hash;
    Address validator;
    Signature signature;
    uint64_t stake_weight;
};

// Validator info
struct ValidatorInfo {
    Address address;
    uint64_t stake;
    uint64_t joined_epoch;
    uint64_t last_active_epoch;
    uint32_t blocks_proposed;
    uint32_t blocks_missed;
    bool is_active;
    
    double uptime() const {
        uint32_t total = blocks_proposed + blocks_missed;
        return total > 0 ? (double)blocks_proposed / total : 1.0;
    }
};

// Proof of Stake consensus engine
class PoSConsensus {
public:
    PoSConsensus(AccountManager& accounts) : accounts_(accounts), current_epoch_(0), current_height_(0) {}
    
    // Register as validator (must have stake)
    bool registerValidator(const Address& addr) {
        const auto& account = accounts_.getAccount(addr);
        if (account.stake < MIN_STAKE) {
            return false;
        }
        
        if (validators_.find(addressKey(addr)) != validators_.end()) {
            return false;  // Already registered
        }
        
        ValidatorInfo info;
        info.address = addr;
        info.stake = account.stake;
        info.joined_epoch = current_epoch_;
        info.last_active_epoch = current_epoch_;
        info.blocks_proposed = 0;
        info.blocks_missed = 0;
        info.is_active = true;
        
        validators_[addressKey(addr)] = info;
        updateValidatorSet();
        
        return true;
    }
    
    // Deregister validator
    void deregisterValidator(const Address& addr) {
        validators_.erase(addressKey(addr));
        updateValidatorSet();
    }
    
    // Select proposer for given height (deterministic based on stake)
    Address selectProposer(uint64_t height) {
        if (active_validators_.empty()) {
            return Address{};
        }
        
        // Weighted random selection based on stake
        uint64_t total_stake = 0;
        for (const auto& v : active_validators_) {
            total_stake += v.stake;
        }
        
        // Deterministic seed from height
        std::mt19937_64 rng(height * 0x123456789ABCDEF);
        uint64_t target = rng() % total_stake;
        
        uint64_t cumulative = 0;
        for (const auto& v : active_validators_) {
            cumulative += v.stake;
            if (cumulative > target) {
                return v.address;
            }
        }
        
        return active_validators_[0].address;
    }
    
    // Propose a new block
    bool proposeBlock(const BlockHeader& block, const KeyPair& proposer_key) {
        // Verify proposer is selected
        Address expected = selectProposer(block.height);
        if (expected != proposer_key.getAddress()) {
            return false;
        }
        
        // Verify block builds on current chain
        if (block.height != current_height_ + 1) {
            return false;
        }
        
        // Store pending block
        pending_block_ = block;
        pending_votes_.clear();
        
        // Record proposal
        auto key = addressKey(proposer_key.getAddress());
        if (validators_.find(key) != validators_.end()) {
            validators_[key].blocks_proposed++;
            validators_[key].last_active_epoch = current_epoch_;
        }
        
        return true;
    }
    
    // Vote on pending block
    bool vote(const Vote& vote) {
        // Verify voter is validator
        auto key = addressKey(vote.validator);
        auto it = validators_.find(key);
        if (it == validators_.end() || !it->second.is_active) {
            return false;
        }
        
        // Verify vote is for pending block
        if (vote.height != pending_block_.height || 
            vote.block_hash != pending_block_.hash()) {
            return false;
        }
        
        // Check for double voting
        if (votes_cast_.find(key) != votes_cast_.end()) {
            // Double vote! Slash
            slash(vote.validator, SLASH_DOUBLE_SIGN);
            return false;
        }
        
        pending_votes_.push_back(vote);
        votes_cast_.insert(key);
        
        // Check for finality
        checkFinality();
        
        return true;
    }
    
    // Check if block has achieved finality
    bool checkFinality() {
        uint64_t voted_stake = 0;
        uint64_t total_stake = 0;
        
        for (const auto& v : active_validators_) {
            total_stake += v.stake;
        }
        
        for (const auto& vote : pending_votes_) {
            voted_stake += vote.stake_weight;
        }
        
        if (total_stake > 0 && 
            (double)voted_stake / total_stake >= FINALITY_THRESHOLD) {
            // Block finalized!
            finalizeBlock();
            return true;
        }
        
        return false;
    }
    
    // Finalize current block
    void finalizeBlock() {
        current_height_ = pending_block_.height;
        finalized_blocks_.push_back(pending_block_);
        
        // Check for epoch transition
        if (current_height_ % EPOCH_LENGTH == 0) {
            endEpoch();
        }
        
        // Clear voting state
        pending_votes_.clear();
        votes_cast_.clear();
    }
    
    // End of epoch processing
    void endEpoch() {
        current_epoch_++;
        
        // Slash offline validators
        for (auto& [key, info] : validators_) {
            if (info.is_active && info.last_active_epoch < current_epoch_ - 1) {
                slash(info.address, SLASH_OFFLINE);
                info.blocks_missed++;
            }
        }
        
        // Update validator set for next epoch
        updateValidatorSet();
    }
    
    // Slash a validator
    void slash(const Address& addr, uint64_t percent) {
        accounts_.slash(addr);
        
        auto key = addressKey(addr);
        auto it = validators_.find(key);
        if (it != validators_.end()) {
            // Update stake from account
            it->second.stake = accounts_.getAccount(addr).stake;
            
            // Remove if below minimum
            if (it->second.stake < MIN_STAKE) {
                it->second.is_active = false;
            }
        }
        
        std::cout << "[Consensus] Slashed validator " << key.substr(0, 16) 
                  << "... by " << percent << "%" << std::endl;
    }
    
    // Get current epoch
    uint64_t currentEpoch() const { return current_epoch_; }
    
    // Get current height
    uint64_t currentHeight() const { return current_height_; }
    
    // Get validator count
    size_t validatorCount() const { return active_validators_.size(); }
    
    // Get total staked
    uint64_t totalStaked() const {
        uint64_t total = 0;
        for (const auto& v : active_validators_) {
            total += v.stake;
        }
        return total;
    }
    
    // Check if address is validator
    bool isValidator(const Address& addr) const {
        auto key = addressKey(addr);
        auto it = validators_.find(key);
        return it != validators_.end() && it->second.is_active;
    }

private:
    void updateValidatorSet() {
        active_validators_.clear();
        
        for (const auto& [key, info] : validators_) {
            if (info.is_active && info.stake >= MIN_STAKE) {
                active_validators_.push_back(info);
            }
        }
        
        // Sort by stake descending
        std::sort(active_validators_.begin(), active_validators_.end(),
            [](const ValidatorInfo& a, const ValidatorInfo& b) {
                return a.stake > b.stake;
            });
        
        // Limit to max validators
        if (active_validators_.size() > MAX_VALIDATORS) {
            active_validators_.resize(MAX_VALIDATORS);
        }
    }
    
    static std::string addressKey(const Address& addr) {
        std::string key;
        for (uint8_t b : addr) {
            key += "0123456789abcdef"[b >> 4];
            key += "0123456789abcdef"[b & 0xF];
        }
        return key;
    }
    
    AccountManager& accounts_;
    
    uint64_t current_epoch_;
    uint64_t current_height_;
    
    std::unordered_map<std::string, ValidatorInfo> validators_;
    std::vector<ValidatorInfo> active_validators_;
    
    BlockHeader pending_block_;
    std::vector<Vote> pending_votes_;
    std::unordered_set<std::string> votes_cast_;
    
    std::vector<BlockHeader> finalized_blocks_;
};

} // namespace consensus
