#pragma once
#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../inference/InferenceNode.h"
#include <thread>
#include <atomic>

using namespace crypto;
using namespace economics;
using namespace inference;

// Economics Tests 21-40
bool test_econ_21_account_creation() {
    AccountManager accounts;
    KeyPair user;
    accounts.mint(user.getAddress(), 1000);
    TEST_ASSERT(accounts.getAccount(user.getAddress()).balance == 1000, "Mint");
    return true;
}

bool test_econ_22_transfer_basic() {
    AccountManager accounts;
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000);
    TEST_ASSERT(accounts.transfer(alice.getAddress(), bob.getAddress(), 300), "Transfer");
    TEST_ASSERT(accounts.getAccount(bob.getAddress()).balance == 300, "Bob");
    return true;
}

bool test_econ_23_transfer_insufficient() {
    AccountManager accounts;
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 100);
    TEST_ASSERT(!accounts.transfer(alice.getAddress(), bob.getAddress(), 200), "Overdraft");
    return true;
}

bool test_econ_24_stake_basic() {
    AccountManager accounts;
    KeyPair v;
    accounts.mint(v.getAddress(), MIN_STAKE * 2);
    TEST_ASSERT(accounts.stake(v.getAddress(), MIN_STAKE), "Stake");
    TEST_ASSERT(accounts.getAccount(v.getAddress()).stake == MIN_STAKE, "Staked");
    return true;
}

bool test_econ_25_stake_insufficient() {
    AccountManager accounts;
    KeyPair v;
    accounts.mint(v.getAddress(), 100);
    TEST_ASSERT(!accounts.stake(v.getAddress(), MIN_STAKE), "Under");
    return true;
}

bool test_econ_26_unstake_basic() {
    AccountManager accounts;
    KeyPair v;
    accounts.mint(v.getAddress(), MIN_STAKE * 2);
    accounts.stake(v.getAddress(), MIN_STAKE);
    TEST_ASSERT(accounts.unstake(v.getAddress(), MIN_STAKE / 2), "Unstake");
    return true;
}

bool test_econ_27_slash_validator() {
    AccountManager accounts;
    KeyPair v;
    accounts.mint(v.getAddress(), MIN_STAKE);
    accounts.stake(v.getAddress(), MIN_STAKE);
    uint64_t before = accounts.getAccount(v.getAddress()).stake;
    accounts.slash(v.getAddress());
    uint64_t after = accounts.getAccount(v.getAddress()).stake;
    TEST_ASSERT(after < before, "Slashed");
    return true;
}

bool test_econ_28_tx_processing() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    Transaction tx;
    tx.to = bob.getAddress();
    tx.value = 1000;
    tx.gas_price = 1;
    tx.gas_limit = 21000;
    tx.nonce = 0;
    tx.sign(alice);
    TEST_ASSERT(processor.process(tx) == TransactionProcessor::Result::SUCCESS, "TX");
    return true;
}

bool test_econ_29_double_spend() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob, charlie;
    accounts.mint(alice.getAddress(), 100000);
    Transaction tx1, tx2;
    tx1.to = bob.getAddress(); tx1.value = 80000; tx1.gas_price = 1; tx1.gas_limit = 21000; tx1.nonce = 0; tx1.sign(alice);
    tx2.to = charlie.getAddress(); tx2.value = 80000; tx2.gas_price = 1; tx2.gas_limit = 21000; tx2.nonce = 0; tx2.sign(alice);
    auto r1 = processor.process(tx1);
    auto r2 = processor.process(tx2);
    TEST_ASSERT(r1 == TransactionProcessor::Result::SUCCESS && r2 != TransactionProcessor::Result::SUCCESS, "Double");
    return true;
}

bool test_econ_30_nonce_sequence() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    for (uint64_t n = 0; n < 5; n++) {
        Transaction tx;
        tx.to = bob.getAddress(); tx.value = 100; tx.gas_price = 1; tx.gas_limit = 21000; tx.nonce = n; tx.sign(alice);
        TEST_ASSERT(processor.process(tx) == TransactionProcessor::Result::SUCCESS, "Seq");
    }
    return true;
}

bool test_econ_31_gas_exhaustion() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    TransactionProcessor processor(accounts, rewards);
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 100);
    Transaction tx;
    tx.to = bob.getAddress(); tx.value = 50; tx.gas_price = 10; tx.gas_limit = 21000; tx.nonce = 0; tx.sign(alice);
    TEST_ASSERT(processor.process(tx) != TransactionProcessor::Result::SUCCESS, "Gas");
    return true;
}

bool test_econ_32_zero_value() {
    AccountManager accounts;
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000);
    TEST_ASSERT(accounts.transfer(alice.getAddress(), bob.getAddress(), 0), "Zero");
    return true;
}

bool test_econ_33_self_transfer() {
    AccountManager accounts;
    KeyPair alice;
    accounts.mint(alice.getAddress(), 1000);
    accounts.transfer(alice.getAddress(), alice.getAddress(), 500);
    TEST_ASSERT(accounts.getAccount(alice.getAddress()).balance == 1000, "Self");
    return true;
}

bool test_econ_34_overflow() {
    AccountManager accounts;
    KeyPair user;
    accounts.mint(user.getAddress(), UINT64_MAX - 100);
    accounts.mint(user.getAddress(), 200);  // Should silently fail on overflow
    TEST_ASSERT(accounts.getAccount(user.getAddress()).balance == UINT64_MAX - 100, "Overflow protected");
    return true;
}

bool test_econ_35_reward_distribution() {
    AccountManager accounts;
    RewardDistributor rewards(accounts);
    KeyPair v;
    accounts.mint(v.getAddress(), MIN_STAKE);
    accounts.stake(v.getAddress(), MIN_STAKE);
    rewards.distributeBlockReward(v.getAddress());
    TEST_ASSERT(accounts.getAccount(v.getAddress()).balance > 0, "Reward");
    return true;
}

bool test_econ_36_validator_set() {
    AccountManager accounts;
    KeyPair v1, v2;
    accounts.mint(v1.getAddress(), MIN_STAKE); accounts.stake(v1.getAddress(), MIN_STAKE);
    accounts.mint(v2.getAddress(), MIN_STAKE); accounts.stake(v2.getAddress(), MIN_STAKE);
    TEST_ASSERT(accounts.getValidators().size() == 2, "Validators");
    return true;
}

bool test_econ_37_burn() {
    AccountManager accounts;
    KeyPair user;
    accounts.mint(user.getAddress(), 1000);
    accounts.burn(user.getAddress(), 300);
    TEST_ASSERT(accounts.getAccount(user.getAddress()).balance == 700, "Burn");
    return true;
}

bool test_econ_38_concurrent() {
    AccountManager accounts;
    KeyPair alice, bob;
    accounts.mint(alice.getAddress(), 1000000);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10; i++) accounts.transfer(alice.getAddress(), bob.getAddress(), 1);
        });
    }
    for (auto& t : threads) t.join();
    uint64_t total = accounts.getAccount(alice.getAddress()).balance + accounts.getAccount(bob.getAddress()).balance;
    TEST_ASSERT(total == 1000000, "Conservation");
    return true;
}

bool test_econ_39_currency() {
    TEST_ASSERT(arqonToQ(1.0) == Q_PER_ARQON, "ArqonToQ");
    TEST_ASSERT(qToArqon(Q_PER_ARQON) == 1.0, "QToArqon");
    return true;
}

bool test_econ_40_gas_calc() {
    TEST_ASSERT(GAS_PER_EVENT == 21000, "GasEvent");
    return true;
}
