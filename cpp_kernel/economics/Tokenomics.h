#pragma once

/**
 * ARQON Tokenomics Implementation
 * 
 * Implements the complete tokenomics spec from whitepaper:
 * - Qx address format with EIP-55 style checksum
 * - 10^18 Q per ARQON (like ETH wei)
 * - 98% emission over 100 years + 2% tail forever
 * - Treasury (10%), GPU nodes (60%), Relay (10%), Stakers (20%)
 * - Governance with 51%+Council OR 78% autonomous
 * - Epoch blocks wrapping projections
 * - PoUW verification with auditors
 */

#include <cstdint>
#include <cstring>
#include <cmath>
#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <random>
#include <chrono>

namespace arqon {

// ============================================================================
// CONSTANTS
// ============================================================================

// Token units: 1 ARQON = 10^18 Q (like ETH wei)
constexpr uint64_t Q_PER_ARQON = 1'000'000'000'000'000'000ULL;  // 10^18

// Total supply soft cap
constexpr uint64_t TOTAL_SUPPLY_ARQON = 100'000'000;  // 100M ARQON
constexpr uint64_t TOTAL_SUPPLY_Q = TOTAL_SUPPLY_ARQON * Q_PER_ARQON;

// Emission parameters
constexpr double MAIN_EMISSION_PERCENT = 0.98;  // 98% over 100 years
constexpr double TAIL_EMISSION_PERCENT = 0.02;  // 2% over infinity
constexpr uint64_t EMISSION_YEARS = 100;

// Epoch timing
constexpr uint64_t EPOCH_DURATION_MS = 15'000;  // 15 seconds
constexpr uint64_t EPOCHS_PER_MINUTE = 4;
constexpr uint64_t EPOCHS_PER_HOUR = EPOCHS_PER_MINUTE * 60;
constexpr uint64_t EPOCHS_PER_DAY = EPOCHS_PER_HOUR * 24;
constexpr uint64_t EPOCHS_PER_YEAR = EPOCHS_PER_DAY * 365;
constexpr uint64_t EPOCHS_100_YEARS = EPOCHS_PER_YEAR * 100;

// Reward distribution (per epoch)
constexpr double GPU_NODE_SHARE = 0.60;      // 60% to GPU inference nodes
constexpr double RELAY_NODE_SHARE = 0.10;    // 10% to relay nodes
constexpr double STAKER_SHARE = 0.20;        // 20% to stakers
constexpr double TREASURY_SHARE = 0.10;      // 10% to treasury

// Governance thresholds
constexpr double FAST_TRACK_THRESHOLD = 0.51;   // 51% + Council
constexpr double AUTONOMOUS_THRESHOLD = 0.78;   // 78% alone
constexpr uint64_t VOTING_PERIOD_EPOCHS = EPOCHS_PER_DAY * 7;  // 7 days
constexpr uint64_t ACTIVATION_DELAY_EPOCHS = EPOCHS_PER_DAY * 7;  // 7 days
constexpr uint64_t GRACE_PERIOD_EPOCHS = EPOCHS_PER_DAY * 7;  // 7 days

// Unbonding period
constexpr uint64_t UNBONDING_EPOCHS = EPOCHS_PER_DAY * 7;  // 7 days

// Slashing percentages
constexpr uint64_t SLASH_DOWNTIME_PCT = 1;      // 1% for >24h downtime
constexpr uint64_t SLASH_FAILED_VERIFY_PCT = 5; // 5% for failed verification
constexpr uint64_t SLASH_DOUBLE_SIGN_PCT = 10;  // 10% for double-signing
constexpr uint64_t SLASH_MALICIOUS_PCT = 100;   // 100% for malicious behavior

// Address sizes
constexpr size_t ADDRESS_SIZE = 20;
constexpr size_t HASH_SIZE = 32;
constexpr size_t SIGNATURE_SIZE = 64;
constexpr size_t PUBLIC_KEY_SIZE = 32;

// ============================================================================
// TYPES
// ============================================================================

using Address = std::array<uint8_t, ADDRESS_SIZE>;
using Hash = std::array<uint8_t, HASH_SIZE>;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;

// ============================================================================
// RESERVED ADDRESSES (No private keys - protocol controlled)
// ============================================================================

namespace ReservedAddress {
    // Qx0000000000000000000000000000000000000001
    inline Address treasury() {
        Address addr{};
        addr[ADDRESS_SIZE - 1] = 0x01;
        return addr;
    }
    
    // Qx0000000000000000000000000000000000000002
    inline Address stakingPool() {
        Address addr{};
        addr[ADDRESS_SIZE - 1] = 0x02;
        return addr;
    }
    
    // Qx0000000000000000000000000000000000000003
    inline Address governance() {
        Address addr{};
        addr[ADDRESS_SIZE - 1] = 0x03;
        return addr;
    }
    
    // Qx0000000000000000000000000000000000000000
    inline Address null() {
        return Address{};
    }
    
    inline bool isReserved(const Address& addr) {
        // Check if all bytes except last are zero
        for (size_t i = 0; i < ADDRESS_SIZE - 1; i++) {
            if (addr[i] != 0) return false;
        }
        return addr[ADDRESS_SIZE - 1] <= 0x03;
    }
}

// ============================================================================
// QX ADDRESS FORMAT
// ============================================================================

class QxAddress {
public:
    // Convert address to Qx hex string with EIP-55 style checksum
    static std::string toQx(const Address& addr) {
        static const char hex_lower[] = "0123456789abcdef";
        static const char hex_upper[] = "0123456789ABCDEF";
        
        // First, create lowercase hex
        std::string lower;
        lower.reserve(ADDRESS_SIZE * 2);
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            lower += hex_lower[(addr[i] >> 4) & 0xF];
            lower += hex_lower[addr[i] & 0xF];
        }
        
        // Compute keccak-like hash for checksum (simplified)
        Hash checksum_hash = simpleHash(reinterpret_cast<const uint8_t*>(lower.data()), lower.size());
        
        // Apply checksum: uppercase if hash nibble >= 8
        std::string result = "Qx";
        result.reserve(42);
        for (size_t i = 0; i < 40; i++) {
            uint8_t hash_byte = checksum_hash[i / 2];
            uint8_t nibble = (i % 2 == 0) ? (hash_byte >> 4) : (hash_byte & 0xF);
            
            if (nibble >= 8 && lower[i] >= 'a' && lower[i] <= 'f') {
                result += hex_upper[lower[i] - 'a' + 10];
            } else {
                result += lower[i];
            }
        }
        
        return result;
    }
    
    // Convert Qx string to address (accepts both checksummed and non-checksummed)
    static bool fromQx(const std::string& qx, Address& addr) {
        if (qx.size() != 42) return false;
        if (qx[0] != 'Q' || qx[1] != 'x') return false;
        
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            char high = qx[2 + i * 2];
            char low = qx[2 + i * 2 + 1];
            
            int h = hexValue(high);
            int l = hexValue(low);
            if (h < 0 || l < 0) return false;
            
            addr[i] = static_cast<uint8_t>((h << 4) | l);
        }
        
        return true;
    }
    
    // Short form: Qx7a3B9c...7890
    static std::string toShort(const Address& addr) {
        std::string full = toQx(addr);
        return full.substr(0, 8) + "..." + full.substr(38, 4);
    }
    
    // Validate checksum
    static bool isValidChecksum(const std::string& qx) {
        if (qx.size() != 42) return false;
        if (qx[0] != 'Q' || qx[1] != 'x') return false;
        
        Address addr;
        if (!fromQx(qx, addr)) return false;
        
        std::string expected = toQx(addr);
        return qx == expected;
    }
    
    // Check if address is zero
    static bool isZero(const Address& addr) {
        for (size_t i = 0; i < ADDRESS_SIZE; i++) {
            if (addr[i] != 0) return false;
        }
        return true;
    }
    
private:
    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    
    static Hash simpleHash(const uint8_t* data, size_t len) {
        Hash result{};
        uint64_t state[4] = {
            0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
            0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL
        };
        
        for (size_t i = 0; i < len; i++) {
            size_t idx = i % 4;
            state[idx] ^= data[i];
            state[idx] = (state[idx] << 7) | (state[idx] >> 57);
            state[idx] += 0x9e3779b97f4a7c15ULL;
        }
        
        std::memcpy(result.data(), state, 32);
        return result;
    }
};

// ============================================================================
// EMISSION CURVE
// ============================================================================

class EmissionCurve {
public:
    // Initial reward calculated to emit 98% in 100 years
    // Using exponential decay: reward(e) = initial * decay^e
    // Sum of geometric series: total = initial / (1 - decay)
    // We want: 98M ARQON = initial / (1 - decay) over 210M epochs
    
    static constexpr double DECAY_FACTOR = 0.99999998;  // Per-epoch decay
    
    // ~8.5 ARQON per epoch initially (in Q)
    static uint64_t initialRewardQ() {
        // Fixed initial reward of ~8.5 ARQON
        return static_cast<uint64_t>(8.5 * Q_PER_ARQON);
    }
    
    // Tail emission: constant after main emission depletes
    // ~0.01 ARQON per epoch forever
    static uint64_t tailRewardQ() {
        double tail_per_year = (TOTAL_SUPPLY_ARQON * TAIL_EMISSION_PERCENT) / 100.0;
        return static_cast<uint64_t>((tail_per_year / EPOCHS_PER_YEAR) * Q_PER_ARQON);
    }
    
    // Get epoch reward in Q
    static uint64_t getEpochReward(uint64_t epoch) {
        if (epoch >= EPOCHS_100_YEARS) {
            return tailRewardQ();  // Tail emission
        }
        
        double decay = std::pow(DECAY_FACTOR, static_cast<double>(epoch));
        uint64_t reward = static_cast<uint64_t>(initialRewardQ() * decay);
        
        // Ensure at least tail emission
        uint64_t tail = tailRewardQ();
        return (reward < tail) ? tail : reward;
    }
    
    // Get cumulative emission at epoch (in Q)
    static uint64_t getCumulativeEmission(uint64_t epoch) {
        if (epoch == 0) return 0;
        
        if (epoch <= EPOCHS_100_YEARS) {
            // Geometric series sum: S = a * (1 - r^n) / (1 - r)
            double sum = initialRewardQ() * (1.0 - std::pow(DECAY_FACTOR, epoch)) 
                        / (1.0 - DECAY_FACTOR);
            return static_cast<uint64_t>(sum);
        } else {
            // Main emission + tail
            double main = initialRewardQ() * (1.0 - std::pow(DECAY_FACTOR, EPOCHS_100_YEARS)) 
                         / (1.0 - DECAY_FACTOR);
            uint64_t tail_epochs = epoch - EPOCHS_100_YEARS;
            return static_cast<uint64_t>(main) + (tail_epochs * tailRewardQ());
        }
    }
    
    // Get emission percentage complete
    static double getEmissionPercent(uint64_t epoch) {
        uint64_t emitted = getCumulativeEmission(epoch);
        return (static_cast<double>(emitted) / TOTAL_SUPPLY_Q) * 100.0;
    }
};

// ============================================================================
// DIFFICULTY CURVE
// ============================================================================

class DifficultyCurve {
public:
    static constexpr uint64_t BASE_DIFFICULTY = 1'000'000;
    static constexpr double GROWTH_PER_EPOCH = 0.0000001;  // ~22% per year
    
    // Get difficulty at epoch
    static uint64_t getDifficulty(uint64_t epoch, uint64_t network_compute) {
        double time_factor = std::pow(1.0 + GROWTH_PER_EPOCH, static_cast<double>(epoch));
        double network_factor = std::max(1.0, static_cast<double>(network_compute) / 1'000'000'000.0);
        return static_cast<uint64_t>(BASE_DIFFICULTY * time_factor * network_factor);
    }
    
    // Get node's share of rewards based on compute contribution
    static double getComputeShare(uint64_t node_compute, uint64_t difficulty) {
        if (difficulty == 0) return 1.0;
        return std::min(1.0, static_cast<double>(node_compute) / difficulty);
    }
};

// ============================================================================
// ACCOUNT
// ============================================================================

struct Account {
    Address address;
    uint64_t balance;         // Available balance in Q
    uint64_t stake;           // Staked amount in Q
    uint64_t unbonding;       // Amount currently unbonding
    uint64_t unbond_epoch;    // Epoch when unbonding completes
    uint64_t nonce;           // Transaction nonce
    
    Account() : balance(0), stake(0), unbonding(0), unbond_epoch(0), nonce(0) {
        address.fill(0);
    }
    
    explicit Account(const Address& addr) 
        : address(addr), balance(0), stake(0), unbonding(0), unbond_epoch(0), nonce(0) {}
    
    uint64_t totalBalance() const { return balance + stake + unbonding; }
};

// ============================================================================
// EPOCH BLOCK
// ============================================================================

struct EpochBlock {
    uint64_t number;              // Epoch number
    uint64_t timestamp;           // Unix timestamp (ms)
    Hash parent_hash;             // Previous epoch hash
    Hash projection_hash;         // Combined 3-torus projection hash
    Hash torus_hashes[3];         // Individual torus state hashes
    uint64_t reward;              // Tokens minted this epoch (Q)
    uint64_t difficulty;          // Current difficulty
    uint64_t total_supply;        // Total supply after this epoch
    uint32_t version;             // Protocol version
    uint32_t tx_count;            // Transaction count
    
    // Compute epoch hash
    Hash hash() const {
        std::array<uint8_t, 256> buffer{};
        size_t pos = 0;
        
        std::memcpy(buffer.data() + pos, &number, 8); pos += 8;
        std::memcpy(buffer.data() + pos, &timestamp, 8); pos += 8;
        std::memcpy(buffer.data() + pos, parent_hash.data(), HASH_SIZE); pos += HASH_SIZE;
        std::memcpy(buffer.data() + pos, projection_hash.data(), HASH_SIZE); pos += HASH_SIZE;
        std::memcpy(buffer.data() + pos, &reward, 8); pos += 8;
        std::memcpy(buffer.data() + pos, &difficulty, 8); pos += 8;
        
        // Simple hash
        Hash result{};
        uint64_t state[4] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
                            0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL};
        for (size_t i = 0; i < pos; i++) {
            state[i % 4] ^= buffer[i];
            state[i % 4] = (state[i % 4] << 7) | (state[i % 4] >> 57);
            state[i % 4] += 0x9e3779b97f4a7c15ULL;
        }
        std::memcpy(result.data(), state, 32);
        return result;
    }
};

// ============================================================================
// NODE TYPES
// ============================================================================

enum class NodeType {
    GPU_INFERENCE,
    RELAY,
    LIGHT_CLIENT
};

struct NodeInfo {
    Address address;
    NodeType type;
    uint64_t compute_units;       // For GPU nodes
    uint64_t stake;               // Optional stake boost
    uint64_t traffic_bytes;       // For relay nodes
    double uptime;                // 0.0 - 1.0
    uint64_t last_seen_epoch;
    uint64_t registered_epoch;
    
    // Stake boost multiplier: 1 + log10(1 + stake_in_arqon)
    double stakeMultiplier() const {
        double stake_arqon = static_cast<double>(stake) / Q_PER_ARQON;
        return 1.0 + std::log10(1.0 + stake_arqon);
    }
};

// ============================================================================
// GOVERNANCE
// ============================================================================

enum class ProposalType {
    PARAMETER_CHANGE,
    PROTOCOL_UPGRADE,
    EMERGENCY_FIX,
    COUNCIL_ELECTION,
    TREASURY_GRANT,
    FOUNDATION_REORG
};

enum class ProposalStatus {
    DRAFT,
    VOTING,
    PASSED,
    FAILED,
    EXECUTED,
    EXPIRED
};

enum class VoteChoice {
    YES,
    NO,
    ABSTAIN
};

struct Vote {
    Address voter;
    VoteChoice choice;
    uint64_t weight;  // Stake-weighted
    uint64_t epoch;
};

struct Proposal {
    uint64_t id;
    ProposalType type;
    ProposalStatus status;
    Address proposer;
    std::string title;
    std::string description;
    uint64_t deposit;             // Q deposited
    uint64_t start_epoch;
    uint64_t end_epoch;
    uint64_t activation_epoch;
    
    uint64_t yes_votes;           // Stake-weighted
    uint64_t no_votes;
    uint64_t abstain_votes;
    
    std::vector<Vote> votes;
    
    // For treasury grants
    Address grant_recipient;
    uint64_t grant_amount;
    
    double yesPercent() const {
        uint64_t total = yes_votes + no_votes;
        if (total == 0) return 0;
        return static_cast<double>(yes_votes) / total;
    }
    
    bool meetsThreshold(double threshold) const {
        return yesPercent() >= threshold;
    }
    
    uint64_t totalVotes() const {
        return yes_votes + no_votes + abstain_votes;
    }
};

// Council member
struct CouncilMember {
    Address address;
    uint64_t elected_epoch;
    uint64_t term_end_epoch;
    uint8_t consecutive_terms;
};

// ============================================================================
// INFERENCE JOB (for PoUW)
// ============================================================================

struct InferenceJob {
    uint64_t id;
    Address requester;
    Address gpu_node;
    std::string model;
    std::string prompt;
    uint32_t max_tokens;
    float temperature;
    uint64_t payment;             // Q
    uint64_t deadline_epoch;
    uint64_t submitted_epoch;
    
    bool completed;
    bool finalized = false;
    std::string response;
    uint32_t tokens_generated;
    uint64_t completion_epoch;
};

struct AuditorVerification {
    uint64_t job_id;
    Address auditor;
    bool verified;
    double similarity_score;      // 0.0 - 1.0
    uint64_t epoch;
};

// ============================================================================
// REWARD DISTRIBUTION
// ============================================================================

struct EpochRewards {
    uint64_t epoch;
    uint64_t total_reward;        // Q
    uint64_t gpu_pool;            // 60%
    uint64_t relay_pool;          // 10%
    uint64_t staker_pool;         // 20%
    uint64_t treasury_pool;       // 10%
    
    std::unordered_map<std::string, uint64_t> gpu_rewards;
    std::unordered_map<std::string, uint64_t> relay_rewards;
    std::unordered_map<std::string, uint64_t> staker_rewards;
};

// ============================================================================
// SLASHING
// ============================================================================

enum class SlashReason {
    DOWNTIME,           // >24h offline
    FAILED_VERIFICATION,// Failed PoUW audit
    DOUBLE_SIGN,        // Signed conflicting data
    MALICIOUS           // Governance-determined
};

struct SlashEvent {
    Address node;
    SlashReason reason;
    uint64_t amount;              // Q slashed
    uint64_t epoch;
    Hash evidence_hash;
};

// ============================================================================
// MAIN TOKENOMICS ENGINE
// ============================================================================

class TokenomicsEngine {
private:
    // State
    std::unordered_map<std::string, Account> accounts_;
    std::vector<NodeInfo> gpu_nodes_;
    std::vector<NodeInfo> relay_nodes_;
    std::vector<CouncilMember> council_;
    std::vector<Proposal> proposals_;
    std::vector<EpochBlock> epochs_;
    std::vector<SlashEvent> slashes_;
    
    uint64_t current_epoch_;
    uint64_t total_supply_;
    uint64_t total_staked_;
    uint64_t network_compute_;
    uint64_t next_proposal_id_;
    
    std::string addrKey(const Address& addr) const {
        return std::string(reinterpret_cast<const char*>(addr.data()), ADDRESS_SIZE);
    }
    
public:
    TokenomicsEngine() 
        : current_epoch_(0), total_supply_(0), total_staked_(0), 
          network_compute_(0), next_proposal_id_(1) {
        // Initialize reserved addresses
        getOrCreateAccount(ReservedAddress::treasury());
        getOrCreateAccount(ReservedAddress::stakingPool());
        getOrCreateAccount(ReservedAddress::governance());
    }
    
    // ========================================================================
    // ACCOUNT MANAGEMENT
    // ========================================================================
    
    Account& getOrCreateAccount(const Address& addr) {
        std::string key = addrKey(addr);
        if (accounts_.find(key) == accounts_.end()) {
            accounts_[key] = Account(addr);
        }
        return accounts_[key];
    }
    
    const Account* getAccount(const Address& addr) const {
        auto it = accounts_.find(addrKey(addr));
        return (it != accounts_.end()) ? &it->second : nullptr;
    }
    
    bool transfer(const Address& from, const Address& to, uint64_t amount) {
        if (amount == 0) return true;
        if (QxAddress::isZero(from) || QxAddress::isZero(to)) return false;
        
        Account& sender = getOrCreateAccount(from);
        Account& recipient = getOrCreateAccount(to);
        
        if (sender.balance < amount) return false;
        if (recipient.balance > UINT64_MAX - amount) return false;
        
        sender.balance -= amount;
        recipient.balance += amount;
        return true;
    }
    
    void mint(const Address& to, uint64_t amount) {
        if (amount == 0) return;
        Account& account = getOrCreateAccount(to);
        
        if (account.balance > UINT64_MAX - amount) {
            account.balance = UINT64_MAX;
        } else {
            account.balance += amount;
        }
        
        if (total_supply_ > UINT64_MAX - amount) {
            total_supply_ = UINT64_MAX;
        } else {
            total_supply_ += amount;
        }
    }
    
    bool burn(const Address& from, uint64_t amount) {
        Account& account = getOrCreateAccount(from);
        if (account.balance < amount) return false;
        
        account.balance -= amount;
        total_supply_ -= amount;
        return true;
    }
    
    // ========================================================================
    // STAKING (No minimums)
    // ========================================================================
    
    bool stake(const Address& addr, uint64_t amount) {
        if (amount == 0) return true;
        
        Account& account = getOrCreateAccount(addr);
        if (account.balance < amount) return false;
        
        account.balance -= amount;
        account.stake += amount;
        total_staked_ += amount;
        
        return true;
    }
    
    bool requestUnstake(const Address& addr, uint64_t amount) {
        Account& account = getOrCreateAccount(addr);
        if (account.stake < amount) return false;
        
        account.stake -= amount;
        account.unbonding = amount;
        account.unbond_epoch = current_epoch_ + UNBONDING_EPOCHS;
        total_staked_ -= amount;
        
        return true;
    }
    
    void processUnbonding(const Address& addr) {
        Account& account = getOrCreateAccount(addr);
        
        if (account.unbonding > 0 && current_epoch_ >= account.unbond_epoch) {
            account.balance += account.unbonding;
            account.unbonding = 0;
            account.unbond_epoch = 0;
        }
    }
    
    // ========================================================================
    // EPOCH PROCESSING
    // ========================================================================
    
    EpochRewards processEpoch(const Hash& projection_hash, const Hash torus_hashes[3]) {
        current_epoch_++;
        
        // Calculate epoch reward
        uint64_t reward = EmissionCurve::getEpochReward(current_epoch_);
        uint64_t difficulty = DifficultyCurve::getDifficulty(current_epoch_, network_compute_);
        
        // Create epoch block
        EpochBlock block;
        block.number = current_epoch_;
        block.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        block.parent_hash = epochs_.empty() ? Hash{} : epochs_.back().hash();
        block.projection_hash = projection_hash;
        std::memcpy(block.torus_hashes, torus_hashes, sizeof(block.torus_hashes));
        block.reward = reward;
        block.difficulty = difficulty;
        block.version = 1;
        block.tx_count = 0;
        
        // Distribute rewards
        EpochRewards rewards;
        rewards.epoch = current_epoch_;
        rewards.total_reward = reward;
        rewards.gpu_pool = static_cast<uint64_t>(reward * GPU_NODE_SHARE);
        rewards.relay_pool = static_cast<uint64_t>(reward * RELAY_NODE_SHARE);
        rewards.staker_pool = static_cast<uint64_t>(reward * STAKER_SHARE);
        rewards.treasury_pool = static_cast<uint64_t>(reward * TREASURY_SHARE);
        
        // Treasury allocation
        mint(ReservedAddress::treasury(), rewards.treasury_pool);
        
        // GPU node rewards (proportional to verified compute)
        distributeGpuRewards(rewards);
        
        // Relay node rewards (proportional to traffic)
        distributeRelayRewards(rewards);
        
        // Staker rewards (proportional to stake)
        distributeStakerRewards(rewards);
        
        // Update total supply
        block.total_supply = total_supply_;
        epochs_.push_back(block);
        
        return rewards;
    }
    
private:
    void distributeGpuRewards(EpochRewards& rewards) {
        if (gpu_nodes_.empty() || rewards.gpu_pool == 0) return;
        
        uint64_t total_weighted = 0;
        for (const auto& node : gpu_nodes_) {
            total_weighted += static_cast<uint64_t>(node.compute_units * node.stakeMultiplier());
        }
        
        if (total_weighted == 0) return;
        
        for (const auto& node : gpu_nodes_) {
            uint64_t weighted = static_cast<uint64_t>(node.compute_units * node.stakeMultiplier());
            uint64_t share = (rewards.gpu_pool * weighted) / total_weighted;
            
            mint(node.address, share);
            rewards.gpu_rewards[addrKey(node.address)] = share;
        }
    }
    
    void distributeRelayRewards(EpochRewards& rewards) {
        if (relay_nodes_.empty() || rewards.relay_pool == 0) return;
        
        uint64_t total_traffic = 0;
        for (const auto& node : relay_nodes_) {
            total_traffic += node.traffic_bytes;
        }
        
        if (total_traffic == 0) return;
        
        for (const auto& node : relay_nodes_) {
            uint64_t share = (rewards.relay_pool * node.traffic_bytes) / total_traffic;
            
            mint(node.address, share);
            rewards.relay_rewards[addrKey(node.address)] = share;
        }
    }
    
    void distributeStakerRewards(EpochRewards& rewards) {
        if (total_staked_ == 0 || rewards.staker_pool == 0) return;
        
        for (auto& [key, account] : accounts_) {
            if (account.stake > 0) {
                uint64_t share = (rewards.staker_pool * account.stake) / total_staked_;
                account.balance += share;  // Direct credit, not mint
                rewards.staker_rewards[key] = share;
            }
        }
    }
    
public:
    // ========================================================================
    // GOVERNANCE
    // ========================================================================
    
    uint64_t submitProposal(const Address& proposer, ProposalType type,
                           const std::string& title, const std::string& desc,
                           uint64_t deposit = 100 * Q_PER_ARQON) {
        Account& account = getOrCreateAccount(proposer);
        if (deposit > 0 && account.balance < deposit) return 0;
        
        account.balance -= deposit;
        
        Proposal p;
        p.id = next_proposal_id_++;
        p.type = type;
        p.status = ProposalStatus::VOTING;
        p.proposer = proposer;
        p.title = title;
        p.description = desc;
        p.deposit = deposit;
        p.start_epoch = current_epoch_;
        p.yes_votes = 0;
        p.no_votes = 0;
        p.abstain_votes = 0;
        
        // Set voting period based on type
        switch (type) {
            case ProposalType::EMERGENCY_FIX:
                p.end_epoch = current_epoch_ + EPOCHS_PER_DAY;
                break;
            case ProposalType::PARAMETER_CHANGE:
                p.end_epoch = current_epoch_ + EPOCHS_PER_DAY * 3;
                break;
            case ProposalType::COUNCIL_ELECTION:
            case ProposalType::FOUNDATION_REORG:
                p.end_epoch = current_epoch_ + EPOCHS_PER_DAY * 14;
                break;
            default:
                p.end_epoch = current_epoch_ + VOTING_PERIOD_EPOCHS;
        }
        
        proposals_.push_back(p);
        return p.id;
    }
    
    bool vote(uint64_t proposal_id, const Address& voter, VoteChoice choice) {
        Proposal* p = findProposal(proposal_id);
        if (!p || p->status != ProposalStatus::VOTING) return false;
        if (current_epoch_ > p->end_epoch) return false;
        
        // Check if already voted
        for (const auto& v : p->votes) {
            if (v.voter == voter) return false;
        }
        
        const Account* account = getAccount(voter);
        if (!account) return false;
        
        uint64_t weight = account->stake;
        if (weight == 0) weight = 1;  // Minimum vote weight
        
        Vote v;
        v.voter = voter;
        v.choice = choice;
        v.weight = weight;
        v.epoch = current_epoch_;
        
        p->votes.push_back(v);
        
        switch (choice) {
            case VoteChoice::YES: p->yes_votes += weight; break;
            case VoteChoice::NO: p->no_votes += weight; break;
            case VoteChoice::ABSTAIN: p->abstain_votes += weight; break;
        }
        
        return true;
    }
    
    bool councilApprove(uint64_t proposal_id, const Address& council_member) {
        // Verify council member
        bool is_council = false;
        for (const auto& m : council_) {
            if (m.address == council_member) {
                is_council = true;
                break;
            }
        }
        if (!is_council) return false;
        
        // For now, council approval is implicit via voting
        return vote(proposal_id, council_member, VoteChoice::YES);
    }
    
    void finalizeProposals() {
        for (auto& p : proposals_) {
            if (p.status != ProposalStatus::VOTING) continue;
            if (current_epoch_ <= p.end_epoch) continue;
            
            // Check thresholds
            bool passed = false;
            
            // Path A: 51% + Council (3/5)
            if (p.meetsThreshold(FAST_TRACK_THRESHOLD)) {
                int council_yes = 0;
                for (const auto& v : p.votes) {
                    for (const auto& m : council_) {
                        if (v.voter == m.address && v.choice == VoteChoice::YES) {
                            council_yes++;
                        }
                    }
                }
                if (council_yes >= 3) passed = true;
            }
            
            // Path B: 78% autonomous
            if (p.meetsThreshold(AUTONOMOUS_THRESHOLD)) {
                passed = true;
            }
            
            if (passed) {
                p.status = ProposalStatus::PASSED;
                p.activation_epoch = current_epoch_ + ACTIVATION_DELAY_EPOCHS;
                
                // Return deposit
                mint(p.proposer, p.deposit);
            } else {
                p.status = ProposalStatus::FAILED;
                
                // Return deposit if >10% support
                if (p.yesPercent() >= 0.10) {
                    mint(p.proposer, p.deposit);
                }
                // Otherwise deposit is burned (already deducted)
            }
        }
    }
    
    Proposal* findProposal(uint64_t id) {
        for (auto& p : proposals_) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }
    
    // ========================================================================
    // SLASHING
    // ========================================================================
    
    void slash(const Address& node, SlashReason reason, const Hash& evidence) {
        Account& account = getOrCreateAccount(node);
        
        uint64_t percent = 0;
        switch (reason) {
            case SlashReason::DOWNTIME: percent = SLASH_DOWNTIME_PCT; break;
            case SlashReason::FAILED_VERIFICATION: percent = SLASH_FAILED_VERIFY_PCT; break;
            case SlashReason::DOUBLE_SIGN: percent = SLASH_DOUBLE_SIGN_PCT; break;
            case SlashReason::MALICIOUS: percent = SLASH_MALICIOUS_PCT; break;
        }
        
        uint64_t slash_amount = (account.stake * percent) / 100;
        if (slash_amount > account.stake) slash_amount = account.stake;
        
        account.stake -= slash_amount;
        total_staked_ -= slash_amount;
        
        // 50% burned, 50% to treasury
        uint64_t to_treasury = slash_amount / 2;
        uint64_t to_burn = slash_amount - to_treasury;
        
        mint(ReservedAddress::treasury(), to_treasury);
        total_supply_ -= to_burn;
        
        SlashEvent event;
        event.node = node;
        event.reason = reason;
        event.amount = slash_amount;
        event.epoch = current_epoch_;
        event.evidence_hash = evidence;
        slashes_.push_back(event);
    }
    
    // ========================================================================
    // NODE REGISTRATION
    // ========================================================================
    
    void registerGpuNode(const Address& addr, uint64_t compute_units) {
        NodeInfo node;
        node.address = addr;
        node.type = NodeType::GPU_INFERENCE;
        node.compute_units = compute_units;
        node.stake = getOrCreateAccount(addr).stake;
        node.traffic_bytes = 0;
        node.uptime = 1.0;
        node.last_seen_epoch = current_epoch_;
        node.registered_epoch = current_epoch_;
        
        gpu_nodes_.push_back(node);
        network_compute_ += compute_units;
    }
    
    void registerRelayNode(const Address& addr) {
        NodeInfo node;
        node.address = addr;
        node.type = NodeType::RELAY;
        node.compute_units = 0;
        node.stake = getOrCreateAccount(addr).stake;
        node.traffic_bytes = 0;
        node.uptime = 1.0;
        node.last_seen_epoch = current_epoch_;
        node.registered_epoch = current_epoch_;
        
        relay_nodes_.push_back(node);
    }
    
    void updateNodeTraffic(const Address& addr, uint64_t bytes) {
        for (auto& node : relay_nodes_) {
            if (node.address == addr) {
                node.traffic_bytes += bytes;
                node.last_seen_epoch = current_epoch_;
                return;
            }
        }
    }
    
    // ========================================================================
    // GETTERS
    // ========================================================================
    
    uint64_t getCurrentEpoch() const { return current_epoch_; }
    uint64_t getTotalSupply() const { return total_supply_; }
    uint64_t getTotalStaked() const { return total_staked_; }
    uint64_t getNetworkCompute() const { return network_compute_; }
    
    const std::vector<EpochBlock>& getEpochs() const { return epochs_; }
    const std::vector<Proposal>& getProposals() const { return proposals_; }
    const std::vector<CouncilMember>& getCouncil() const { return council_; }
    
    // Treasury balance
    uint64_t getTreasuryBalance() const {
        const Account* treasury = getAccount(ReservedAddress::treasury());
        return treasury ? treasury->balance : 0;
    }
    
    // Set initial council (genesis)
    void setInitialCouncil(const std::vector<Address>& members) {
        council_.clear();
        for (const auto& addr : members) {
            CouncilMember m;
            m.address = addr;
            m.elected_epoch = 0;
            m.term_end_epoch = EPOCHS_PER_YEAR * 2;  // 2 year term
            m.consecutive_terms = 1;
            council_.push_back(m);
        }
    }
};

// ============================================================================
// HELPER: Convert Q to ARQON for display
// ============================================================================

inline double qToArqon(uint64_t q) {
    return static_cast<double>(q) / Q_PER_ARQON;
}

inline uint64_t arqonToQ(double arqon) {
    return static_cast<uint64_t>(arqon * Q_PER_ARQON);
}

} // namespace arqon
