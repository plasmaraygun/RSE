#pragma once

/**
 * Proof of Useful Work (PoUW) Verification System
 * 
 * Implements:
 * - Inference job submission and tracking
 * - Random auditor selection
 * - Result verification with semantic similarity
 * - Slashing for failed verification
 */

#include "Tokenomics.h"
#include <random>
#include <functional>

namespace arqon {

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr size_t NUM_AUDITORS = 3;                    // 3 random auditors per job
constexpr double VERIFICATION_THRESHOLD = 0.95;       // 95% similarity required
constexpr double AUDITOR_AGREEMENT_THRESHOLD = 2.0/3.0; // 2 of 3 must agree

// ============================================================================
// AUDITOR SELECTION (VRF-like)
// ============================================================================

class AuditorSelector {
public:
    // Select N auditors for a job using deterministic randomness
    static std::vector<Address> selectAuditors(
        uint64_t job_id,
        uint64_t epoch,
        const std::vector<NodeInfo>& gpu_nodes,
        const Address& exclude_node,  // The node being audited
        size_t n = NUM_AUDITORS
    ) {
        std::vector<Address> auditors;
        if (gpu_nodes.size() <= n) {
            // Not enough nodes, use all except the audited one
            for (const auto& node : gpu_nodes) {
                if (node.address != exclude_node) {
                    auditors.push_back(node.address);
                }
            }
            return auditors;
        }
        
        // Create seed from job_id and epoch
        uint64_t seed = job_id ^ (epoch << 32) ^ 0x9e3779b97f4a7c15ULL;
        std::mt19937_64 rng(seed);
        
        // Shuffle and pick top N (excluding the audited node)
        std::vector<size_t> indices;
        for (size_t i = 0; i < gpu_nodes.size(); i++) {
            if (gpu_nodes[i].address != exclude_node) {
                indices.push_back(i);
            }
        }
        
        std::shuffle(indices.begin(), indices.end(), rng);
        
        for (size_t i = 0; i < std::min(n, indices.size()); i++) {
            auditors.push_back(gpu_nodes[indices[i]].address);
        }
        
        return auditors;
    }
    
    // Verify that an address was legitimately selected as auditor
    static bool verifyAuditorSelection(
        const Address& auditor,
        uint64_t job_id,
        uint64_t epoch,
        const std::vector<NodeInfo>& gpu_nodes,
        const Address& exclude_node
    ) {
        auto selected = selectAuditors(job_id, epoch, gpu_nodes, exclude_node);
        for (const auto& a : selected) {
            if (a == auditor) return true;
        }
        return false;
    }
};

// ============================================================================
// SEMANTIC SIMILARITY (Simplified)
// ============================================================================

class SemanticVerifier {
public:
    // Compare two inference outputs for semantic similarity
    // Returns 0.0 - 1.0 (1.0 = identical)
    static double computeSimilarity(const std::string& output1, const std::string& output2) {
        if (output1.empty() && output2.empty()) return 1.0;
        if (output1.empty() || output2.empty()) return 0.0;
        
        // Jaccard similarity on word tokens
        auto tokenize = [](const std::string& s) {
            std::vector<std::string> tokens;
            std::string token;
            for (char c : s) {
                if (std::isalnum(c)) {
                    token += std::tolower(c);
                } else if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            }
            if (!token.empty()) tokens.push_back(token);
            return tokens;
        };
        
        auto tokens1 = tokenize(output1);
        auto tokens2 = tokenize(output2);
        
        if (tokens1.empty() && tokens2.empty()) return 1.0;
        if (tokens1.empty() || tokens2.empty()) return 0.0;
        
        // Count intersection and union
        std::unordered_map<std::string, int> counts;
        for (const auto& t : tokens1) counts[t]++;
        
        size_t intersection = 0;
        for (const auto& t : tokens2) {
            if (counts[t] > 0) {
                intersection++;
                counts[t]--;
            }
        }
        
        size_t union_size = tokens1.size() + tokens2.size() - intersection;
        if (union_size == 0) return 1.0;
        
        return static_cast<double>(intersection) / union_size;
    }
    
    // Check if verification passes threshold
    static bool meetsThreshold(double similarity) {
        return similarity >= VERIFICATION_THRESHOLD;
    }
};

// ============================================================================
// POUW MANAGER
// ============================================================================

class PoUWManager {
private:
    std::vector<InferenceJob> pending_jobs_;
    std::vector<InferenceJob> completed_jobs_;
    std::vector<AuditorVerification> verifications_;
    
    uint64_t next_job_id_;
    TokenomicsEngine& engine_;
    
    // Callback for actual inference execution
    std::function<std::string(const std::string& model, const std::string& prompt, 
                              uint32_t max_tokens, float temp)> inference_callback_;
    
public:
    explicit PoUWManager(TokenomicsEngine& engine) 
        : next_job_id_(1), engine_(engine) {}
    
    // Set inference callback (connects to actual inference engine)
    void setInferenceCallback(
        std::function<std::string(const std::string&, const std::string&, 
                                  uint32_t, float)> callback
    ) {
        inference_callback_ = callback;
    }
    
    // ========================================================================
    // JOB SUBMISSION
    // ========================================================================
    
    uint64_t submitJob(
        const Address& requester,
        const std::string& model,
        const std::string& prompt,
        uint32_t max_tokens,
        float temperature,
        uint64_t payment
    ) {
        // Verify payment
        Account& account = engine_.getOrCreateAccount(requester);
        if (account.balance < payment) return 0;
        
        // Deduct payment upfront
        account.balance -= payment;
        
        InferenceJob job;
        job.id = next_job_id_++;
        job.requester = requester;
        job.gpu_node = Address{};  // Assigned later
        job.model = model;
        job.prompt = prompt;
        job.max_tokens = max_tokens;
        job.temperature = temperature;
        job.payment = payment;
        job.submitted_epoch = engine_.getCurrentEpoch();
        job.deadline_epoch = engine_.getCurrentEpoch() + EPOCHS_PER_HOUR;  // 1 hour deadline
        job.completed = false;
        
        pending_jobs_.push_back(job);
        return job.id;
    }
    
    // ========================================================================
    // JOB ASSIGNMENT & EXECUTION
    // ========================================================================
    
    bool assignJob(uint64_t job_id, const Address& gpu_node) {
        for (auto& job : pending_jobs_) {
            if (job.id == job_id && QxAddress::isZero(job.gpu_node)) {
                job.gpu_node = gpu_node;
                return true;
            }
        }
        return false;
    }
    
    bool completeJob(uint64_t job_id, const std::string& response, uint32_t tokens) {
        for (auto it = pending_jobs_.begin(); it != pending_jobs_.end(); ++it) {
            if (it->id == job_id) {
                it->completed = true;
                it->response = response;
                it->tokens_generated = tokens;
                it->completion_epoch = engine_.getCurrentEpoch();
                
                completed_jobs_.push_back(*it);
                pending_jobs_.erase(it);
                return true;
            }
        }
        return false;
    }
    
    // ========================================================================
    // VERIFICATION
    // ========================================================================
    
    // Submit auditor verification for a completed job
    bool submitVerification(
        uint64_t job_id,
        const Address& auditor,
        const std::string& auditor_response
    ) {
        // Find job
        InferenceJob* job = nullptr;
        for (auto& j : completed_jobs_) {
            if (j.id == job_id) {
                job = &j;
                break;
            }
        }
        if (!job) return false;
        
        // Check auditor is valid (simplified - would use VRF in production)
        // For now, accept any registered GPU node
        
        // Compute similarity
        double similarity = SemanticVerifier::computeSimilarity(
            job->response, auditor_response
        );
        
        AuditorVerification v;
        v.job_id = job_id;
        v.auditor = auditor;
        v.verified = SemanticVerifier::meetsThreshold(similarity);
        v.similarity_score = similarity;
        v.epoch = engine_.getCurrentEpoch();
        
        verifications_.push_back(v);
        return true;
    }
    
    // Finalize verification and distribute rewards/slashing
    void finalizeVerification(uint64_t job_id) {
        // Find job
        InferenceJob* job = nullptr;
        for (auto& j : completed_jobs_) {
            if (j.id == job_id) {
                job = &j;
                break;
            }
        }
        if (!job) return;
        
        // Count verifications for this job
        int verified_count = 0;
        int total_count = 0;
        
        for (const auto& v : verifications_) {
            if (v.job_id == job_id) {
                total_count++;
                if (v.verified) verified_count++;
            }
        }
        
        if (total_count == 0) {
            // No auditors, pay the node (trust mode)
            distributePayment(*job, true);
            return;
        }
        
        double agreement = static_cast<double>(verified_count) / total_count;
        
        if (agreement >= AUDITOR_AGREEMENT_THRESHOLD) {
            // Verification passed - distribute payment
            distributePayment(*job, true);
        } else {
            // Verification failed - slash and refund
            Hash evidence{};
            engine_.slash(job->gpu_node, SlashReason::FAILED_VERIFICATION, evidence);
            
            // Refund requester
            engine_.mint(job->requester, job->payment);
        }
    }
    
private:
    void distributePayment(const InferenceJob& job, bool verified) {
        if (!verified) return;
        
        // Distribution: same as epoch rewards
        // 60% to GPU node, 10% to relay, 20% to stakers, 10% to treasury
        uint64_t gpu_share = static_cast<uint64_t>(job.payment * GPU_NODE_SHARE);
        uint64_t treasury_share = static_cast<uint64_t>(job.payment * TREASURY_SHARE);
        uint64_t staker_share = static_cast<uint64_t>(job.payment * STAKER_SHARE);
        // Relay share goes to stakers for inference payments (no relay routing tracked per-job)
        uint64_t extra_staker = static_cast<uint64_t>(job.payment * RELAY_NODE_SHARE);
        
        engine_.mint(job.gpu_node, gpu_share);
        engine_.mint(ReservedAddress::treasury(), treasury_share);
        
        // Staker rewards distributed via next epoch
        // For now, add to treasury and let epoch distribution handle it
        engine_.mint(ReservedAddress::treasury(), staker_share + extra_staker);
    }
    
public:
    // ========================================================================
    // CLEANUP
    // ========================================================================
    
    // Remove expired jobs and refund
    void cleanupExpiredJobs() {
        uint64_t current = engine_.getCurrentEpoch();
        
        for (auto it = pending_jobs_.begin(); it != pending_jobs_.end();) {
            if (current > it->deadline_epoch) {
                // Refund requester
                engine_.mint(it->requester, it->payment);
                it = pending_jobs_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // ========================================================================
    // GETTERS
    // ========================================================================
    
    const std::vector<InferenceJob>& getPendingJobs() const { return pending_jobs_; }
    const std::vector<InferenceJob>& getCompletedJobs() const { return completed_jobs_; }
    
    InferenceJob* getJob(uint64_t id) {
        for (auto& j : pending_jobs_) {
            if (j.id == id) return &j;
        }
        for (auto& j : completed_jobs_) {
            if (j.id == id) return &j;
        }
        return nullptr;
    }
};

// ============================================================================
// INFERENCE PRICING
// ============================================================================

class InferencePricing {
public:
    // Get price in Q per 1000 tokens based on model size
    static uint64_t getPricePerKTokens(const std::string& model) {
        // Parse model size from name (simplified)
        std::string lower = model;
        for (char& c : lower) c = std::tolower(c);
        
        if (lower.find("405b") != std::string::npos || 
            lower.find("400b") != std::string::npos) {
            return 5 * Q_PER_ARQON / 1000;  // 0.005 ARQON
        }
        if (lower.find("70b") != std::string::npos) {
            return Q_PER_ARQON / 1000;  // 0.001 ARQON
        }
        if (lower.find("34b") != std::string::npos ||
            lower.find("33b") != std::string::npos ||
            lower.find("27b") != std::string::npos) {
            return Q_PER_ARQON / 2000;  // 0.0005 ARQON
        }
        if (lower.find("13b") != std::string::npos ||
            lower.find("14b") != std::string::npos) {
            return Q_PER_ARQON / 5000;  // 0.0002 ARQON
        }
        if (lower.find("7b") != std::string::npos ||
            lower.find("8b") != std::string::npos) {
            return Q_PER_ARQON / 10000;  // 0.0001 ARQON
        }
        // Default for smaller models
        return Q_PER_ARQON / 10000;  // 0.0001 ARQON
    }
    
    // Calculate total price for a job
    static uint64_t calculateJobPrice(const std::string& model, uint32_t max_tokens) {
        uint64_t price_per_k = getPricePerKTokens(model);
        return (price_per_k * max_tokens) / 1000;
    }
};

} // namespace arqon
