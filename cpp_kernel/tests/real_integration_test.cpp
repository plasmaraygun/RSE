/**
 * ARQON Real Integration Tests
 * 
 * These are REAL tests using actual cryptography, network calls, and timing.
 * No stubs, no mocks, no LARP.
 * 
 * Requires: libsodium, libcurl
 * Compile: g++ -std=c++20 -O2 -o real_integration_test real_integration_test.cpp -lsodium -lcurl
 */

#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>

// Real crypto
#include <sodium.h>

// Real HTTP
#include <curl/curl.h>

// ============================================================================
// TEST FRAMEWORK
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    if (!(cond)) { \
        std::cerr << "  [FAIL] " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }

#define RUN_TEST(fn) do { \
    auto start = std::chrono::high_resolution_clock::now(); \
    bool result = fn(); \
    auto end = std::chrono::high_resolution_clock::now(); \
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(); \
    if (result) { \
        std::cout << "  " << #fn << "... PASS (" << ms << "ms)" << std::endl; \
        tests_passed++; \
    } else { \
        std::cout << "  " << #fn << "... FAIL (" << ms << "ms)" << std::endl; \
        tests_failed++; \
    } \
} while(0)

// ============================================================================
// REAL ED25519 CRYPTO
// ============================================================================

struct RealKeyPair {
    unsigned char public_key[crypto_sign_PUBLICKEYBYTES];
    unsigned char secret_key[crypto_sign_SECRETKEYBYTES];
    
    void generate() {
        crypto_sign_keypair(public_key, secret_key);
    }
    
    std::string publicKeyHex() const {
        std::ostringstream ss;
        ss << "Qx";
        for (int i = 0; i < crypto_sign_PUBLICKEYBYTES && i < 20; i++) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)public_key[i];
        }
        return ss.str();
    }
};

struct RealSignature {
    unsigned char sig[crypto_sign_BYTES];
    
    bool sign(const unsigned char* msg, size_t msg_len, const unsigned char* secret_key) {
        unsigned long long sig_len;
        return crypto_sign_detached(sig, &sig_len, msg, msg_len, secret_key) == 0;
    }
    
    bool verify(const unsigned char* msg, size_t msg_len, const unsigned char* public_key) const {
        return crypto_sign_verify_detached(sig, msg, msg_len, public_key) == 0;
    }
    
    std::string hex() const {
        std::ostringstream ss;
        for (int i = 0; i < crypto_sign_BYTES; i++) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)sig[i];
        }
        return ss.str();
    }
};

// ============================================================================
// REAL HTTP CLIENT
// ============================================================================

static size_t write_response_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

struct HttpResponse {
    long status_code;
    std::string body;
    double total_time_ms;
    bool success;
    std::string error;
};

HttpResponse http_get(const std::string& url, int timeout_ms = 5000) {
    HttpResponse resp{};
    CURL* curl = curl_easy_init();
    
    if (!curl) {
        resp.success = false;
        resp.error = "Failed to init curl";
        return resp;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // For testing
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &resp.total_time_ms);
        resp.total_time_ms *= 1000;  // Convert to ms
        resp.success = true;
    } else {
        resp.success = false;
        resp.error = curl_easy_strerror(res);
    }
    
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse http_post(const std::string& url, const std::string& body, 
                       const std::string& content_type = "application/json",
                       int timeout_ms = 10000) {
    HttpResponse resp{};
    CURL* curl = curl_easy_init();
    
    if (!curl) {
        resp.success = false;
        resp.error = "Failed to init curl";
        return resp;
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &resp.total_time_ms);
        resp.total_time_ms *= 1000;
        resp.success = true;
    } else {
        resp.success = false;
        resp.error = curl_easy_strerror(res);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

// ============================================================================
// REAL CRYPTO TESTS
// ============================================================================

bool test_real_ed25519_keygen() {
    RealKeyPair kp;
    kp.generate();
    
    // Verify key is non-zero
    bool all_zero = true;
    for (int i = 0; i < crypto_sign_PUBLICKEYBYTES; i++) {
        if (kp.public_key[i] != 0) {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT(!all_zero, "public key should not be all zeros");
    
    // Verify address format
    std::string addr = kp.publicKeyHex();
    TEST_ASSERT(addr.substr(0, 2) == "Qx", "address starts with Qx");
    TEST_ASSERT(addr.length() == 42, "address is 42 chars (Qx + 40 hex)");
    
    return true;
}

bool test_real_ed25519_sign_verify() {
    RealKeyPair kp;
    kp.generate();
    
    // Sign a message
    std::string message = "Transfer 100 Q from Qx1234 to Qx5678";
    RealSignature sig;
    
    bool signed_ok = sig.sign(
        reinterpret_cast<const unsigned char*>(message.data()),
        message.size(),
        kp.secret_key
    );
    TEST_ASSERT(signed_ok, "signing should succeed");
    
    // Verify signature
    bool verified = sig.verify(
        reinterpret_cast<const unsigned char*>(message.data()),
        message.size(),
        kp.public_key
    );
    TEST_ASSERT(verified, "signature should verify");
    
    // Tampered message should NOT verify
    std::string tampered = "Transfer 999 Q from Qx1234 to Qx5678";
    bool tampered_verified = sig.verify(
        reinterpret_cast<const unsigned char*>(tampered.data()),
        tampered.size(),
        kp.public_key
    );
    TEST_ASSERT(!tampered_verified, "tampered message should NOT verify");
    
    return true;
}

bool test_real_ed25519_wrong_key() {
    RealKeyPair alice, bob;
    alice.generate();
    bob.generate();
    
    std::string message = "Hello from Alice";
    RealSignature sig;
    sig.sign(
        reinterpret_cast<const unsigned char*>(message.data()),
        message.size(),
        alice.secret_key
    );
    
    // Bob's key should NOT verify Alice's signature
    bool wrong_key = sig.verify(
        reinterpret_cast<const unsigned char*>(message.data()),
        message.size(),
        bob.public_key
    );
    TEST_ASSERT(!wrong_key, "wrong public key should NOT verify");
    
    return true;
}

bool test_real_blake2b_hash() {
    unsigned char hash[crypto_generichash_BYTES];
    std::string data = "Arqon blockchain test data";
    
    int result = crypto_generichash(
        hash, sizeof(hash),
        reinterpret_cast<const unsigned char*>(data.data()), data.size(),
        nullptr, 0
    );
    TEST_ASSERT(result == 0, "hash should succeed");
    
    // Same input = same hash
    unsigned char hash2[crypto_generichash_BYTES];
    crypto_generichash(hash2, sizeof(hash2),
        reinterpret_cast<const unsigned char*>(data.data()), data.size(),
        nullptr, 0);
    TEST_ASSERT(memcmp(hash, hash2, sizeof(hash)) == 0, "same input = same hash");
    
    // Different input = different hash
    std::string data2 = "Different data";
    unsigned char hash3[crypto_generichash_BYTES];
    crypto_generichash(hash3, sizeof(hash3),
        reinterpret_cast<const unsigned char*>(data2.data()), data2.size(),
        nullptr, 0);
    TEST_ASSERT(memcmp(hash, hash3, sizeof(hash)) != 0, "different input = different hash");
    
    return true;
}

bool test_real_random_generation() {
    unsigned char buf1[32], buf2[32];
    
    randombytes_buf(buf1, 32);
    randombytes_buf(buf2, 32);
    
    // Random buffers should be different
    TEST_ASSERT(memcmp(buf1, buf2, 32) != 0, "random buffers should differ");
    
    // Should not be all zeros
    bool all_zero = true;
    for (int i = 0; i < 32; i++) {
        if (buf1[i] != 0) { all_zero = false; break; }
    }
    TEST_ASSERT(!all_zero, "random should not be all zeros");
    
    return true;
}

// ============================================================================
// REAL NETWORK TESTS
// ============================================================================

bool test_http_get_external() {
    // Test against a real public endpoint
    HttpResponse resp = http_get("https://httpbin.org/get", 10000);
    
    if (!resp.success) {
        std::cerr << "    Network error: " << resp.error << std::endl;
        // Don't fail if network is unavailable - just skip
        std::cout << "    [SKIP] Network unavailable" << std::endl;
        return true;
    }
    
    TEST_ASSERT(resp.status_code == 200, "should get 200 OK");
    TEST_ASSERT(resp.body.length() > 0, "should have response body");
    TEST_ASSERT(resp.total_time_ms > 0, "should have non-zero latency");
    
    std::cout << "    Response: " << resp.status_code << " (" 
              << (int)resp.total_time_ms << "ms, " 
              << resp.body.length() << " bytes)" << std::endl;
    
    return true;
}

bool test_http_post_json() {
    std::string json = R"({"prompt": "test", "model": "test"})";
    HttpResponse resp = http_post("https://httpbin.org/post", json, "application/json", 10000);
    
    if (!resp.success) {
        std::cout << "    [SKIP] Network unavailable" << std::endl;
        return true;
    }
    
    TEST_ASSERT(resp.status_code == 200, "should get 200 OK");
    TEST_ASSERT(resp.body.find("prompt") != std::string::npos, "should echo back json");
    
    std::cout << "    Response: " << resp.status_code << " (" 
              << (int)resp.total_time_ms << "ms)" << std::endl;
    
    return true;
}

bool test_ollama_availability() {
    HttpResponse resp = http_get("http://localhost:11434/api/tags", 2000);
    
    if (!resp.success || resp.status_code != 200) {
        std::cout << "    [INFO] Ollama not running at localhost:11434" << std::endl;
        return true;  // Not a failure, just not available
    }
    
    std::cout << "    [INFO] Ollama is running! Response: " << resp.body.substr(0, 100) << "..." << std::endl;
    return true;
}

bool test_petals_health() {
    HttpResponse resp = http_get("https://health.petals.dev/api/v1/state", 10000);
    
    if (!resp.success) {
        std::cout << "    [SKIP] Petals health endpoint unavailable" << std::endl;
        return true;
    }
    
    TEST_ASSERT(resp.status_code == 200, "should get 200 OK from Petals");
    TEST_ASSERT(resp.body.length() > 0, "should have state data");
    
    std::cout << "    Petals health: " << resp.status_code << " (" 
              << (int)resp.total_time_ms << "ms, "
              << resp.body.length() << " bytes)" << std::endl;
    
    return true;
}

// ============================================================================
// REAL TRANSACTION TESTS
// ============================================================================

struct Transaction {
    std::string from;      // Qx address
    std::string to;        // Qx address
    uint64_t amount;       // in Q (10^18 = 1 ARQON)
    uint64_t nonce;
    uint64_t timestamp;
    RealSignature signature;
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << from << "|" << to << "|" << amount << "|" << nonce << "|" << timestamp;
        return ss.str();
    }
    
    bool sign(const RealKeyPair& kp) {
        std::string data = serialize();
        return signature.sign(
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size(),
            kp.secret_key
        );
    }
    
    bool verify(const unsigned char* public_key) const {
        std::string data = serialize();
        return signature.verify(
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size(),
            public_key
        );
    }
};

bool test_real_transaction_signing() {
    RealKeyPair alice, bob;
    alice.generate();
    bob.generate();
    
    Transaction tx;
    tx.from = alice.publicKeyHex();
    tx.to = bob.publicKeyHex();
    tx.amount = 100 * 1000000000000000000ULL;  // 100 ARQON in Q
    tx.nonce = 1;
    tx.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    bool signed_ok = tx.sign(alice);
    TEST_ASSERT(signed_ok, "transaction signing should succeed");
    
    bool verified = tx.verify(alice.public_key);
    TEST_ASSERT(verified, "transaction should verify with sender key");
    
    bool wrong_key = tx.verify(bob.public_key);
    TEST_ASSERT(!wrong_key, "transaction should NOT verify with wrong key");
    
    return true;
}

bool test_real_batch_transactions() {
    const int BATCH_SIZE = 100;
    std::vector<RealKeyPair> keys(BATCH_SIZE);
    std::vector<Transaction> txs(BATCH_SIZE);
    
    // Generate keys
    for (int i = 0; i < BATCH_SIZE; i++) {
        keys[i].generate();
    }
    
    // Create and sign transactions
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        txs[i].from = keys[i].publicKeyHex();
        txs[i].to = keys[(i + 1) % BATCH_SIZE].publicKeyHex();
        txs[i].amount = (i + 1) * 1000000000000000000ULL;
        txs[i].nonce = i;
        txs[i].timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        txs[i].sign(keys[i]);
    }
    
    auto mid = std::chrono::high_resolution_clock::now();
    
    // Verify all transactions
    int verified = 0;
    for (int i = 0; i < BATCH_SIZE; i++) {
        if (txs[i].verify(keys[i].public_key)) {
            verified++;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    auto sign_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start).count();
    auto verify_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid).count();
    
    std::cout << "    Signed " << BATCH_SIZE << " txs in " << sign_ms << "ms ("
              << (BATCH_SIZE * 1000 / (sign_ms > 0 ? sign_ms : 1)) << " tx/s)" << std::endl;
    std::cout << "    Verified " << BATCH_SIZE << " txs in " << verify_ms << "ms ("
              << (BATCH_SIZE * 1000 / (verify_ms > 0 ? verify_ms : 1)) << " tx/s)" << std::endl;
    
    TEST_ASSERT(verified == BATCH_SIZE, "all transactions should verify");
    
    return true;
}

// ============================================================================
// REAL TIMING TESTS
// ============================================================================

bool test_crypto_performance() {
    const int ITERATIONS = 1000;
    
    // Key generation timing
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; i++) {
        RealKeyPair kp;
        kp.generate();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto keygen_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "    Key generation: " << (keygen_us / ITERATIONS) << " µs/op ("
              << (ITERATIONS * 1000000 / keygen_us) << " ops/s)" << std::endl;
    
    // Signing timing
    RealKeyPair kp;
    kp.generate();
    std::string msg = "Test message for signing benchmark";
    
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; i++) {
        RealSignature sig;
        sig.sign(reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), kp.secret_key);
    }
    end = std::chrono::high_resolution_clock::now();
    auto sign_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "    Signing: " << (sign_us / ITERATIONS) << " µs/op ("
              << (ITERATIONS * 1000000 / sign_us) << " ops/s)" << std::endl;
    
    // Verification timing
    RealSignature sig;
    sig.sign(reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), kp.secret_key);
    
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; i++) {
        sig.verify(reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), kp.public_key);
    }
    end = std::chrono::high_resolution_clock::now();
    auto verify_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "    Verification: " << (verify_us / ITERATIONS) << " µs/op ("
              << (ITERATIONS * 1000000 / verify_us) << " ops/s)" << std::endl;
    
    // Hashing timing
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; i++) {
        unsigned char hash[32];
        crypto_generichash(hash, 32, 
            reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
            nullptr, 0);
    }
    end = std::chrono::high_resolution_clock::now();
    auto hash_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "    BLAKE2b hash: " << (hash_us / ITERATIONS) << " µs/op ("
              << (ITERATIONS * 1000000 / hash_us) << " ops/s)" << std::endl;
    
    TEST_ASSERT(keygen_us > 0, "keygen should take measurable time");
    TEST_ASSERT(sign_us > 0, "signing should take measurable time");
    TEST_ASSERT(verify_us > 0, "verification should take measurable time");
    
    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    // Initialize libsodium
    if (sodium_init() < 0) {
        std::cerr << "FATAL: Failed to initialize libsodium" << std::endl;
        return 1;
    }
    
    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ARQON REAL INTEGRATION TESTS                           ║\n";
    std::cout << "║       Using: libsodium (Ed25519), libcurl (HTTP)             ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  REAL ED25519 CRYPTOGRAPHY\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_real_ed25519_keygen);
    RUN_TEST(test_real_ed25519_sign_verify);
    RUN_TEST(test_real_ed25519_wrong_key);
    RUN_TEST(test_real_blake2b_hash);
    RUN_TEST(test_real_random_generation);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  REAL NETWORK / HTTP\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_http_get_external);
    RUN_TEST(test_http_post_json);
    RUN_TEST(test_ollama_availability);
    RUN_TEST(test_petals_health);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  REAL TRANSACTIONS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_real_transaction_signing);
    RUN_TEST(test_real_batch_transactions);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CRYPTO PERFORMANCE BENCHMARKS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    RUN_TEST(test_crypto_performance);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  RESULTS: " << tests_passed << " passed, " << tests_failed << " failed\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    
    curl_global_cleanup();
    
    return tests_failed > 0 ? 1 : 0;
}
