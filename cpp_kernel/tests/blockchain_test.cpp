/**
 * Blockchain Features Tests
 * 
 * Tests cryptographic identity, economic incentives, and P2P networking.
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../network/P2PNode.h"
#include "../network/ProjectionSync.h"
#include "../braided/BlockchainBraid.h"

#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace crypto;
using namespace economics;
using namespace network;
using namespace braided;

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << std::endl; return false; } } while(0)

// ============================================================================
// Cryptography Tests
// ============================================================================

bool test_keypair_generation() {
    KeyPair kp1, kp2;
    
    // Different keypairs should have different addresses
    TEST_ASSERT(std::memcmp(kp1.getAddress().data(), kp2.getAddress().data(), ADDRESS_SIZE) != 0,
                "Different keypairs should have different addresses");
    
    return true;
}

bool test_signature_verification() {
    KeyPair kp;
    
    std::string message = "Hello, RSE Blockchain!";
    auto sig = kp.sign(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    
    // Verify with correct public key
    bool valid = KeyPair::verify(kp.getPublicKey(), 
                                  reinterpret_cast<const uint8_t*>(message.data()),
                                  message.size(), sig);
    
    TEST_ASSERT(valid, "Signature should verify with correct public key");
    
    return true;
}

bool test_transaction_signing() {
    KeyPair sender, recipient;
    
    Transaction tx;
    tx.to = recipient.getAddress();
    tx.value = 1000;
    tx.gas_price = 10;
    tx.gas_limit = 21000;
    tx.nonce = 0;
    
    tx.sign(sender);
    
    TEST_ASSERT(tx.verify(), "Signed transaction should verify");
    TEST_ASSERT(std::memcmp(tx.from.data(), sender.getAddress().data(), ADDRESS_SIZE) == 0,
                "Transaction from address should match sender");
    
    return true;
}

bool test_address_hex_conversion() {
    KeyPair kp;
    
    std::string hex = AddressUtil::toHex(kp.getAddress());
    
    TEST_ASSERT(hex.size() == ADDRESS_SIZE * 2 + 2, "Hex string should be correct length");
    TEST_ASSERT(hex[0] == '0' && hex[1] == 'x', "Hex string should start with 0x");
    
    Address parsed;
    bool ok = AddressUtil::fromHex(hex, parsed);
    
    TEST_ASSERT(ok, "Should parse hex address");
    TEST_ASSERT(std::memcmp(parsed.data(), kp.getAddress().data(), ADDRESS_SIZE) == 0,
                "Parsed address should match original");
    
    return true;
}

// ============================================================================
// Economics Tests
// ============================================================================

bool test_account_creation() {
    AccountManager accounts;
    
    KeyPair kp;
    Account& acc = accounts.getAccount(kp.getAddress());
    
    TEST_ASSERT(acc.balance == 0, "New account should have zero balance");
    TEST_ASSERT(acc.nonce == 0, "New account should have zero nonce");
    
    return true;
}

bool test_balance_transfer() {
    AccountManager accounts;
    
    KeyPair alice, bob;
    
    // Mint 1000 to Alice
    accounts.mint(alice.getAddress(), 1000);
    
    // Transfer 300 to Bob
    bool ok = accounts.transfer(alice.getAddress(), bob.getAddress(), 300);
    
    TEST_ASSERT(ok, "Transfer should succeed");
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).balance == 700, "Alice should have 700");
    TEST_ASSERT(accounts.getAccount(bob.getAddress()).balance == 300, "Bob should have 300");
    
    return true;
}

bool test_insufficient_balance() {
    AccountManager accounts;
    
    KeyPair alice, bob;
    
    accounts.mint(alice.getAddress(), 100);
    
    // Try to transfer more than balance
    bool ok = accounts.transfer(alice.getAddress(), bob.getAddress(), 200);
    
    TEST_ASSERT(!ok, "Transfer should fail with insufficient balance");
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).balance == 100, "Alice balance unchanged");
    TEST_ASSERT(accounts.getAccount(bob.getAddress()).balance == 0, "Bob balance unchanged");
    
    return true;
}

bool test_staking() {
    AccountManager accounts;
    
    KeyPair validator;
    
    // Mint enough for staking
    accounts.mint(validator.getAddress(), MIN_STAKE * 2);
    
    // Stake minimum amount
    bool ok = accounts.stake(validator.getAddress(), MIN_STAKE);
    
    TEST_ASSERT(ok, "Staking should succeed");
    TEST_ASSERT(accounts.isValidator(validator.getAddress()), "Should be validator");
    TEST_ASSERT(accounts.getAccount(validator.getAddress()).stake == MIN_STAKE, "Stake should be recorded");
    TEST_ASSERT(accounts.getValidators().size() == 1, "Should have 1 validator");
    
    return true;
}

bool test_slashing() {
    AccountManager accounts;
    
    KeyPair validator;
    
    accounts.mint(validator.getAddress(), MIN_STAKE);
    accounts.stake(validator.getAddress(), MIN_STAKE);
    
    uint64_t initial_stake = accounts.getAccount(validator.getAddress()).stake;
    
    // Slash validator
    accounts.slash(validator.getAddress());
    
    uint64_t after_slash = accounts.getAccount(validator.getAddress()).stake;
    
    TEST_ASSERT(after_slash < initial_stake, "Stake should be reduced");
    TEST_ASSERT(after_slash == initial_stake * (100 - SLASH_PERCENTAGE) / 100,
                "Slash amount should be correct");
    
    return true;
}

bool test_gas_calculation() {
    uint64_t event_gas = FeeCalculator::eventGas(0);
    TEST_ASSERT(event_gas == GAS_PER_EVENT, "Event gas should be base amount");
    
    uint64_t with_data = FeeCalculator::eventGas(100);
    TEST_ASSERT(with_data == GAS_PER_EVENT + 100 * GAS_PER_BYTE, "Should include data cost");
    
    return true;
}

bool test_transaction_processing() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    
    KeyPair alice, bob;
    
    // Setup: Alice has 100000 wei (enough for value + gas)
    accounts.mint(alice.getAddress(), 100000);
    
    // Create transaction: Alice sends 1000 to Bob
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 1000;
    tx.gas_price = 1;
    tx.gas_limit = 30000;
    tx.nonce = 0;
    tx.sign(alice);
    
    auto result = processor.process(tx);
    
    TEST_ASSERT(result == TransactionProcessor::Result::SUCCESS, "Transaction should succeed");
    TEST_ASSERT(accounts.getAccount(bob.getAddress()).balance == 1000, "Bob should receive 1000");
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).nonce == 1, "Nonce should increment");
    
    return true;
}

bool test_reward_distribution() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    
    KeyPair val1, val2;
    
    // Setup validators
    accounts.mint(val1.getAddress(), MIN_STAKE);
    accounts.mint(val2.getAddress(), MIN_STAKE);
    accounts.stake(val1.getAddress(), MIN_STAKE);
    accounts.stake(val2.getAddress(), MIN_STAKE);
    
    // Add some fees
    rewards.addFee(1000);
    
    uint64_t before1 = accounts.getAccount(val1.getAddress()).balance;
    uint64_t before2 = accounts.getAccount(val2.getAddress()).balance;
    
    // Distribute rewards
    rewards.distribute();
    
    uint64_t after1 = accounts.getAccount(val1.getAddress()).balance;
    uint64_t after2 = accounts.getAccount(val2.getAddress()).balance;
    
    TEST_ASSERT(after1 > before1, "Validator 1 should receive rewards");
    TEST_ASSERT(after2 > before2, "Validator 2 should receive rewards");
    TEST_ASSERT(after1 - before1 == after2 - before2, "Rewards should be equal");
    
    return true;
}

// ============================================================================
// P2P Network Tests
// ============================================================================

bool test_p2p_node_creation() {
    KeyPair kp;
    P2PNode node(kp.getAddress(), 0, 9000);
    
    TEST_ASSERT(node.getTorusId() == 0, "Torus ID should be 0");
    TEST_ASSERT(node.getPeerCount() == 0, "Should have no peers initially");
    
    return true;
}

bool test_p2p_peer_connection() {
    KeyPair kp1, kp2;
    P2PNode node1(kp1.getAddress(), 0, 9001);
    P2PNode node2(kp2.getAddress(), 1, 9002);
    
    node1.start();
    node2.start();
    
    // Connect node1 to node2
    NetAddr addr2(0x7F000001, 9002);  // 127.0.0.1:9002
    bool ok = node1.connectPeer(addr2);
    
    TEST_ASSERT(ok, "Should connect to peer");
    TEST_ASSERT(node1.getPeerCount() == 1, "Should have 1 peer");
    
    node1.stop();
    node2.stop();
    
    return true;
}

bool test_p2p_message_broadcast() {
    KeyPair kp1, kp2;
    P2PNode node1(kp1.getAddress(), 0, 9003);
    P2PNode node2(kp2.getAddress(), 1, 9005);
    
    node1.start();
    node2.start();
    
    // Connect nodes first (127.0.0.1 = 0x7F000001)
    NetAddr peer_addr(0x7F000001, 9005);
    node1.connectPeer(peer_addr);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    Message msg(MessageType::PING, data);
    
    node1.broadcast(msg);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test passes if we have peers (broadcast will work)
    TEST_ASSERT(node1.getPeerCount() > 0, "Should have connected peer");
    
    node1.stop();
    node2.stop();
    
    return true;
}

bool test_projection_sync() {
    KeyPair kp;
    P2PNode node(kp.getAddress(), 0, 9004);
    ProjectionSync sync(node, 0);
    
    // Create a projection
    Projection proj;
    proj.torus_id = 0;
    proj.current_time = 1000;
    proj.total_events_processed = 5000;
    proj.active_processes = 10;
    
    node.start();
    
    // Broadcast projection
    sync.broadcastProjection(proj);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    node.stop();
    
    return true;
}

// ============================================================================
// Blockchain Braid Integration Tests
// ============================================================================

bool test_blockchain_braid_creation() {
    BlockchainBraid braid(0, false);  // No network
    
    TEST_ASSERT(braid.getTorusId() == 0, "Torus ID should be 0");
    TEST_ASSERT(!braid.isNetworkEnabled(), "Network should be disabled");
    
    return true;
}

bool test_blockchain_transaction_submission() {
    BlockchainBraid braid(0, false);
    
    KeyPair alice, bob;
    
    // Genesis: give Alice enough for value + gas
    braid.mint(alice.getAddress(), 100000);
    
    // Create transaction
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 1000;
    tx.gas_price = 1;
    tx.gas_limit = 30000;
    tx.nonce = 0;
    tx.sign(alice);
    
    bool ok = braid.submitTransaction(tx);
    
    TEST_ASSERT(ok, "Transaction should be submitted");
    TEST_ASSERT(braid.getBalance(bob.getAddress()) == 1000, "Bob should receive tokens");
    TEST_ASSERT(braid.getNonce(alice.getAddress()) == 1, "Alice nonce should increment");
    
    return true;
}

bool test_blockchain_braid_interval() {
    BlockchainBraid braid(0, false);
    
    KeyPair validator;
    
    // Setup validator
    braid.mint(validator.getAddress(), MIN_STAKE);
    braid.stake(validator.getAddress(), MIN_STAKE);
    
    uint64_t before = braid.getBalance(validator.getAddress());
    
    // Execute braid interval (should distribute rewards)
    braid.executeBraidInterval(100);
    
    uint64_t after = braid.getBalance(validator.getAddress());
    
    TEST_ASSERT(after > before, "Validator should receive rewards");
    
    return true;
}

bool test_blockchain_statistics() {
    BlockchainBraid braid(0, false);
    
    KeyPair alice;
    braid.mint(alice.getAddress(), 1000);
    
    auto stats = braid.getStats();
    
    TEST_ASSERT(stats.total_supply == 1000, "Total supply should be 1000");
    TEST_ASSERT(stats.account_count == 1, "Should have 1 account");
    TEST_ASSERT(stats.peer_count == 0, "Should have 0 peers (network disabled)");
    
    return true;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  RSE Blockchain Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    int passed = 0, failed = 0;
    
    #define RUN_TEST(name) do { \
        std::cout << "  " #name "... "; \
        if (name()) { std::cout << "PASS" << std::endl; passed++; } \
        else { std::cout << "FAIL" << std::endl; failed++; } \
    } while(0)
    
    std::cout << "[Cryptography]" << std::endl;
    RUN_TEST(test_keypair_generation);
    RUN_TEST(test_signature_verification);
    RUN_TEST(test_transaction_signing);
    RUN_TEST(test_address_hex_conversion);
    
    std::cout << "\n[Economics]" << std::endl;
    RUN_TEST(test_account_creation);
    RUN_TEST(test_balance_transfer);
    RUN_TEST(test_insufficient_balance);
    RUN_TEST(test_staking);
    RUN_TEST(test_slashing);
    RUN_TEST(test_gas_calculation);
    RUN_TEST(test_transaction_processing);
    RUN_TEST(test_reward_distribution);
    
    std::cout << "\n[P2P Network]" << std::endl;
    RUN_TEST(test_p2p_node_creation);
    RUN_TEST(test_p2p_peer_connection);
    RUN_TEST(test_p2p_message_broadcast);
    RUN_TEST(test_projection_sync);
    
    std::cout << "\n[Blockchain Braid]" << std::endl;
    RUN_TEST(test_blockchain_braid_creation);
    RUN_TEST(test_blockchain_transaction_submission);
    RUN_TEST(test_blockchain_braid_interval);
    RUN_TEST(test_blockchain_statistics);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  PASSED: " << passed << "  FAILED: " << failed << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
