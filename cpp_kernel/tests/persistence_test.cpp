/**
 * Persistence Layer Tests
 * 
 * Verifies real disk I/O for state snapshots and WAL.
 */

#include "../core/Persistence.h"
#include "../core/Crypto.h"
#include "../core/Economics.h"

#include <iostream>
#include <cassert>
#include <filesystem>

using namespace persistence;
using namespace crypto;
using namespace economics;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        return false; \
    } \
} while(0)

// Test 1: Save and load snapshot
bool test_snapshot_save_load() {
    std::cout << "Test 1: Snapshot save/load..." << std::endl;
    
    init_crypto();
    
    // Create test directory
    std::string test_dir = "/tmp/arqon_test_" + std::to_string(time(nullptr));
    std::filesystem::create_directories(test_dir);
    
    // Create accounts
    AccountManager accounts;
    KeyPair alice, bob, charlie;
    
    accounts.mint(alice.getAddress(), 1000000000);  // 1 ARQN
    accounts.mint(bob.getAddress(), 500000000);     // 0.5 ARQN
    accounts.mint(charlie.getAddress(), 250000000); // 0.25 ARQN
    
    accounts.transfer(alice.getAddress(), bob.getAddress(), 100000000);
    
    // Save snapshot (use non-CAS mode for this test)
    SnapshotManager snapshots(test_dir, false);  // content_addressed=false
    TEST_ASSERT(snapshots.saveSnapshot(accounts, 100), "Save snapshot failed");
    
    // Verify file exists
    std::string snapshot_path = test_dir + "/snapshot_100.arqs";
    TEST_ASSERT(std::filesystem::exists(snapshot_path), "Snapshot file not created");
    
    size_t file_size = std::filesystem::file_size(snapshot_path);
    std::cout << "  Snapshot size: " << file_size << " bytes" << std::endl;
    TEST_ASSERT(file_size > 0, "Snapshot file is empty");
    
    // Load into new account manager
    AccountManager loaded_accounts;
    uint64_t loaded_height;
    
    TEST_ASSERT(snapshots.loadSnapshot(loaded_accounts, loaded_height), "Load snapshot failed");
    TEST_ASSERT(loaded_height == 100, "Block height mismatch");
    
    // Verify balances
    auto alice_acc = loaded_accounts.getAccount(alice.getAddress());
    auto bob_acc = loaded_accounts.getAccount(bob.getAddress());
    auto charlie_acc = loaded_accounts.getAccount(charlie.getAddress());
    
    std::cout << "  Alice balance: " << alice_acc.balance << std::endl;
    std::cout << "  Bob balance: " << bob_acc.balance << std::endl;
    std::cout << "  Charlie balance: " << charlie_acc.balance << std::endl;
    
    TEST_ASSERT(alice_acc.balance == 900000000, "Alice balance mismatch");
    TEST_ASSERT(bob_acc.balance == 600000000, "Bob balance mismatch");
    TEST_ASSERT(charlie_acc.balance == 250000000, "Charlie balance mismatch");
    
    // Cleanup
    std::filesystem::remove_all(test_dir);
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 2: WAL append and replay
bool test_wal_operations() {
    std::cout << "Test 2: WAL operations..." << std::endl;
    
    init_crypto();
    
    std::string test_dir = "/tmp/arqon_wal_test_" + std::to_string(time(nullptr));
    std::filesystem::create_directories(test_dir);
    
    // Create WAL
    WriteAheadLog wal(test_dir);
    TEST_ASSERT(wal.open(), "WAL open failed");
    
    // Append entries
    KeyPair alice, bob;
    
    Transaction tx;
    tx.from = alice.getAddress();
    tx.to = bob.getAddress();
    tx.value = 1000;
    tx.gas_price = 10;
    tx.gas_limit = 21000;
    tx.nonce = 0;
    
    TEST_ASSERT(wal.appendBlockStart(1), "Block start failed");
    TEST_ASSERT(wal.appendTransaction(tx), "Transaction append failed");
    TEST_ASSERT(wal.appendTransaction(tx), "Transaction append failed");
    
    Hash block_hash{};
    TEST_ASSERT(wal.appendBlockCommit(1, block_hash), "Block commit failed");
    
    wal.close();
    
    // Verify WAL file exists
    std::string wal_path = test_dir + "/wal.log";
    TEST_ASSERT(std::filesystem::exists(wal_path), "WAL file not created");
    
    size_t wal_size = std::filesystem::file_size(wal_path);
    std::cout << "  WAL size: " << wal_size << " bytes" << std::endl;
    
    // Replay WAL
    WriteAheadLog wal2(test_dir);
    auto entries = wal2.replay();
    
    std::cout << "  Replayed entries: " << entries.size() << std::endl;
    TEST_ASSERT(entries.size() == 4, "Entry count mismatch");
    TEST_ASSERT(entries[0].type == WALEntryType::BLOCK_START, "First entry should be BLOCK_START");
    TEST_ASSERT(entries[1].type == WALEntryType::TRANSACTION, "Second entry should be TRANSACTION");
    TEST_ASSERT(entries[3].type == WALEntryType::BLOCK_COMMIT, "Last entry should be BLOCK_COMMIT");
    
    // Cleanup
    std::filesystem::remove_all(test_dir);
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 3: Snapshot pruning
bool test_snapshot_pruning() {
    std::cout << "Test 3: Snapshot pruning..." << std::endl;
    
    init_crypto();
    
    std::string test_dir = "/tmp/arqon_prune_test_" + std::to_string(time(nullptr));
    
    AccountManager accounts;
    KeyPair alice;
    accounts.mint(alice.getAddress(), 1000000000);
    
    SnapshotManager snapshots(test_dir, false);  // non-CAS for pruning test
    
    // Create 15 snapshots
    for (int i = 1; i <= 15; i++) {
        TEST_ASSERT(snapshots.saveSnapshot(accounts, i * 100), "Save failed");
    }
    
    auto heights = snapshots.listSnapshots();
    std::cout << "  Snapshots before prune: " << heights.size() << std::endl;
    TEST_ASSERT(heights.size() == 15, "Should have 15 snapshots");
    
    // Prune to keep only 5
    snapshots.pruneSnapshots(5);
    
    heights = snapshots.listSnapshots();
    std::cout << "  Snapshots after prune: " << heights.size() << std::endl;
    TEST_ASSERT(heights.size() == 5, "Should have 5 snapshots after prune");
    
    // Verify we kept the most recent ones
    TEST_ASSERT(heights[0] == 1100, "Should keep height 1100");
    TEST_ASSERT(heights[4] == 1500, "Should keep height 1500");
    
    // Cleanup
    std::filesystem::remove_all(test_dir);
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 4: Checksum verification
bool test_checksum_verification() {
    std::cout << "Test 4: Checksum verification..." << std::endl;
    
    init_crypto();
    
    std::string test_dir = "/tmp/arqon_checksum_test_" + std::to_string(time(nullptr));
    
    AccountManager accounts;
    KeyPair alice;
    accounts.mint(alice.getAddress(), 1000000000);
    
    SnapshotManager snapshots(test_dir, false);  // non-CAS for checksum test
    snapshots.saveSnapshot(accounts, 100);
    
    // Corrupt the snapshot file
    std::string path = test_dir + "/snapshot_100.arqs";
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    
    // Corrupt a byte in the middle
    file.seekp(50);
    char corrupt = 0xFF;
    file.write(&corrupt, 1);
    file.close();
    
    // Try to load - should fail checksum
    AccountManager loaded;
    uint64_t height;
    bool loaded_ok = snapshots.loadSnapshot(loaded, height, path);
    
    std::cout << "  Corrupted snapshot load: " << (loaded_ok ? "succeeded (BAD)" : "failed (GOOD)") << std::endl;
    TEST_ASSERT(!loaded_ok, "Corrupted snapshot should fail to load");
    
    // Cleanup
    std::filesystem::remove_all(test_dir);
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

int main() {
    std::cout << "=== Persistence Layer Tests ===" << std::endl;
    std::cout << "Testing real disk I/O for state snapshots." << std::endl;
    std::cout << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    if (test_snapshot_save_load()) passed++; else failed++;
    if (test_wal_operations()) passed++; else failed++;
    if (test_snapshot_pruning()) passed++; else failed++;
    if (test_checksum_verification()) passed++; else failed++;
    
    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    
    if (failed == 0) {
        std::cout << std::endl;
        std::cout << "✅ ALL TESTS PASSED - Real persistence verified!" << std::endl;
    }
    
    return failed;
}
