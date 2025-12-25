#pragma once
#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../single_torus/BettiRDLKernel.h"
#include <thread>
#include <atomic>

using namespace crypto;
using namespace economics;

// Security Tests 86-100
bool test_sec_86_replay() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 1000; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = 0;
    tx.sign(alice);
    auto r1 = processor.process(tx);
    auto r2 = processor.process(tx);
    TEST_ASSERT(r1 == TransactionProcessor::Result::SUCCESS && r2 != TransactionProcessor::Result::SUCCESS, "Replay");
    return true;
}

bool test_sec_87_underflow() {
    AccountManager accounts;
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 100);
    TEST_ASSERT(!accounts.transfer(alice.getAddress(), bob.getAddress(), 200), "Underflow");
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).balance == 100, "Unchanged");
    return true;
}

bool test_sec_88_stake_no_balance() {
    AccountManager accounts;
    KeyPair user;
    TEST_ASSERT(!accounts.stake(user.getAddress(), MIN_STAKE), "NoBalance");
    return true;
}

bool test_sec_89_unstake_excess() {
    AccountManager accounts;
    KeyPair user;
    accounts.mint(user.getAddress(), MIN_STAKE);
    accounts.stake(user.getAddress(), MIN_STAKE);
    TEST_ASSERT(!accounts.unstake(user.getAddress(), MIN_STAKE * 2), "Excess");
    return true;
}

bool test_sec_90_unsigned_tx() {
    KeyPair alice, bob;
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 1000;
    tx.nonce = 0;
    TEST_ASSERT(!tx.verify(), "Unsigned");
    return true;
}

bool test_sec_91_forged_sig() {
    KeyPair alice, bob, mallory;
    AccountManager accounts;
    accounts.mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 1000; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = 0;
    tx.sign(mallory);
    TEST_ASSERT(!tx.verify(), "Forged");
    return true;
}

bool test_sec_92_nonce_gap() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 1000; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = 5;
    tx.sign(alice);
    TEST_ASSERT(processor.process(tx) != TransactionProcessor::Result::SUCCESS, "Gap");
    return true;
}

bool test_sec_93_extreme_gas() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 100; tx.gas_price = UINT64_MAX / 100000; tx.gas_limit = 21000; tx.nonce = 0;
    tx.sign(alice);
    TEST_ASSERT(processor.process(tx) != TransactionProcessor::Result::SUCCESS, "ExtremeGas");
    return true;
}

bool test_sec_94_zero_gas() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 100; tx.gas_price = 1; tx.gas_limit = 0; tx.nonce = 0;
    tx.sign(alice);
    TEST_ASSERT(processor.process(tx) != TransactionProcessor::Result::SUCCESS, "ZeroGas");
    return true;
}

bool test_sec_95_kernel_bounds() {
    BettiRDLKernel<32> kernel;
    TEST_ASSERT(!kernel.spawnProcess(INT32_MAX, 0, 0), "MaxInt");
    TEST_ASSERT(!kernel.spawnProcess(0, INT32_MIN, 0), "MinInt");
    return true;
}

bool test_sec_96_pool_exhaust() {
    BettiRDLKernel<32> kernel;
    int n = 0;
    for (int i = 0; i < 10000; i++) {
        if (kernel.spawnProcess(i%32, (i/32)%32, (i/1024)%32)) n++;
    }
    TEST_ASSERT(n > 0 && n < 10000, "Bounded");
    return true;
}

bool test_sec_97_concurrent_mint() {
    AccountManager accounts;
    KeyPair user;
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10; i++) if (accounts.mint(user.getAddress(), 100)) ok++;
        });
    }
    for (auto& t : threads) t.join();
    TEST_ASSERT(accounts.getAccount(user.getAddress()).balance == ok * 100, "ConcMint");
    return true;
}

bool test_sec_98_slash_protect() {
    AccountManager accounts;
    KeyPair v;
    accounts.mint(v.getAddress(), MIN_STAKE);
    accounts.stake(v.getAddress(), MIN_STAKE);
    for (int i = 0; i < 50; i++) accounts.slash(v.getAddress(), 10);
    TEST_ASSERT(accounts.getAccount(v.getAddress()).stake >= 0, "SlashProt");
    return true;
}

bool test_sec_99_validator_remove() {
    AccountManager accounts;
    KeyPair v;
    accounts.mint(v.getAddress(), MIN_STAKE);
    accounts.stake(v.getAddress(), MIN_STAKE);
    TEST_ASSERT(accounts.getValidators().size() == 1, "Added");
    accounts.unstake(v.getAddress(), MIN_STAKE);
    TEST_ASSERT(accounts.getValidators().size() == 0, "Removed");
    return true;
}

bool test_sec_100_full_stress() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    BettiRDLKernel<32> kernel;
    std::vector<KeyPair> users(50);
    for (auto& u : users) accounts.mint(u.getAddress(), 1000000);
    for (int i = 0; i < 50; i++) kernel.spawnProcess(i%32, (i/32)%32, 0);
    int tx_ok = 0;
    for (int i = 0; i < 50; i++) {
        Transaction tx;
        tx.to = users[(i+1)%50].getAddress();
        tx.value = 100; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = 0;
        tx.sign(users[i]);
        if (processor.process(tx) == TransactionProcessor::Result::SUCCESS) tx_ok++;
    }
    kernel.run(50);
    TEST_ASSERT(tx_ok > 25 && kernel.getProcessCount() == 50, "FullStress");
    return true;
}
