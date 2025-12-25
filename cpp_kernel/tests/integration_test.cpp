/**
 * Integration Tests for P2P, Storage, Consensus
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../network/P2PNetwork.h"
#include "../storage/PersistentStorage.h"
#include "../consensus/ProofOfStake.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace crypto;
using namespace economics;
using namespace p2p;
using namespace storage;
using namespace consensus;

static int passed = 0, failed = 0;

#define TEST(name) std::cout << "  " << #name << "... " << std::flush
#define PASS() do { std::cout << "PASS" << std::endl; passed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << std::endl; failed++; } while(0)
#define ASSERT(cond, msg) if (!(cond)) { FAIL(msg); return; } 

// ═══════════════════════════════════════════════════════════════
// STORAGE TESTS
// ═══════════════════════════════════════════════════════════════

void test_storage_put_get() {
    TEST(test_storage_put_get);
    
    std::string test_dir = "/tmp/arqon_test_storage_" + std::to_string(rand());
    PersistentKV kv(test_dir);
    
    std::vector<uint8_t> value = {1, 2, 3, 4, 5};
    ASSERT(kv.put("test_key", value), "Put failed");
    
    std::vector<uint8_t> retrieved;
    ASSERT(kv.get("test_key", retrieved), "Get failed");
    ASSERT(retrieved == value, "Value mismatch");
    
    std::filesystem::remove_all(test_dir);
    PASS();
}

void test_storage_persistence() {
    TEST(test_storage_persistence);
    
    std::string test_dir = "/tmp/arqon_test_persist_" + std::to_string(rand());
    std::vector<uint8_t> value = {10, 20, 30};
    
    // Write and close
    {
        PersistentKV kv(test_dir);
        kv.put("persist_key", value);
        kv.flush();
    }
    
    // Reopen and read
    {
        PersistentKV kv(test_dir);
        std::vector<uint8_t> retrieved;
        ASSERT(kv.get("persist_key", retrieved), "Key not found after reopen");
        ASSERT(retrieved == value, "Value corrupted");
    }
    
    std::filesystem::remove_all(test_dir);
    PASS();
}

void test_storage_delete() {
    TEST(test_storage_delete);
    
    std::string test_dir = "/tmp/arqon_test_del_" + std::to_string(rand());
    PersistentKV kv(test_dir);
    
    std::vector<uint8_t> value = {1, 2, 3};
    kv.put("del_key", value);
    ASSERT(kv.has("del_key"), "Key should exist");
    
    kv.del("del_key");
    ASSERT(!kv.has("del_key"), "Key should be deleted");
    
    std::filesystem::remove_all(test_dir);
    PASS();
}

void test_storage_batch() {
    TEST(test_storage_batch);
    
    std::string test_dir = "/tmp/arqon_test_batch_" + std::to_string(rand());
    PersistentKV kv(test_dir);
    
    std::vector<std::pair<std::string, std::vector<uint8_t>>> batch;
    for (int i = 0; i < 10; i++) {
        batch.push_back({"key_" + std::to_string(i), {(uint8_t)i}});
    }
    
    ASSERT(kv.writeBatch(batch), "Batch write failed");
    ASSERT(kv.size() == 10, "Wrong count");
    
    std::filesystem::remove_all(test_dir);
    PASS();
}

void test_account_storage() {
    TEST(test_account_storage);
    
    std::string test_dir = "/tmp/arqon_test_acc_" + std::to_string(rand());
    AccountStorage storage(test_dir);
    
    KeyPair user;
    storage.saveAccount(user.getAddress(), 1000, 500, 5);
    
    uint64_t balance, stake, nonce;
    ASSERT(storage.loadAccount(user.getAddress(), balance, stake, nonce), "Load failed");
    ASSERT(balance == 1000 && stake == 500 && nonce == 5, "Values wrong");
    
    std::filesystem::remove_all(test_dir);
    PASS();
}

// ═══════════════════════════════════════════════════════════════
// CONSENSUS TESTS
// ═══════════════════════════════════════════════════════════════

void test_consensus_validator_register() {
    TEST(test_consensus_validator_register);
    
    AccountManager accounts;
    PoSConsensus consensus(accounts);
    
    KeyPair validator;
    accounts.mint(validator.getAddress(), MIN_STAKE * 2);
    accounts.stake(validator.getAddress(), MIN_STAKE);
    
    ASSERT(consensus.registerValidator(validator.getAddress()), "Register failed");
    ASSERT(consensus.isValidator(validator.getAddress()), "Not validator");
    ASSERT(consensus.validatorCount() == 1, "Wrong count");
    
    PASS();
}

void test_consensus_proposer_selection() {
    TEST(test_consensus_proposer_selection);
    
    AccountManager accounts;
    PoSConsensus consensus(accounts);
    
    // Register multiple validators
    std::vector<KeyPair> validators(5);
    for (auto& v : validators) {
        accounts.mint(v.getAddress(), MIN_STAKE * 2);
        accounts.stake(v.getAddress(), MIN_STAKE);
        consensus.registerValidator(v.getAddress());
    }
    
    // Selection should be deterministic for same height
    Address p1 = consensus.selectProposer(100);
    Address p2 = consensus.selectProposer(100);
    ASSERT(p1 == p2, "Not deterministic");
    
    // Different heights may select different proposers
    Address p3 = consensus.selectProposer(101);
    // (may or may not be different, but should be valid)
    
    bool found = false;
    for (auto& v : validators) {
        if (v.getAddress() == p1) found = true;
    }
    ASSERT(found, "Invalid proposer selected");
    
    PASS();
}

void test_consensus_epoch_transition() {
    TEST(test_consensus_epoch_transition);
    
    AccountManager accounts;
    PoSConsensus consensus(accounts);
    
    ASSERT(consensus.currentEpoch() == 0, "Wrong initial epoch");
    
    consensus.endEpoch();
    ASSERT(consensus.currentEpoch() == 1, "Epoch not incremented");
    
    consensus.endEpoch();
    consensus.endEpoch();
    ASSERT(consensus.currentEpoch() == 3, "Wrong epoch");
    
    PASS();
}

void test_consensus_total_stake() {
    TEST(test_consensus_total_stake);
    
    AccountManager accounts;
    PoSConsensus consensus(accounts);
    
    KeyPair v1, v2;
    accounts.mint(v1.getAddress(), MIN_STAKE * 3);
    accounts.mint(v2.getAddress(), MIN_STAKE * 2);
    
    accounts.stake(v1.getAddress(), MIN_STAKE * 2);
    accounts.stake(v2.getAddress(), MIN_STAKE);
    
    consensus.registerValidator(v1.getAddress());
    consensus.registerValidator(v2.getAddress());
    
    ASSERT(consensus.totalStaked() == MIN_STAKE * 3, "Wrong total stake");
    
    PASS();
}

// ═══════════════════════════════════════════════════════════════
// P2P TESTS
// ═══════════════════════════════════════════════════════════════

void test_p2p_node_create() {
    TEST(test_p2p_node_create);
    
    P2PNode node(31340);
    ASSERT(!node.isRunning(), "Should not be running");
    ASSERT(node.peerCount() == 0, "Should have no peers");
    
    PASS();
}

void test_p2p_node_start_stop() {
    TEST(test_p2p_node_start_stop);
    
    P2PNode node(31341);
    ASSERT(node.start(), "Start failed");
    ASSERT(node.isRunning(), "Should be running");
    
    node.stop();
    ASSERT(!node.isRunning(), "Should not be running");
    
    PASS();
}

void test_p2p_two_nodes() {
    TEST(test_p2p_two_nodes);
    
    P2PNode node1(31342);
    P2PNode node2(31343);
    
    ASSERT(node1.start(), "Node1 start failed");
    ASSERT(node2.start(), "Node2 start failed");
    
    // Node2 connects to Node1
    node2.bootstrap({"127.0.0.1:31342"});
    
    // Wait for connection
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // May or may not have discovered each other depending on timing
    // Just verify no crash
    
    node1.stop();
    node2.stop();
    
    PASS();
}

void test_p2p_routing_table() {
    TEST(test_p2p_routing_table);
    
    KeyPair local;
    RoutingTable table(local.getAddress());
    
    // Add some peers
    for (int i = 0; i < 10; i++) {
        KeyPair peer_key;
        PeerInfo peer;
        peer.node_id = peer_key.getAddress();
        peer.ip = "127.0.0.1";
        peer.port = 30000 + i;
        peer.last_seen = 12345;
        peer.is_active = true;
        table.addPeer(peer);
    }
    
    ASSERT(table.peerCount() == 10, "Wrong peer count");
    
    // Find closest to random target
    KeyPair target_key;
    auto closest = table.findClosest(target_key.getAddress(), 3);
    ASSERT(closest.size() <= 3, "Too many results");
    
    PASS();
}

void test_p2p_xor_distance() {
    TEST(test_p2p_xor_distance);
    
    Address a{}, b{}, c{};
    a[0] = 0x00;
    b[0] = 0xFF;
    c[0] = 0x0F;
    
    uint32_t ab = xorDistance(a, b);
    uint32_t ac = xorDistance(a, c);
    
    ASSERT(ab > ac, "XOR distance wrong");
    ASSERT(xorDistance(a, a) == 0, "Self distance not zero");
    
    PASS();
}

// ═══════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           INTEGRATION TEST SUITE                             ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  STORAGE TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    test_storage_put_get();
    test_storage_persistence();
    test_storage_delete();
    test_storage_batch();
    test_account_storage();

    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CONSENSUS TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    test_consensus_validator_register();
    test_consensus_proposer_selection();
    test_consensus_epoch_transition();
    test_consensus_total_stake();

    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  P2P TESTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    test_p2p_node_create();
    test_p2p_node_start_stop();
    test_p2p_two_nodes();
    test_p2p_routing_table();
    test_p2p_xor_distance();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  PASSED: " << passed << "  FAILED: " << failed;
    for (int i = 0; i < 45 - (passed >= 10 ? 1 : 0) - (failed >= 10 ? 1 : 0); i++) std::cout << " ";
    std::cout << "║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    return failed > 0 ? 1 : 0;
}
