#pragma once

#include "Crypto.h"
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>

/**
 * Economic Incentives System for RSE
 * 
 * Currency: Arqon (ARQN)
 * Base Unit: Q (1 Arqon = 10^9 Q)
 * 
 * Implements:
 * - Gas metering and fees
 * - Account balances (in Q)
 * - Staking for validators
 * - Reward distribution
 * - Slashing for misbehavior
 */

namespace economics {

using namespace crypto;

// ============================================================================
// Gas Constants
// ============================================================================

constexpr uint64_t GAS_PER_EVENT = 21000;           // Base gas for event
constexpr uint64_t GAS_PER_BYTE = 68;               // Gas per byte of data
constexpr uint64_t GAS_PER_PROCESS_SPAWN = 32000;   // Gas to spawn process
constexpr uint64_t GAS_PER_EDGE_CREATE = 5000;      // Gas to create edge

constexpr uint64_t MIN_GAS_PRICE = 1;               // Minimum gas price (Q)
constexpr uint64_t MIN_STAKE = 1000000000;          // Minimum stake to be validator (1 Arqon)

constexpr uint64_t BLOCK_REWARD = 2000000000;       // Reward per braid interval (2 Arqon)

// Currency conversion
constexpr uint64_t Q_PER_ARQON = 1000000000ULL;     // 10^9 Q = 1 Arqon
constexpr uint64_t SLASH_PERCENTAGE = 10;           // Slash 10% of stake for misbehavior

// ============================================================================
// Account Manager
// ============================================================================

class AccountManager {
private:
    // Account storage (address -> account)
    std::unordered_map<std::string, Account> accounts_;
    
    // Validator set (addresses of staked validators)
    std::vector<Address> validators_;
    
    // Total supply
    uint64_t total_supply_;
    
    std::string addressKey(const Address& addr) const {
        return std::string(reinterpret_cast<const char*>(addr.data()), ADDRESS_SIZE);
    }
    
public:
    AccountManager() : total_supply_(0) {}
    
    // Get or create account
    Account& getAccount(const Address& addr) {
        std::string key = addressKey(addr);
        
        if (accounts_.find(key) == accounts_.end()) {
            accounts_[key] = Account(addr);
        }
        
        return accounts_[key];
    }
    
    const Account& getAccount(const Address& addr) const {
        std::string key = addressKey(addr);
        auto it = accounts_.find(key);
        
        static Account empty;
        if (it == accounts_.end()) return empty;
        
        return it->second;
    }
    
    // Check if account exists
    bool hasAccount(const Address& addr) const {
        return accounts_.find(addressKey(addr)) != accounts_.end();
    }
    
    // Transfer balance
    // Protected against overflow and underflow
    bool transfer(const Address& from, const Address& to, uint64_t amount) {
        if (amount == 0) return true;  // No-op
        if (AddressUtil::isZero(from) || AddressUtil::isZero(to)) {
            return false;
        }
        if (from == to) return true;  // Self-transfer is no-op
        
        Account& sender = getAccount(from);
        Account& recipient = getAccount(to);
        
        if (sender.balance < amount) {
            return false;  // Insufficient balance
        }
        
        // Check recipient overflow
        if (recipient.balance > UINT64_MAX - amount) {
            return false;  // Would overflow recipient
        }
        
        sender.balance -= amount;
        recipient.balance += amount;
        
        return true;
    }
    
    // Mint new tokens (for genesis or rewards)
    // Protected against overflow
    void mint(const Address& to, uint64_t amount) {
        if (amount == 0) return;
        
        Account& account = getAccount(to);
        
        // Check for overflow before adding
        if (account.balance > UINT64_MAX - amount) {
            account.balance = UINT64_MAX;  // Cap at max
        } else {
            account.balance += amount;
        }
        
        if (total_supply_ > UINT64_MAX - amount) {
            total_supply_ = UINT64_MAX;
        } else {
            total_supply_ += amount;
        }
    }
    
    // Burn tokens
    bool burn(const Address& from, uint64_t amount) {
        Account& account = getAccount(from);
        
        if (account.balance < amount) {
            return false;
        }
        
        account.balance -= amount;
        total_supply_ -= amount;
        
        return true;
    }
    
    // Stake tokens to become validator
    bool stake(const Address& addr, uint64_t amount) {
        if (amount < MIN_STAKE) {
            return false;
        }
        
        Account& account = getAccount(addr);
        
        if (account.balance < amount) {
            return false;
        }
        
        account.balance -= amount;
        account.stake += amount;
        
        // Add to validator set if not already present
        bool found = false;
        for (const auto& v : validators_) {
            if (std::memcmp(v.data(), addr.data(), ADDRESS_SIZE) == 0) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            validators_.push_back(addr);
        }
        
        return true;
    }
    
    // Unstake tokens
    bool unstake(const Address& addr, uint64_t amount) {
        Account& account = getAccount(addr);
        
        if (account.stake < amount) {
            return false;
        }
        
        account.stake -= amount;
        account.balance += amount;
        
        // Remove from validator set if stake falls below minimum
        if (account.stake < MIN_STAKE) {
            validators_.erase(
                std::remove_if(validators_.begin(), validators_.end(),
                    [&addr](const Address& v) {
                        return std::memcmp(v.data(), addr.data(), ADDRESS_SIZE) == 0;
                    }),
                validators_.end()
            );
        }
        
        return true;
    }
    
    // Slash validator for misbehavior
    void slash(const Address& addr) {
        Account& account = getAccount(addr);
        
        uint64_t slash_amount = (account.stake * SLASH_PERCENTAGE) / 100;
        
        if (slash_amount > 0) {
            account.stake -= slash_amount;
            total_supply_ -= slash_amount;  // Burned
            
            std::cerr << "[Economics] Slashed validator " << AddressUtil::toHex(addr)
                      << " for " << slash_amount << " wei" << std::endl;
        }
        
        // Remove from validator set if stake falls below minimum
        if (account.stake < MIN_STAKE) {
            validators_.erase(
                std::remove_if(validators_.begin(), validators_.end(),
                    [&addr](const Address& v) {
                        return std::memcmp(v.data(), addr.data(), ADDRESS_SIZE) == 0;
                    }),
                validators_.end()
            );
        }
    }
    
    // Get validator set
    const std::vector<Address>& getValidators() const {
        return validators_;
    }
    
    // Check if address is validator
    bool isValidator(const Address& addr) const {
        for (const auto& v : validators_) {
            if (std::memcmp(v.data(), addr.data(), ADDRESS_SIZE) == 0) {
                return true;
            }
        }
        return false;
    }
    
    // Get total supply
    uint64_t getTotalSupply() const {
        return total_supply_;
    }
    
    // Get account count
    size_t getAccountCount() const {
        return accounts_.size();
    }
    
    // Get validator count
    size_t getValidatorCount() const {
        return validators_.size();
    }
    
    // Get all accounts (for persistence)
    std::vector<std::pair<Address, Account>> getAllAccounts() const {
        std::vector<std::pair<Address, Account>> result;
        for (const auto& [key, acc] : accounts_) {
            result.push_back({acc.address, acc});
        }
        return result;
    }
    
    // Clear all accounts (for loading snapshot)
    void clear() {
        accounts_.clear();
        validators_.clear();
        total_supply_ = 0;
    }
};

// ============================================================================
// Gas Meter
// ============================================================================

class GasMeter {
private:
    uint64_t gas_used_;
    uint64_t gas_limit_;
    
public:
    GasMeter(uint64_t limit) : gas_used_(0), gas_limit_(limit) {}
    
    // Consume gas
    bool consume(uint64_t amount) {
        if (gas_used_ + amount > gas_limit_) {
            return false;  // Out of gas
        }
        
        gas_used_ += amount;
        return true;
    }
    
    // Get remaining gas
    uint64_t remaining() const {
        return gas_limit_ - gas_used_;
    }
    
    // Get used gas
    uint64_t used() const {
        return gas_used_;
    }
    
    // Reset meter
    void reset(uint64_t limit) {
        gas_used_ = 0;
        gas_limit_ = limit;
    }
};

// ============================================================================
// Fee Calculator
// ============================================================================

class FeeCalculator {
public:
    // Calculate gas cost for event
    static uint64_t eventGas(size_t data_size) {
        return GAS_PER_EVENT + (data_size * GAS_PER_BYTE);
    }
    
    // Calculate gas cost for process spawn
    static uint64_t spawnGas() {
        return GAS_PER_PROCESS_SPAWN;
    }
    
    // Calculate gas cost for edge creation
    static uint64_t edgeGas() {
        return GAS_PER_EDGE_CREATE;
    }
    
    // Calculate total fee (gas * gas_price)
    static uint64_t fee(uint64_t gas, uint64_t gas_price) {
        return gas * gas_price;
    }
};

// ============================================================================
// Reward Distributor
// ============================================================================

class RewardDistributor {
private:
    AccountManager& accounts_;
    
    // Accumulated fees for current interval
    uint64_t accumulated_fees_;
    
    // Validator rewards (address -> reward amount)
    std::unordered_map<std::string, uint64_t> pending_rewards_;
    
public:
    RewardDistributor(AccountManager& accounts) 
        : accounts_(accounts), accumulated_fees_(0) {}
    
    // Add transaction fee to pool
    void addFee(uint64_t fee) {
        accumulated_fees_ += fee;
    }
    
    // Distribute rewards to validators
    void distribute() {
        const auto& validators = accounts_.getValidators();
        
        if (validators.empty()) {
            // No validators, burn the fees
            return;
        }
        
        // Total reward = block reward + accumulated fees
        uint64_t total_reward = BLOCK_REWARD + accumulated_fees_;
        
        // Distribute equally among validators (could weight by stake)
        uint64_t reward_per_validator = total_reward / validators.size();
        
        for (const auto& addr : validators) {
            accounts_.mint(addr, reward_per_validator);
            
            std::string key(reinterpret_cast<const char*>(addr.data()), ADDRESS_SIZE);
            pending_rewards_[key] += reward_per_validator;
        }
        
        // Reset accumulated fees
        accumulated_fees_ = 0;
    }
    
    // Get pending reward for address
    uint64_t getPendingReward(const Address& addr) const {
        std::string key(reinterpret_cast<const char*>(addr.data()), ADDRESS_SIZE);
        auto it = pending_rewards_.find(key);
        
        if (it == pending_rewards_.end()) {
            return 0;
        }
        
        return it->second;
    }
    
    // Clear pending rewards (after claiming)
    void clearRewards() {
        pending_rewards_.clear();
    }
    
    uint64_t getAccumulatedFees() const {
        return accumulated_fees_;
    }
};

// ============================================================================
// Transaction Processor
// ============================================================================

class TransactionProcessor {
private:
    AccountManager& accounts_;
    RewardDistributor& rewards_;
    
public:
    TransactionProcessor(AccountManager& accounts, RewardDistributor& rewards)
        : accounts_(accounts), rewards_(rewards) {}
    
    // Process transaction
    enum class Result {
        SUCCESS,
        INVALID_SIGNATURE,
        INVALID_NONCE,
        INSUFFICIENT_BALANCE,
        OUT_OF_GAS,
        EXECUTION_FAILED
    };
    
    Result process(const Transaction& tx) {
        // Input validation
        if (tx.gas_limit == 0 || tx.gas_price == 0) {
            return Result::OUT_OF_GAS;
        }
        
        // Verify signature
        if (!tx.verify()) {
            return Result::INVALID_SIGNATURE;
        }
        
        // Get sender account
        Account& sender = accounts_.getAccount(tx.from);
        
        // Check nonce
        if (tx.nonce != sender.nonce) {
            return Result::INVALID_NONCE;
        }
        
        // Calculate gas cost with overflow protection
        uint64_t gas_cost = FeeCalculator::eventGas(tx.data_len);
        uint64_t max_fee = FeeCalculator::fee(tx.gas_limit, tx.gas_price);
        
        // Check for overflow in value + max_fee
        if (tx.value > UINT64_MAX - max_fee) {
            return Result::INSUFFICIENT_BALANCE;
        }
        
        // Check balance (value + max fee)
        if (sender.balance < tx.value + max_fee) {
            return Result::INSUFFICIENT_BALANCE;
        }
        
        // Check gas limit
        if (tx.gas_limit < gas_cost) {
            return Result::OUT_OF_GAS;
        }
        
        // Deduct max fee upfront
        sender.balance -= max_fee;
        
        // Execute transaction
        GasMeter meter(tx.gas_limit);
        
        if (!meter.consume(gas_cost)) {
            // Refund unused gas
            uint64_t refund = FeeCalculator::fee(meter.remaining(), tx.gas_price);
            sender.balance += refund;
            
            return Result::OUT_OF_GAS;
        }
        
        // Transfer value
        if (tx.value > 0) {
            if (!accounts_.transfer(tx.from, tx.to, tx.value)) {
                // Refund all gas
                sender.balance += max_fee;
                return Result::EXECUTION_FAILED;
            }
        }
        
        // Calculate actual fee
        uint64_t actual_fee = FeeCalculator::fee(meter.used(), tx.gas_price);
        uint64_t refund = max_fee - actual_fee;
        
        // Refund unused gas
        sender.balance += refund;
        
        // Add fee to reward pool
        rewards_.addFee(actual_fee);
        
        // Increment nonce
        sender.nonce++;
        
        return Result::SUCCESS;
    }
    
    static const char* resultString(Result r) {
        switch (r) {
            case Result::SUCCESS: return "SUCCESS";
            case Result::INVALID_SIGNATURE: return "INVALID_SIGNATURE";
            case Result::INVALID_NONCE: return "INVALID_NONCE";
            case Result::INSUFFICIENT_BALANCE: return "INSUFFICIENT_BALANCE";
            case Result::OUT_OF_GAS: return "OUT_OF_GAS";
            case Result::EXECUTION_FAILED: return "EXECUTION_FAILED";
            default: return "UNKNOWN";
        }
    }
};

// ============================================================================
// Economic System (combines all components)
// ============================================================================

class EconomicSystem {
private:
    AccountManager accounts_;
    RewardDistributor rewards_;
    TransactionProcessor processor_;
    
    uint64_t braid_interval_count_;
    
public:
    EconomicSystem() 
        : rewards_(accounts_),
          processor_(accounts_, rewards_),
          braid_interval_count_(0) {}
    
    AccountManager& accounts() { return accounts_; }
    RewardDistributor& rewards() { return rewards_; }
    TransactionProcessor& processor() { return processor_; }
    
    // Called at each braid interval
    void onBraidInterval() {
        braid_interval_count_++;
        rewards_.distribute();
    }
    
    // Genesis: create initial accounts with balances
    void genesis(const std::vector<std::pair<Address, uint64_t>>& initial_balances) {
        for (const auto& [addr, balance] : initial_balances) {
            accounts_.mint(addr, balance);
        }
    }
    
    uint64_t getBraidIntervalCount() const {
        return braid_interval_count_;
    }
};

} // namespace economics
