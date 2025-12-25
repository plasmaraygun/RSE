#pragma once
#include "../core/Crypto.h"
#include "../core/Economics.h"
#include <vector>
#include <thread>
#include <atomic>

using namespace crypto;
using namespace economics;

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "  FAIL: " << msg << std::endl; return false; } } while(0)

// Crypto Tests 1-20
bool test_crypto_01_keypair_generation() {
    KeyPair kp;
    TEST_ASSERT(kp.getPublicKey().size() == PUBLIC_KEY_SIZE, "Pubkey size");
    return true;
}

bool test_crypto_02_unique_keypairs() {
    KeyPair kp1, kp2;
    TEST_ASSERT(kp1.getPublicKey() != kp2.getPublicKey(), "Unique pubkeys");
    return true;
}

bool test_crypto_03_signature_basic() {
    KeyPair kp;
    std::vector<uint8_t> msg = {1, 2, 3, 4, 5};
    auto sig = kp.sign(msg.data(), msg.size());
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig), "Verify");
    return true;
}

bool test_crypto_04_signature_tamper() {
    KeyPair kp;
    std::vector<uint8_t> msg = {1, 2, 3};
    auto sig = kp.sign(msg.data(), msg.size());
    Signature tampered = sig;
    tampered[0] ^= 0xFF;
    TEST_ASSERT(!KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), tampered), "Tamper fail");
    return true;
}

bool test_crypto_05_wrong_pubkey() {
    KeyPair kp1, kp2;
    std::vector<uint8_t> msg = {1, 2, 3};
    auto sig = kp1.sign(msg.data(), msg.size());
    TEST_ASSERT(!KeyPair::verify(kp2.getPublicKey(), msg.data(), msg.size(), sig), "Wrong key");
    return true;
}

bool test_crypto_06_message_tamper() {
    KeyPair kp;
    std::vector<uint8_t> msg = {1, 2, 3};
    auto sig = kp.sign(msg.data(), msg.size());
    std::vector<uint8_t> bad = {1, 2, 4};
    TEST_ASSERT(!KeyPair::verify(kp.getPublicKey(), bad.data(), bad.size(), sig), "Msg tamper");
    return true;
}

bool test_crypto_07_empty_message() {
    KeyPair kp;
    std::vector<uint8_t> msg;
    auto sig = kp.sign(msg.data(), msg.size());
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig), "Empty OK");
    return true;
}

bool test_crypto_08_large_message() {
    KeyPair kp;
    std::vector<uint8_t> msg(100000, 0xAB);
    auto sig = kp.sign(msg.data(), msg.size());
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig), "Large OK");
    return true;
}

bool test_crypto_09_deterministic_address() {
    KeyPair kp;
    TEST_ASSERT(kp.getAddress() == kp.getAddress(), "Deterministic");
    return true;
}

bool test_crypto_10_address_size() {
    KeyPair kp;
    TEST_ASSERT(kp.getAddress().size() == ADDRESS_SIZE, "Address size");
    return true;
}

bool test_crypto_11_signature_consistency() {
    KeyPair kp;
    std::vector<uint8_t> msg = {1, 2, 3};
    auto sig1 = kp.sign(msg.data(), msg.size());
    auto sig2 = kp.sign(msg.data(), msg.size());
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig1), "Sig1");
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig2), "Sig2");
    return true;
}

bool test_crypto_12_multiple_signatures() {
    KeyPair kp;
    for (int i = 0; i < 50; i++) {
        std::vector<uint8_t> msg = {static_cast<uint8_t>(i)};
        auto sig = kp.sign(msg.data(), msg.size());
        TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig), "Multi");
    }
    return true;
}

bool test_crypto_13_binary_message() {
    KeyPair kp;
    std::vector<uint8_t> msg = {0x00, 0xFF, 0x7F, 0x80};
    auto sig = kp.sign(msg.data(), msg.size());
    TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig), "Binary");
    return true;
}

bool test_crypto_14_zero_signature_fails() {
    KeyPair kp;
    std::vector<uint8_t> msg = {1, 2, 3};
    Signature zero{};
    TEST_ASSERT(!KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), zero), "Zero sig");
    return true;
}

bool test_crypto_15_pubkey_not_zero() {
    KeyPair kp;
    PublicKey zero{};
    TEST_ASSERT(kp.getPublicKey() != zero, "Not zero");
    return true;
}

bool test_crypto_16_address_collision() {
    std::vector<Address> addrs;
    for (int i = 0; i < 100; i++) {
        KeyPair kp;
        for (const auto& a : addrs) TEST_ASSERT(a != kp.getAddress(), "Collision");
        addrs.push_back(kp.getAddress());
    }
    return true;
}

bool test_crypto_17_roundtrip() {
    for (int sz = 1; sz <= 500; sz += 50) {
        KeyPair kp;
        std::vector<uint8_t> msg(sz, 0x42);
        auto sig = kp.sign(msg.data(), msg.size());
        TEST_ASSERT(KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig), "RT");
    }
    return true;
}

bool test_crypto_18_bit_flip() {
    KeyPair kp;
    std::vector<uint8_t> msg = {1, 2, 3, 4, 5};
    auto sig = kp.sign(msg.data(), msg.size());
    for (size_t i = 0; i < 5; i++) {
        Signature t = sig;
        t[i] ^= 0x01;
        TEST_ASSERT(!KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), t), "Flip");
    }
    return true;
}

bool test_crypto_19_concurrent() {
    KeyPair kp;
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&kp, &ok, t]() {
            for (int i = 0; i < 10; i++) {
                std::vector<uint8_t> msg = {(uint8_t)t, (uint8_t)i};
                auto sig = kp.sign(msg.data(), msg.size());
                if (KeyPair::verify(kp.getPublicKey(), msg.data(), msg.size(), sig)) ok++;
            }
        });
    }
    for (auto& t : threads) t.join();
    TEST_ASSERT(ok == 40, "Concurrent");
    return true;
}

bool test_crypto_20_transaction_sign() {
    KeyPair sender, receiver;
    AccountManager accounts;
    accounts.mint(sender.getAddress(), 1000000);
    Transaction tx;
    tx.to = receiver.getAddress();
    tx.value = 100;
    tx.gas_price = 1;
    tx.gas_limit = 21000;
    tx.nonce = 0;
    tx.sign(sender);
    TEST_ASSERT(tx.verify(), "Tx verify");
    return true;
}
