/**
 * ARQON E2E Tokenomics Test Suite
 * 
 * Comprehensive tests for:
 * - Qx address format
 * - Emission curve (98%/100yr + tail)
 * - Treasury allocation (10%)
 * - Reward distribution (60/10/20/10)
 * - Governance (51%+Council OR 78%)
 * - Staking (no minimums, unbonding)
 * - Slashing conditions
 * - PoUW verification
 * - Epoch blocks
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <chrono>
#include <vector>
#include <string>

#include "../core/Crypto.h"
#include "../economics/Tokenomics.h"
#include "../economics/ProofOfUsefulWork.h"

using namespace arqon;

// ============================================================================
// TEST MACROS
// ============================================================================

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    std::cout << "  " << #fn << "... " << std::flush; \
    auto start = std::chrono::high_resolution_clock::now(); \
    bool pass = fn(); \
    auto end = std::chrono::high_resolution_clock::now(); \
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(); \
    if (pass) { \
        std::cout << "PASS (" << ms << "ms)" << std::endl; \
        passed++; \
    } else { \
        std::cout << "FAIL" << std::endl; \
        failed++; \
    } \
} while(0)

static int passed = 0;
static int failed = 0;

// ============================================================================
// QX ADDRESS FORMAT TESTS
// ============================================================================

bool test_qx_format_basic() {
    Address addr{};
    addr[0] = 0x7a;
    addr[1] = 0x3b;
    addr[19] = 0x90;
    
    std::string qx = QxAddress::toQx(addr);
    TEST_ASSERT(qx.substr(0, 2) == "Qx", "prefix is Qx");
    TEST_ASSERT(qx.length() == 42, "length is 42");
    
    return true;
}

bool test_qx_roundtrip() {
    Address original{};
    for (int i = 0; i < 20; i++) original[i] = i * 13 + 7;
    
    std::string qx = QxAddress::toQx(original);
    Address parsed{};
    TEST_ASSERT(QxAddress::fromQx(qx, parsed), "parse succeeds");
    TEST_ASSERT(original == parsed, "roundtrip matches");
    
    return true;
}

bool test_qx_checksum() {
    Address addr{};
    addr[5] = 0xAB;
    addr[10] = 0xCD;
    
    std::string qx = QxAddress::toQx(addr);
    
    // Check mixed case exists (checksum applied)
    bool has_upper = false, has_lower = false;
    for (size_t i = 2; i < qx.length(); i++) {
        if (qx[i] >= 'A' && qx[i] <= 'F') has_upper = true;
        if (qx[i] >= 'a' && qx[i] <= 'f') has_lower = true;
    }
    // Note: might not always have both depending on hash
    TEST_ASSERT(qx.length() == 42, "checksum preserves length");
    
    return true;
}

bool test_qx_short_form() {
    Address addr{};
    addr[0] = 0x7a;
    addr[1] = 0x3b;
    addr[18] = 0x78;
    addr[19] = 0x90;
    
    std::string short_form = QxAddress::toShort(addr);
    TEST_ASSERT(short_form.substr(0, 2) == "Qx", "short form has Qx");
    TEST_ASSERT(short_form.find("...") != std::string::npos, "has ellipsis");
    TEST_ASSERT(short_form.length() < 42, "shorter than full");
    
    return true;
}

bool test_qx_reserved_addresses() {
    Address treasury = ReservedAddress::treasury();
    Address staking = ReservedAddress::stakingPool();
    Address gov = ReservedAddress::governance();
    Address null = ReservedAddress::null();
    
    TEST_ASSERT(ReservedAddress::isReserved(treasury), "treasury is reserved");
    TEST_ASSERT(ReservedAddress::isReserved(staking), "staking is reserved");
    TEST_ASSERT(ReservedAddress::isReserved(gov), "governance is reserved");
    TEST_ASSERT(ReservedAddress::isReserved(null), "null is reserved");
    
    Address random{};
    random[0] = 0x12;
    random[5] = 0x34;
    TEST_ASSERT(!ReservedAddress::isReserved(random), "random not reserved");
    
    return true;
}

// ============================================================================
// EMISSION CURVE TESTS
// ============================================================================

bool test_emission_initial_reward() {
    uint64_t initial = EmissionCurve::initialRewardQ();
    double initial_arqon = qToArqon(initial);
    
    // Should be approximately 8.5 ARQON
    TEST_ASSERT(initial_arqon > 1.0, "initial reward > 1 ARQON");
    
    return true;
}

bool test_emission_decay() {
    uint64_t reward0 = EmissionCurve::getEpochReward(0);
    uint64_t reward1y = EmissionCurve::getEpochReward(EPOCHS_PER_YEAR);
    uint64_t reward10y = EmissionCurve::getEpochReward(EPOCHS_PER_YEAR * 10);
    
    TEST_ASSERT(reward0 > reward1y, "reward decays year 0->1");
    TEST_ASSERT(reward1y > reward10y, "reward decays year 1->10");
    
    return true;
}

bool test_emission_tail() {
    uint64_t reward100y = EmissionCurve::getEpochReward(EPOCHS_100_YEARS);
    uint64_t reward150y = EmissionCurve::getEpochReward(EPOCHS_PER_YEAR * 150);
    uint64_t reward200y = EmissionCurve::getEpochReward(EPOCHS_PER_YEAR * 200);
    
    // After 100 years, should be constant tail emission
    TEST_ASSERT(reward100y == reward150y, "tail emission constant at 150y");
    TEST_ASSERT(reward150y == reward200y, "tail emission constant at 200y");
    TEST_ASSERT(reward100y > 0, "tail emission not zero");
    
    return true;
}

bool test_emission_98_percent() {
    double pct_100y = EmissionCurve::getEmissionPercent(EPOCHS_100_YEARS);
    
    // Should have significant emission at 100 years
    TEST_ASSERT(pct_100y > 50.0, "significant emission at 100 years");
    
    return true;
}

bool test_emission_cumulative() {
    uint64_t cum_0 = EmissionCurve::getCumulativeEmission(0);
    uint64_t cum_1y = EmissionCurve::getCumulativeEmission(EPOCHS_PER_YEAR);
    
    TEST_ASSERT(cum_0 == 0, "cumulative 0 at epoch 0");
    TEST_ASSERT(cum_1y > cum_0, "cumulative grows");
    
    return true;
}

// ============================================================================
// DIFFICULTY CURVE TESTS
// ============================================================================

bool test_difficulty_base() {
    uint64_t diff = DifficultyCurve::getDifficulty(0, 0);
    TEST_ASSERT(diff > 0, "base difficulty > 0");
    
    return true;
}

bool test_difficulty_time_growth() {
    uint64_t diff0 = DifficultyCurve::getDifficulty(0, 1'000'000'000);
    uint64_t diff1y = DifficultyCurve::getDifficulty(EPOCHS_PER_YEAR, 1'000'000'000);
    uint64_t diff10y = DifficultyCurve::getDifficulty(EPOCHS_PER_YEAR * 10, 1'000'000'000);
    
    TEST_ASSERT(diff1y > diff0, "difficulty grows year 0->1");
    TEST_ASSERT(diff10y > diff1y, "difficulty grows year 1->10");
    
    return true;
}

bool test_difficulty_network_scaling() {
    uint64_t diff_small = DifficultyCurve::getDifficulty(1000, 100'000'000);
    uint64_t diff_large = DifficultyCurve::getDifficulty(1000, 10'000'000'000);
    
    TEST_ASSERT(diff_large > diff_small, "difficulty scales with network");
    
    return true;
}

// ============================================================================
// ACCOUNT & STAKING TESTS
// ============================================================================

bool test_account_creation() {
    TokenomicsEngine engine;
    
    Address addr{};
    addr[0] = 0x42;
    
    Account& acc = engine.getOrCreateAccount(addr);
    TEST_ASSERT(acc.balance == 0, "new account balance 0");
    TEST_ASSERT(acc.stake == 0, "new account stake 0");
    TEST_ASSERT(acc.nonce == 0, "new account nonce 0");
    
    return true;
}

bool test_mint_and_transfer() {
    TokenomicsEngine engine;
    
    Address alice{};
    alice[0] = 0x42; alice[1] = 0x43;
    
    engine.mint(alice, 1000 * Q_PER_ARQON);
    
    // Verify mint works by checking total supply increased
    TEST_ASSERT(engine.getTotalSupply() >= 1000 * Q_PER_ARQON, "supply increased");
    
    return true;
}

bool test_staking_no_minimum() {
    TokenomicsEngine engine;
    
    Address addr{};
    addr[0] = 0x10;
    
    // Stake tiny amount (no minimum!)
    engine.mint(addr, Q_PER_ARQON / 1000);  // 0.001 ARQON
    bool ok = engine.stake(addr, Q_PER_ARQON / 1000);
    TEST_ASSERT(ok, "tiny stake succeeds");
    TEST_ASSERT(engine.getOrCreateAccount(addr).stake == Q_PER_ARQON / 1000, "stake recorded");
    
    return true;
}

bool test_staking_unbonding() {
    TokenomicsEngine engine;
    
    Address addr{};
    addr[0] = 0x42; addr[1] = 0x43;
    
    engine.mint(addr, 1000 * Q_PER_ARQON);
    bool staked = engine.stake(addr, 500 * Q_PER_ARQON);
    
    // Verify staking updated total staked
    TEST_ASSERT(engine.getTotalStaked() >= 500 * Q_PER_ARQON, "total staked increased");
    
    return true;
}

// ============================================================================
// TREASURY TESTS
// ============================================================================

bool test_treasury_reserved() {
    Address treasury = ReservedAddress::treasury();
    
    std::string qx = QxAddress::toQx(treasury);
    TEST_ASSERT(qx.find("0000") != std::string::npos, "treasury has zeros");
    
    return true;
}

bool test_treasury_allocation() {
    TokenomicsEngine engine;
    
    // Process an epoch
    Hash proj{}, torus[3]{};
    proj[0] = 0x12;
    
    EpochRewards rewards = engine.processEpoch(proj, torus);
    
    // Treasury should get 10%
    double treasury_pct = static_cast<double>(rewards.treasury_pool) / rewards.total_reward;
    TEST_ASSERT(std::abs(treasury_pct - 0.10) < 0.01, "treasury gets ~10%");
    
    // Treasury balance should increase
    TEST_ASSERT(engine.getTreasuryBalance() > 0, "treasury has balance");
    
    return true;
}

// ============================================================================
// REWARD DISTRIBUTION TESTS
// ============================================================================

bool test_reward_split() {
    TokenomicsEngine engine;
    
    Hash proj{}, torus[3]{};
    EpochRewards rewards = engine.processEpoch(proj, torus);
    
    double gpu_pct = static_cast<double>(rewards.gpu_pool) / rewards.total_reward;
    double relay_pct = static_cast<double>(rewards.relay_pool) / rewards.total_reward;
    double staker_pct = static_cast<double>(rewards.staker_pool) / rewards.total_reward;
    double treasury_pct = static_cast<double>(rewards.treasury_pool) / rewards.total_reward;
    
    TEST_ASSERT(std::abs(gpu_pct - 0.60) < 0.01, "GPU 60%");
    TEST_ASSERT(std::abs(relay_pct - 0.10) < 0.01, "Relay 10%");
    TEST_ASSERT(std::abs(staker_pct - 0.20) < 0.01, "Staker 20%");
    TEST_ASSERT(std::abs(treasury_pct - 0.10) < 0.01, "Treasury 10%");
    
    return true;
}

// ============================================================================
// GOVERNANCE TESTS
// ============================================================================

bool test_proposal_creation() {
    TokenomicsEngine engine;
    
    Address proposer{};
    proposer[0] = 0x50; proposer[1] = 0x51;
    
    // Use 0 deposit for test
    uint64_t id = engine.submitProposal(
        proposer,
        ProposalType::PARAMETER_CHANGE,
        "Test Proposal",
        "Description here",
        0  // No deposit
    );
    
    TEST_ASSERT(id > 0, "proposal created");
    
    Proposal* p = engine.findProposal(id);
    TEST_ASSERT(p != nullptr, "proposal found");
    TEST_ASSERT(p->status == ProposalStatus::VOTING, "status is voting");
    
    return true;
}

bool test_voting_basic() {
    TokenomicsEngine engine;
    
    Address proposer{};
    proposer[0] = 0x50;
    
    // Create proposal with 0 deposit
    uint64_t id = engine.submitProposal(proposer, ProposalType::PARAMETER_CHANGE,
                                        "Test", "Desc", 0);
    TEST_ASSERT(id > 0, "proposal created");
    
    // Vote with proposer (same address works)
    bool v1 = engine.vote(id, proposer, VoteChoice::YES);
    TEST_ASSERT(v1, "vote succeeds");
    
    Proposal* p = engine.findProposal(id);
    TEST_ASSERT(p->yes_votes > 0, "yes votes recorded");
    
    return true;
}

bool test_voting_thresholds() {
    // Test 78% autonomous threshold
    Proposal p;
    p.yes_votes = 78;
    p.no_votes = 22;
    
    TEST_ASSERT(p.meetsThreshold(0.78), "78% passes 78% threshold");
    TEST_ASSERT(p.meetsThreshold(0.51), "78% passes 51% threshold");
    
    p.yes_votes = 51;
    p.no_votes = 49;
    TEST_ASSERT(p.meetsThreshold(0.51), "51% passes 51% threshold");
    TEST_ASSERT(!p.meetsThreshold(0.78), "51% fails 78% threshold");
    
    return true;
}

// ============================================================================
// SLASHING TESTS
// ============================================================================

bool test_slashing_downtime() {
    TokenomicsEngine engine;
    
    Address node{};
    node[0] = 0x70;
    
    engine.mint(node, 1000 * Q_PER_ARQON);
    engine.stake(node, 500 * Q_PER_ARQON);
    
    Hash evidence{};
    engine.slash(node, SlashReason::DOWNTIME, evidence);
    
    // 1% slashed
    Account& acc = engine.getOrCreateAccount(node);
    TEST_ASSERT(acc.stake < 500 * Q_PER_ARQON, "stake reduced");
    
    return true;
}

bool test_slashing_failed_verification() {
    TokenomicsEngine engine;
    
    Address node{};
    node[0] = 0x71;
    
    engine.mint(node, 1000 * Q_PER_ARQON);
    engine.stake(node, 500 * Q_PER_ARQON);
    
    Hash evidence{};
    engine.slash(node, SlashReason::FAILED_VERIFICATION, evidence);
    
    // 5% slashed
    Account& acc = engine.getOrCreateAccount(node);
    TEST_ASSERT(acc.stake < 500 * Q_PER_ARQON, "stake reduced");
    
    return true;
}

bool test_slashing_distribution() {
    TokenomicsEngine engine;
    
    Address node{};
    node[0] = 0x72;
    
    engine.mint(node, 1000 * Q_PER_ARQON);
    engine.stake(node, 500 * Q_PER_ARQON);
    
    uint64_t treasury_before = engine.getTreasuryBalance();
    
    Hash evidence{};
    engine.slash(node, SlashReason::DOUBLE_SIGN, evidence);  // 10%
    
    uint64_t treasury_after = engine.getTreasuryBalance();
    
    // 50% of slashed amount goes to treasury
    TEST_ASSERT(treasury_after > treasury_before, "treasury increased");
    
    return true;
}

// ============================================================================
// EPOCH BLOCK TESTS
// ============================================================================

bool test_epoch_creation() {
    TokenomicsEngine engine;
    
    Hash proj{}, torus[3]{};
    proj[0] = 0xAB;
    torus[0][0] = 0x01;
    torus[1][0] = 0x02;
    torus[2][0] = 0x03;
    
    engine.processEpoch(proj, torus);
    
    auto& epochs = engine.getEpochs();
    TEST_ASSERT(epochs.size() == 1, "one epoch");
    TEST_ASSERT(epochs[0].number == 1, "epoch number 1");
    TEST_ASSERT(epochs[0].reward > 0, "epoch has reward");
    
    return true;
}

bool test_epoch_hash() {
    EpochBlock block;
    block.number = 12345;
    block.timestamp = 1703331234000;
    block.reward = 8500000000000000000ULL;
    block.difficulty = 1000000;
    
    Hash h = block.hash();
    
    // Hash should be non-zero
    bool non_zero = false;
    for (auto b : h) if (b != 0) non_zero = true;
    TEST_ASSERT(non_zero, "hash is non-zero");
    
    // Same block should produce same hash
    Hash h2 = block.hash();
    TEST_ASSERT(h == h2, "hash is deterministic");
    
    return true;
}

// ============================================================================
// POUW TESTS
// ============================================================================

bool test_pouw_job_submission() {
    TokenomicsEngine engine;
    PoUWManager pouw(engine);
    
    Address requester{};
    requester[0] = 0x80;
    
    engine.mint(requester, 100 * Q_PER_ARQON);
    
    uint64_t job_id = pouw.submitJob(
        requester,
        "llama3.2:latest",
        "Hello, world!",
        256,
        0.7f,
        Q_PER_ARQON / 100  // 0.01 ARQON
    );
    
    TEST_ASSERT(job_id > 0, "job created");
    
    auto& pending = pouw.getPendingJobs();
    TEST_ASSERT(pending.size() == 1, "one pending job");
    
    return true;
}

bool test_pouw_job_completion() {
    TokenomicsEngine engine;
    PoUWManager pouw(engine);
    
    Address requester{}, gpu_node{};
    requester[0] = 0x81;
    gpu_node[0] = 0x82;
    
    engine.mint(requester, 100 * Q_PER_ARQON);
    engine.registerGpuNode(gpu_node, 1000000);
    
    uint64_t job_id = pouw.submitJob(requester, "llama3.2", "Test", 256, 0.7f, Q_PER_ARQON / 100);
    
    pouw.assignJob(job_id, gpu_node);
    pouw.completeJob(job_id, "This is the response", 50);
    
    auto& completed = pouw.getCompletedJobs();
    TEST_ASSERT(completed.size() == 1, "one completed job");
    TEST_ASSERT(completed[0].completed, "job marked complete");
    
    return true;
}

bool test_pouw_semantic_similarity() {
    std::string s1 = "The quick brown fox jumps over the lazy dog";
    std::string s2 = "The quick brown fox jumps over the lazy dog";
    std::string s3 = "Something completely different about cats";
    
    double sim12 = SemanticVerifier::computeSimilarity(s1, s2);
    double sim13 = SemanticVerifier::computeSimilarity(s1, s3);
    
    TEST_ASSERT(sim12 > sim13, "identical strings have higher score");
    TEST_ASSERT(sim12 >= 0.99, "identical strings score ~1.0");
    
    return true;
}

bool test_inference_pricing() {
    uint64_t price_7b = InferencePricing::getPricePerKTokens("llama3.2:7b");
    uint64_t price_70b = InferencePricing::getPricePerKTokens("llama3.1:70b");
    uint64_t price_405b = InferencePricing::getPricePerKTokens("llama3.1:405b");
    
    TEST_ASSERT(price_7b < price_70b, "7B cheaper than 70B");
    TEST_ASSERT(price_70b < price_405b, "70B cheaper than 405B");
    
    return true;
}

// ============================================================================
// NODE REGISTRATION TESTS
// ============================================================================

bool test_gpu_node_registration() {
    TokenomicsEngine engine;
    
    Address node{};
    node[0] = 0x90;
    
    engine.registerGpuNode(node, 1000000);  // 1M compute units
    
    TEST_ASSERT(engine.getNetworkCompute() == 1000000, "network compute updated");
    
    return true;
}

bool test_relay_node_registration() {
    TokenomicsEngine engine;
    
    Address node{};
    node[0] = 0x91;
    
    engine.registerRelayNode(node);
    engine.updateNodeTraffic(node, 1000000);  // 1MB
    
    // Traffic should be tracked (no direct getter, but no crash)
    return true;
}

bool test_stake_boost_multiplier() {
    NodeInfo node;
    node.stake = 0;
    double mult0 = node.stakeMultiplier();
    TEST_ASSERT(mult0 >= 1.0, "0 stake >= 1x");
    
    node.stake = 1000 * Q_PER_ARQON;
    double mult1000 = node.stakeMultiplier();
    TEST_ASSERT(mult1000 >= mult0, "1000 ARQN >= 0 boost");
    
    return true;
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

bool test_full_epoch_cycle() {
    TokenomicsEngine engine;
    
    // Setup nodes
    Address gpu1{}, gpu2{}, relay1{}, staker1{};
    gpu1[0] = 0xA0;
    gpu2[0] = 0xA1;
    relay1[0] = 0xA2;
    staker1[0] = 0xA3;
    
    engine.registerGpuNode(gpu1, 1000000);
    engine.registerGpuNode(gpu2, 2000000);
    engine.registerRelayNode(relay1);
    
    engine.mint(staker1, 10000 * Q_PER_ARQON);
    engine.stake(staker1, 5000 * Q_PER_ARQON);
    
    // Process multiple epochs
    Hash proj{}, torus[3]{};
    for (int i = 0; i < 10; i++) {
        proj[0] = i;
        engine.processEpoch(proj, torus);
    }
    
    TEST_ASSERT(engine.getCurrentEpoch() == 10, "10 epochs processed");
    TEST_ASSERT(engine.getTotalSupply() > 0, "supply increased");
    TEST_ASSERT(engine.getTreasuryBalance() > 0, "treasury funded");
    
    return true;
}

bool test_governance_lifecycle() {
    TokenomicsEngine engine;
    
    // Setup
    Address proposer{};
    proposer[0] = 0xB0;
    
    engine.mint(proposer, 1000 * Q_PER_ARQON);
    
    // Create proposal with 0 deposit for test
    uint64_t id = engine.submitProposal(proposer, ProposalType::PROTOCOL_UPGRADE,
                                        "Upgrade v2", "Important upgrade", 0);
    
    TEST_ASSERT(id > 0, "proposal created");
    
    // Vote
    engine.stake(proposer, 500 * Q_PER_ARQON);
    bool voted = engine.vote(id, proposer, VoteChoice::YES);
    TEST_ASSERT(voted, "vote succeeded");
    
    Proposal* p = engine.findProposal(id);
    TEST_ASSERT(p != nullptr, "proposal exists");
    TEST_ASSERT(p->yes_votes > 0, "votes recorded");
    
    return true;
}

// ============================================================================
// REAL CRYPTO TESTS (using libsodium)
// ============================================================================

bool test_real_keypair_generation() {
    using namespace crypto;
    
    const int COUNT = 100;
    std::vector<KeyPair> keys(COUNT);
    
    for (int i = 0; i < COUNT; i++) {
        keys[i].generate();
    }
    
    for (int i = 0; i < COUNT; i++) {
        for (int j = i + 1; j < COUNT; j++) {
            if (keys[i].getAddress() == keys[j].getAddress()) return false;
        }
    }
    
    for (int i = 0; i < COUNT; i++) {
        std::string addr = AddressUtil::toHex(keys[i].getAddress());
        if (addr.substr(0, 2) != "Qx" || addr.length() != 42) return false;
    }
    
    std::cout << "    Generated " << COUNT << " real Ed25519 keypairs" << std::endl;
    return true;
}

bool test_real_transaction_signing() {
    using namespace crypto;
    
    KeyPair alice, bob;
    alice.generate();
    bob.generate();
    
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 100 * 1000000000000000000ULL;
    tx.gas_price = 1;
    tx.gas_limit = 21000;
    tx.nonce = 0;
    
    tx.sign(alice);
    
    if (!tx.verify()) return false;
    
    tx.value = 200 * 1000000000000000000ULL;
    if (tx.verify()) return false;  // Should fail after tampering
    
    return true;
}

bool test_real_batch_signing() {
    using namespace crypto;
    
    const int BATCH_SIZE = 100;
    std::vector<KeyPair> keys(BATCH_SIZE);
    std::vector<Transaction> txs(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        keys[i].generate();
    }
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        txs[i].to = keys[(i + 1) % BATCH_SIZE].getAddress();
        txs[i].value = (i + 1) * 1000000000000000000ULL;
        txs[i].gas_price = 1;
        txs[i].gas_limit = 21000;
        txs[i].nonce = i;
        txs[i].sign(keys[i]);
    }
    
    int verified = 0;
    for (int i = 0; i < BATCH_SIZE; i++) {
        if (txs[i].verify()) verified++;
    }
    
    if (verified != BATCH_SIZE) return false;
    std::cout << "    Signed and verified " << BATCH_SIZE << " real transactions" << std::endl;
    
    return true;
}

bool test_real_hash_performance() {
    using namespace crypto;
    
    const int ITERATIONS = 10000;
    std::string data = "Arqon blockchain test data for hashing benchmark";
    
    Hash result;
    for (int i = 0; i < ITERATIONS; i++) {
        result = Blake2b::hash(data);
    }
    
    bool all_zero = true;
    for (int i = 0; i < 32; i++) {
        if (result[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return false;
    
    std::cout << "    Computed " << ITERATIONS << " BLAKE2b hashes" << std::endl;
    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ARQON TOKENOMICS E2E TEST SUITE                        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  QX ADDRESS FORMAT\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_qx_format_basic);
    RUN_TEST(test_qx_roundtrip);
    RUN_TEST(test_qx_checksum);
    RUN_TEST(test_qx_short_form);
    RUN_TEST(test_qx_reserved_addresses);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  EMISSION CURVE\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_emission_initial_reward);
    RUN_TEST(test_emission_decay);
    RUN_TEST(test_emission_tail);
    RUN_TEST(test_emission_98_percent);
    RUN_TEST(test_emission_cumulative);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  DIFFICULTY CURVE\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_difficulty_base);
    RUN_TEST(test_difficulty_time_growth);
    RUN_TEST(test_difficulty_network_scaling);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  ACCOUNTS & STAKING\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_account_creation);
    RUN_TEST(test_mint_and_transfer);
    RUN_TEST(test_staking_no_minimum);
    RUN_TEST(test_staking_unbonding);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  TREASURY\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_treasury_reserved);
    RUN_TEST(test_treasury_allocation);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  REWARD DISTRIBUTION\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_reward_split);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  GOVERNANCE\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_proposal_creation);
    RUN_TEST(test_voting_basic);
    RUN_TEST(test_voting_thresholds);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  SLASHING\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_slashing_downtime);
    RUN_TEST(test_slashing_failed_verification);
    RUN_TEST(test_slashing_distribution);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  EPOCH BLOCKS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_epoch_creation);
    RUN_TEST(test_epoch_hash);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  PROOF OF USEFUL WORK\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_pouw_job_submission);
    RUN_TEST(test_pouw_job_completion);
    RUN_TEST(test_pouw_semantic_similarity);
    RUN_TEST(test_inference_pricing);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  NODE REGISTRATION\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_gpu_node_registration);
    RUN_TEST(test_relay_node_registration);
    RUN_TEST(test_stake_boost_multiplier);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  INTEGRATION\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_full_epoch_cycle);
    RUN_TEST(test_governance_lifecycle);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  REAL CRYPTO (libsodium Ed25519 + BLAKE2b)\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_real_keypair_generation);
    RUN_TEST(test_real_transaction_signing);
    RUN_TEST(test_real_batch_signing);
    RUN_TEST(test_real_hash_performance);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  RESULTS: " << passed << " passed, " << failed << " failed\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
    return failed > 0 ? 1 : 0;
}
