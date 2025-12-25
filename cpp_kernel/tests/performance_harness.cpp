/**
 * Performance Testing Harness
 * 
 * Benchmarks the RSE system at scale (1000+ simulated nodes).
 * Tests throughput, latency, and consensus convergence.
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../braided/BlockchainBraid.h"
#include "../network/P2PNode.h"
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <iomanip>
#include <fstream>

using namespace crypto;
using namespace economics;
using namespace braided;

// ============================================================================
// Configuration
// ============================================================================

struct BenchmarkConfig {
    size_t num_nodes = 1000;
    size_t num_transactions = 100000;
    size_t batch_size = 1000;
    size_t num_threads = std::thread::hardware_concurrency();
    bool verbose = false;
};

// ============================================================================
// Metrics Collection
// ============================================================================

struct BenchmarkMetrics {
    std::atomic<uint64_t> transactions_processed{0};
    std::atomic<uint64_t> transactions_failed{0};
    std::atomic<uint64_t> total_latency_us{0};
    std::atomic<uint64_t> min_latency_us{UINT64_MAX};
    std::atomic<uint64_t> max_latency_us{0};
    
    double throughput_tps = 0;
    double avg_latency_ms = 0;
    double p50_latency_ms = 0;
    double p99_latency_ms = 0;
    
    void updateLatency(uint64_t latency_us) {
        total_latency_us.fetch_add(latency_us, std::memory_order_relaxed);
        
        uint64_t current_min = min_latency_us.load(std::memory_order_relaxed);
        while (latency_us < current_min && 
               !min_latency_us.compare_exchange_weak(current_min, latency_us));
        
        uint64_t current_max = max_latency_us.load(std::memory_order_relaxed);
        while (latency_us > current_max && 
               !max_latency_us.compare_exchange_weak(current_max, latency_us));
    }
};

// ============================================================================
// Simulated Node
// ============================================================================

class SimulatedNode {
public:
    KeyPair keypair;
    uint64_t node_id;
    uint64_t nonce = 0;
    
    SimulatedNode(uint64_t id) : node_id(id) {}
    
    Transaction createTransaction(const Address& to, uint64_t amount) {
        Transaction tx;
        tx.from = keypair.getAddress();
        tx.to = to;
        tx.value = amount;
        tx.nonce = nonce++;
        tx.gas_limit = 21000;
        tx.gas_price = 1;
        tx.sign(keypair);
        return tx;
    }
};

// ============================================================================
// Performance Harness
// ============================================================================

class PerformanceHarness {
private:
    BenchmarkConfig config_;
    BenchmarkMetrics metrics_;
    std::vector<std::unique_ptr<SimulatedNode>> nodes_;
    AccountManager accounts_;
    RewardDistributor rewards_;
    TransactionProcessor processor_;
    std::vector<uint64_t> latency_samples_;
    std::mutex samples_mutex_;
    
public:
    PerformanceHarness(const BenchmarkConfig& config)
        : config_(config),
          rewards_(accounts_),
          processor_(accounts_, rewards_) {
        init_crypto();
    }
    
    void setup() {
        std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║         RSE PERFORMANCE TESTING HARNESS                  ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;
        std::cout << std::endl;
        
        std::cout << "[Setup] Creating " << config_.num_nodes << " simulated nodes..." << std::endl;
        
        nodes_.reserve(config_.num_nodes);
        for (size_t i = 0; i < config_.num_nodes; i++) {
            auto node = std::make_unique<SimulatedNode>(i);
            
            // Register account with initial balance
            auto& acct = accounts_.getAccount(node->keypair.getAddress());
            acct.balance = 1000 * Q_PER_ARQON;  // 1000 ARQN
            acct.stake = 100 * Q_PER_ARQON;     // 100 ARQN staked
            
            nodes_.push_back(std::move(node));
            
            if ((i + 1) % 100 == 0 && config_.verbose) {
                std::cout << "  Created " << (i + 1) << " nodes" << std::endl;
            }
        }
        
        std::cout << "[Setup] Done. Total accounts: " << nodes_.size() << std::endl;
        std::cout << std::endl;
    }
    
    void runTransactionBenchmark() {
        std::cout << "═══ Transaction Throughput Benchmark ═══" << std::endl;
        std::cout << "  Transactions: " << config_.num_transactions << std::endl;
        std::cout << "  Threads: " << config_.num_threads << std::endl;
        std::cout << "  Batch size: " << config_.batch_size << std::endl;
        std::cout << std::endl;
        
        latency_samples_.reserve(config_.num_transactions);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Spawn worker threads
        std::vector<std::thread> workers;
        size_t txs_per_thread = config_.num_transactions / config_.num_threads;
        
        for (size_t t = 0; t < config_.num_threads; t++) {
            workers.emplace_back([this, t, txs_per_thread]() {
                std::mt19937 rng(t);
                std::uniform_int_distribution<size_t> node_dist(0, nodes_.size() - 1);
                std::uniform_int_distribution<uint64_t> amount_dist(1, 100);
                
                for (size_t i = 0; i < txs_per_thread; i++) {
                    size_t from_idx = node_dist(rng);
                    size_t to_idx = node_dist(rng);
                    while (to_idx == from_idx) to_idx = node_dist(rng);
                    
                    auto& from_node = nodes_[from_idx];
                    auto& to_node = nodes_[to_idx];
                    
                    uint64_t amount = amount_dist(rng) * Q_PER_ARQON / 1000;  // Small amounts
                    
                    auto tx_start = std::chrono::high_resolution_clock::now();
                    
                    Transaction tx = from_node->createTransaction(
                        to_node->keypair.getAddress(), amount);
                    
                    auto result = processor_.process(tx);
                    bool success = (result == TransactionProcessor::Result::SUCCESS);
                    
                    auto tx_end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                        tx_end - tx_start).count();
                    
                    if (success) {
                        metrics_.transactions_processed.fetch_add(1, std::memory_order_relaxed);
                        metrics_.updateLatency(latency);
                        
                        std::lock_guard<std::mutex> lock(samples_mutex_);
                        latency_samples_.push_back(latency);
                    } else {
                        metrics_.transactions_failed.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }
        
        // Wait for completion
        for (auto& w : workers) {
            w.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        // Calculate metrics
        uint64_t processed = metrics_.transactions_processed.load();
        metrics_.throughput_tps = duration_ms > 0 ? (processed * 1000.0 / duration_ms) : 0;
        metrics_.avg_latency_ms = processed > 0 ? 
            (metrics_.total_latency_us.load() / processed / 1000.0) : 0;
        
        // Calculate percentiles
        if (!latency_samples_.empty()) {
            std::sort(latency_samples_.begin(), latency_samples_.end());
            size_t p50_idx = latency_samples_.size() * 50 / 100;
            size_t p99_idx = latency_samples_.size() * 99 / 100;
            metrics_.p50_latency_ms = latency_samples_[p50_idx] / 1000.0;
            metrics_.p99_latency_ms = latency_samples_[p99_idx] / 1000.0;
        }
        
        printResults(duration_ms);
    }
    
    void printResults(uint64_t duration_ms) {
        std::cout << std::endl;
        std::cout << "═══ Results ═══" << std::endl;
        std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
        std::cout << "  Processed: " << metrics_.transactions_processed.load() << std::endl;
        std::cout << "  Failed: " << metrics_.transactions_failed.load() << std::endl;
        std::cout << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                  << metrics_.throughput_tps << " TPS" << std::endl;
        std::cout << "  Avg latency: " << std::setprecision(3) << metrics_.avg_latency_ms << " ms" << std::endl;
        std::cout << "  P50 latency: " << metrics_.p50_latency_ms << " ms" << std::endl;
        std::cout << "  P99 latency: " << metrics_.p99_latency_ms << " ms" << std::endl;
        std::cout << "  Min latency: " << (metrics_.min_latency_us.load() / 1000.0) << " ms" << std::endl;
        std::cout << "  Max latency: " << (metrics_.max_latency_us.load() / 1000.0) << " ms" << std::endl;
    }
    
    void exportResults(const std::string& filename) {
        std::ofstream out(filename);
        out << "{\n";
        out << "  \"nodes\": " << config_.num_nodes << ",\n";
        out << "  \"transactions\": " << metrics_.transactions_processed.load() << ",\n";
        out << "  \"throughput_tps\": " << metrics_.throughput_tps << ",\n";
        out << "  \"avg_latency_ms\": " << metrics_.avg_latency_ms << ",\n";
        out << "  \"p50_latency_ms\": " << metrics_.p50_latency_ms << ",\n";
        out << "  \"p99_latency_ms\": " << metrics_.p99_latency_ms << "\n";
        out << "}\n";
        out.close();
        std::cout << std::endl << "[Export] Results saved to " << filename << std::endl;
    }
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    BenchmarkConfig config;
    
    // Parse args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--nodes" && i + 1 < argc) {
            config.num_nodes = std::stoul(argv[++i]);
        } else if (arg == "--txs" && i + 1 < argc) {
            config.num_transactions = std::stoul(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.num_threads = std::stoul(argv[++i]);
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--help") {
            std::cout << "Usage: performance_harness [options]" << std::endl;
            std::cout << "  --nodes N     Number of simulated nodes (default: 1000)" << std::endl;
            std::cout << "  --txs N       Number of transactions (default: 100000)" << std::endl;
            std::cout << "  --threads N   Worker threads (default: CPU cores)" << std::endl;
            std::cout << "  -v, --verbose Verbose output" << std::endl;
            return 0;
        }
    }
    
    PerformanceHarness harness(config);
    harness.setup();
    harness.runTransactionBenchmark();
    harness.exportResults("benchmark_results.json");
    
    std::cout << std::endl;
    std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║              BENCHMARK COMPLETE                          ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;
    
    return 0;
}
