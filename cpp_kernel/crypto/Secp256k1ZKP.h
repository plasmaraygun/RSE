#pragma once

/**
 * Real Zero-Knowledge Proofs via libsecp256k1-zkp
 * 
 * REQUIRES: libsecp256k1-zkp installed
 *   git clone https://github.com/ElementsProject/secp256k1-zkp
 *   cd secp256k1-zkp
 *   ./autogen.sh
 *   ./configure --enable-module-rangeproof --enable-module-surjectionproof --enable-module-generator
 *   make && sudo make install
 * 
 * LINK WITH: -lsecp256k1
 */

#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include <stdexcept>
#include <iostream>

#ifdef USE_SECP256K1_ZKP
#include <secp256k1.h>
#include <secp256k1_generator.h>
#include <secp256k1_rangeproof.h>
#include <secp256k1_pedersen.h>
#endif

namespace crypto_zkp {

// ============================================================================
// Constants
// ============================================================================

constexpr size_t BLIND_SIZE = 32;
constexpr size_t COMMITMENT_SIZE = 33;  // Compressed point
constexpr size_t MAX_RANGE_PROOF_SIZE = 5134;  // Max size for 64-bit range

using BlindingFactor = std::array<uint8_t, BLIND_SIZE>;

// ============================================================================
// Pedersen Commitment
// ============================================================================

struct PedersenCommitment {
    std::array<uint8_t, COMMITMENT_SIZE> data;
    
    bool operator==(const PedersenCommitment& other) const {
        return data == other.data;
    }
};

// ============================================================================
// Range Proof
// ============================================================================

struct RangeProof {
    std::vector<uint8_t> proof;
    uint64_t min_value;
    uint64_t max_value;
    
    size_t size() const { return proof.size(); }
    
    bool empty() const { return proof.empty(); }
};

// ============================================================================
// Secp256k1-zkp Context Manager
// ============================================================================

#ifdef USE_SECP256K1_ZKP

class Secp256k1Context {
private:
    secp256k1_context* ctx_;
    secp256k1_generator generator_;
    bool initialized_;
    
    static Secp256k1Context* instance_;
    
    Secp256k1Context() : ctx_(nullptr), initialized_(false) {
        // Create context with all capabilities
        ctx_ = secp256k1_context_create(
            SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY
        );
        
        if (!ctx_) {
            throw std::runtime_error("Failed to create secp256k1 context");
        }
        
        // Generate the standard generator point (for Pedersen commitments)
        // Using a fixed seed for deterministic generator
        unsigned char seed[32] = {0};
        seed[0] = 'R'; seed[1] = 'S'; seed[2] = 'E';
        
        if (!secp256k1_generator_generate(ctx_, &generator_, seed)) {
            secp256k1_context_destroy(ctx_);
            ctx_ = nullptr;
            throw std::runtime_error("Failed to generate generator point");
        }
        
        initialized_ = true;
    }
    
public:
    ~Secp256k1Context() {
        if (ctx_) {
            secp256k1_context_destroy(ctx_);
            ctx_ = nullptr;
        }
    }
    
    // Singleton access
    static Secp256k1Context& getInstance() {
        if (!instance_) {
            instance_ = new Secp256k1Context();
        }
        return *instance_;
    }
    
    secp256k1_context* ctx() { return ctx_; }
    const secp256k1_generator* generator() const { return &generator_; }
    bool isInitialized() const { return initialized_; }
    
    // Delete copy/move
    Secp256k1Context(const Secp256k1Context&) = delete;
    Secp256k1Context& operator=(const Secp256k1Context&) = delete;
};

Secp256k1Context* Secp256k1Context::instance_ = nullptr;

// ============================================================================
// Pedersen Commitment Operations
// ============================================================================

class PedersenOps {
public:
    /**
     * Create a Pedersen commitment to a value
     * 
     * C = value * H + blind * G
     * 
     * Where:
     *   - H is the generator point for values
     *   - G is the generator point for blinding factors
     *   - blind is a random 32-byte blinding factor
     *   - value is the amount being committed to
     */
    static PedersenCommitment commit(uint64_t value, const BlindingFactor& blind) {
        auto& ctx = Secp256k1Context::getInstance();
        
        secp256k1_pedersen_commitment commit;
        
        if (!secp256k1_pedersen_commit(
            ctx.ctx(),
            &commit,
            blind.data(),
            value,
            ctx.generator(),
            &secp256k1_generator_const_g
        )) {
            throw std::runtime_error("Failed to create Pedersen commitment");
        }
        
        PedersenCommitment result;
        
        if (!secp256k1_pedersen_commitment_serialize(
            ctx.ctx(),
            result.data.data(),
            &commit
        )) {
            throw std::runtime_error("Failed to serialize commitment");
        }
        
        return result;
    }
    
    /**
     * Parse a serialized commitment
     */
    static bool parse(const PedersenCommitment& serialized, secp256k1_pedersen_commitment* commit) {
        auto& ctx = Secp256k1Context::getInstance();
        
        return secp256k1_pedersen_commitment_parse(
            ctx.ctx(),
            commit,
            serialized.data.data()
        ) == 1;
    }
    
    /**
     * Verify that commitments sum to zero (for transaction balance)
     * 
     * Used to verify: sum(inputs) - sum(outputs) - fee = 0
     * Without revealing actual values
     */
    static bool verifyTally(
        const std::vector<PedersenCommitment>& positive,  // Inputs
        const std::vector<PedersenCommitment>& negative   // Outputs
    ) {
        auto& ctx = Secp256k1Context::getInstance();
        
        // Parse all commitments
        std::vector<secp256k1_pedersen_commitment> pos_commits(positive.size());
        std::vector<secp256k1_pedersen_commitment> neg_commits(negative.size());
        
        std::vector<const secp256k1_pedersen_commitment*> pos_ptrs;
        std::vector<const secp256k1_pedersen_commitment*> neg_ptrs;
        
        for (size_t i = 0; i < positive.size(); i++) {
            if (!parse(positive[i], &pos_commits[i])) {
                return false;
            }
            pos_ptrs.push_back(&pos_commits[i]);
        }
        
        for (size_t i = 0; i < negative.size(); i++) {
            if (!parse(negative[i], &neg_commits[i])) {
                return false;
            }
            neg_ptrs.push_back(&neg_commits[i]);
        }
        
        return secp256k1_pedersen_verify_tally(
            ctx.ctx(),
            pos_ptrs.data(), pos_ptrs.size(),
            neg_ptrs.data(), neg_ptrs.size()
        ) == 1;
    }
    
    /**
     * Compute sum of blinding factors
     * 
     * Used when creating transactions to ensure blinding factors balance
     */
    static BlindingFactor blindSum(
        const std::vector<BlindingFactor>& positive,
        const std::vector<BlindingFactor>& negative
    ) {
        auto& ctx = Secp256k1Context::getInstance();
        
        std::vector<const unsigned char*> pos_ptrs;
        std::vector<const unsigned char*> neg_ptrs;
        
        for (const auto& b : positive) {
            pos_ptrs.push_back(b.data());
        }
        for (const auto& b : negative) {
            neg_ptrs.push_back(b.data());
        }
        
        BlindingFactor result;
        
        if (!secp256k1_pedersen_blind_sum(
            ctx.ctx(),
            result.data(),
            pos_ptrs.data(), pos_ptrs.size(),
            neg_ptrs.size()
        )) {
            throw std::runtime_error("Failed to compute blind sum");
        }
        
        return result;
    }
};

// ============================================================================
// Range Proof Operations
// ============================================================================

class RangeProofOps {
public:
    /**
     * Create a range proof proving value is in [0, 2^64)
     * 
     * This proves the committed value is non-negative without revealing it.
     * Essential for preventing negative amounts in confidential transactions.
     */
    static RangeProof prove(
        uint64_t value,
        const BlindingFactor& blind,
        const PedersenCommitment& commitment,
        uint64_t min_value = 0
    ) {
        auto& ctx = Secp256k1Context::getInstance();
        
        RangeProof result;
        result.proof.resize(MAX_RANGE_PROOF_SIZE);
        result.min_value = min_value;
        
        size_t proof_len = result.proof.size();
        
        // Parse commitment
        secp256k1_pedersen_commitment commit;
        if (!PedersenOps::parse(commitment, &commit)) {
            throw std::runtime_error("Failed to parse commitment for range proof");
        }
        
        // Create range proof
        if (!secp256k1_rangeproof_sign(
            ctx.ctx(),
            result.proof.data(),
            &proof_len,
            min_value,
            &commit,
            blind.data(),
            commit.data,  // nonce (using commitment as nonce)
            0,            // exp (0 = compact proof)
            0,            // min_bits
            value,
            nullptr, 0,   // message
            nullptr, 0,   // extra_commit
            ctx.generator()
        )) {
            throw std::runtime_error("Failed to create range proof");
        }
        
        result.proof.resize(proof_len);
        
        return result;
    }
    
    /**
     * Verify a range proof
     * 
     * Returns true if the committed value is proven to be in the valid range.
     */
    static bool verify(
        const PedersenCommitment& commitment,
        const RangeProof& proof
    ) {
        auto& ctx = Secp256k1Context::getInstance();
        
        // Parse commitment
        secp256k1_pedersen_commitment commit;
        if (!PedersenOps::parse(commitment, &commit)) {
            return false;
        }
        
        uint64_t min_value, max_value;
        
        return secp256k1_rangeproof_verify(
            ctx.ctx(),
            &min_value,
            &max_value,
            &commit,
            proof.proof.data(),
            proof.proof.size(),
            nullptr, 0,  // extra_commit
            ctx.generator()
        ) == 1;
    }
    
    /**
     * Get information from a range proof without full verification
     */
    static bool getInfo(
        const RangeProof& proof,
        uint64_t& min_value,
        uint64_t& max_value
    ) {
        auto& ctx = Secp256k1Context::getInstance();
        
        int exp, mantissa;
        
        return secp256k1_rangeproof_info(
            ctx.ctx(),
            &exp,
            &mantissa,
            &min_value,
            &max_value,
            proof.proof.data(),
            proof.proof.size()
        ) == 1;
    }
};

// ============================================================================
// Confidential Transaction
// ============================================================================

struct ConfidentialValue {
    uint64_t value;              // The actual value (secret)
    BlindingFactor blind;        // Blinding factor (secret)
    PedersenCommitment commit;   // Public commitment
    RangeProof range_proof;      // Proof that value >= 0
    
    // Create new confidential value with random blinding
    static ConfidentialValue create(uint64_t value) {
        ConfidentialValue cv;
        cv.value = value;
        
        // Generate random blinding factor
        // In production, use proper random source
        for (size_t i = 0; i < BLIND_SIZE; i++) {
            cv.blind[i] = static_cast<uint8_t>(rand() & 0xFF);
        }
        
        cv.commit = PedersenOps::commit(value, cv.blind);
        cv.range_proof = RangeProofOps::prove(value, cv.blind, cv.commit);
        
        return cv;
    }
    
    // Verify this confidential value
    bool verify() const {
        return RangeProofOps::verify(commit, range_proof);
    }
};

/**
 * Verify a confidential transaction balances
 * 
 * Checks: sum(input_commits) = sum(output_commits) + fee_commit
 * Without revealing any values
 */
bool verifyConfidentialTransaction(
    const std::vector<PedersenCommitment>& inputs,
    const std::vector<PedersenCommitment>& outputs,
    const std::vector<RangeProof>& output_proofs
) {
    // Verify all range proofs
    if (outputs.size() != output_proofs.size()) {
        return false;
    }
    
    for (size_t i = 0; i < outputs.size(); i++) {
        if (!RangeProofOps::verify(outputs[i], output_proofs[i])) {
            std::cerr << "[CT] Range proof " << i << " failed" << std::endl;
            return false;
        }
    }
    
    // Verify commitments balance
    if (!PedersenOps::verifyTally(inputs, outputs)) {
        std::cerr << "[CT] Commitment tally failed" << std::endl;
        return false;
    }
    
    return true;
}

#else // !USE_SECP256K1_ZKP

// Stub implementations when library not available
// These will fail at runtime with clear error messages

class Secp256k1Context {
public:
    static Secp256k1Context& getInstance() {
        throw std::runtime_error(
            "libsecp256k1-zkp not available. Install with:\n"
            "  git clone https://github.com/ElementsProject/secp256k1-zkp\n"
            "  cd secp256k1-zkp && ./autogen.sh\n"
            "  ./configure --enable-module-rangeproof --enable-module-generator\n"
            "  make && sudo make install\n"
            "Then compile with -DUSE_SECP256K1_ZKP -lsecp256k1"
        );
    }
};

class PedersenOps {
public:
    static PedersenCommitment commit(uint64_t, const BlindingFactor&) {
        Secp256k1Context::getInstance();
        return {};
    }
    
    static bool verifyTally(const std::vector<PedersenCommitment>&, const std::vector<PedersenCommitment>&) {
        Secp256k1Context::getInstance();
        return false;
    }
};

class RangeProofOps {
public:
    static RangeProof prove(uint64_t, const BlindingFactor&, const PedersenCommitment&, uint64_t = 0) {
        Secp256k1Context::getInstance();
        return {};
    }
    
    static bool verify(const PedersenCommitment&, const RangeProof&) {
        Secp256k1Context::getInstance();
        return false;
    }
};

#endif // USE_SECP256K1_ZKP

} // namespace crypto_zkp
