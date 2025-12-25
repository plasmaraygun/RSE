#pragma once

/**
 * Petals Network Client
 * 
 * HTTP/WebSocket client for connecting to Petals distributed inference network.
 * Petals allows running large language models (LLMs) collaboratively by splitting
 * the model across multiple nodes, each serving different layers.
 * 
 * Integration points:
 * - health.petals.dev - Network health monitor
 * - chat.petals.dev - Chat interface
 * - Direct node connections for inference
 * 
 * References:
 * - https://github.com/bigscience-workshop/petals
 * - https://github.com/petals-infra/health.petals.dev
 * - https://github.com/petals-infra/chat.petals.dev
 */

#include "InferenceNode.h"
#include "../core/Constants.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <netdb.h>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace inference {

// ============================================================================
// Petals Network Configuration
// ============================================================================

struct PetalsConfig {
    // Network endpoints
    std::string health_endpoint = "https://health.petals.dev";
    std::string chat_endpoint = "https://chat.petals.dev";
    std::string dht_prefix = "/petals";
    
    // Default models
    std::vector<std::string> supported_models = {
        "meta-llama/Meta-Llama-3.1-405B-Instruct",
        "meta-llama/Meta-Llama-3.1-70B-Instruct",
        "meta-llama/Llama-2-70b-chat-hf",
        "bigscience/bloom",
        "bigscience/bloomz"
    };
    
    // Connection settings
    uint32_t connection_timeout_ms = 30000;
    uint32_t request_timeout_ms = 120000;
    uint32_t max_retries = 3;
    
    // Inference settings
    uint32_t default_max_tokens = 256;
    double default_temperature = 0.7;
    
    // Petals bridge settings (Python server)
    uint16_t bridge_port = 8765;
    double default_top_p = 0.9;
};

// ============================================================================
// Petals Node Info (from health.petals.dev)
// ============================================================================

struct PetalsNodeInfo {
    std::string peer_id;           // Libp2p peer ID
    std::string public_name;       // Display name
    std::string version;           // Petals version
    
    // Model serving
    std::string model_id;
    std::vector<uint32_t> served_blocks;  // Which transformer blocks
    
    // Hardware
    std::string gpu_name;
    uint64_t gpu_memory_mb;
    
    // Performance
    double throughput_tokens_per_sec;
    double latency_ms;
    bool online;
    
    // Network
    std::string multiaddr;         // Libp2p multiaddress
    uint64_t last_seen;
};

// ============================================================================
// Petals Swarm Status
// ============================================================================

struct PetalsSwarmStatus {
    std::string model_id;
    uint32_t total_blocks;
    uint32_t available_blocks;
    std::vector<PetalsNodeInfo> nodes;
    
    // Aggregate stats
    uint64_t total_gpu_memory_mb;
    double avg_throughput;
    uint32_t online_nodes;
    
    bool isModelAvailable() const {
        return available_blocks >= total_blocks;
    }
    
    double coveragePercent() const {
        return total_blocks > 0 ? (100.0 * available_blocks / total_blocks) : 0.0;
    }
};

// ============================================================================
// Inference Session
// ============================================================================

struct InferenceSession {
    uint64_t session_id;
    std::string model_id;
    std::vector<std::string> peer_ids;  // Nodes in the inference chain
    
    // State
    bool active;
    uint32_t tokens_generated;
    uint64_t started_at;
    uint64_t last_activity;
    
    // Performance tracking
    double avg_tokens_per_sec;
    uint64_t total_latency_ms;
};

// ============================================================================
// Callbacks
// ============================================================================

using TokenCallback = std::function<void(const std::string& token)>;
using CompletionCallback = std::function<void(const std::string& full_response, uint32_t tokens, double tps)>;
using ErrorCallback = std::function<void(const std::string& error)>;

// ============================================================================
// Petals Client
// ============================================================================

class PetalsClient {
private:
    PetalsConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> session_counter_{0};
    
    // Active sessions
    std::unordered_map<uint64_t, InferenceSession> sessions_;
    mutable std::mutex sessions_mutex_;
    
    // Request queue for async processing
    struct PendingRequest {
        uint64_t session_id;
        std::string prompt;
        uint32_t max_tokens;
        double temperature;
        double top_p;
        TokenCallback on_token;
        CompletionCallback on_complete;
        ErrorCallback on_error;
    };
    
    std::queue<PendingRequest> request_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    
    void workerLoop() {
        while (running_) {
            PendingRequest request;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { 
                    return !running_ || !request_queue_.empty(); 
                });
                
                if (!running_) break;
                if (request_queue_.empty()) continue;
                
                request = std::move(request_queue_.front());
                request_queue_.pop();
            }
            
            // Process the request
            processRequest(request);
        }
    }
    
    // Connect to Petals bridge and send request
    bool connectToBridge(const std::string& json_request, std::string& json_response) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(config_.bridge_port);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            return false;
        }
        
        // Send request
        send(sock, json_request.c_str(), json_request.length(), 0);
        
        // Receive response
        char buffer[arqon::RECV_BUFFER_SIZE];
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        close(sock);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            json_response = buffer;
            return true;
        }
        return false;
    }
    
    // Simple JSON parser for response
    std::string extractJsonString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;
        if (json[pos-1] != '"') return "";
        
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
    
    int extractJsonInt(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        
        pos += search.length();
        while (pos < json.length() && json[pos] == ' ') pos++;
        
        int value = 0;
        while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9') {
            value = value * 10 + (json[pos] - '0');
            pos++;
        }
        return value;
    }
    
    std::string escapeJson(const std::string& input) {
        std::string output;
        output.reserve(input.length() * 2);
        for (char c : input) {
            switch (c) {
                case '"': output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default: output += c;
            }
        }
        return output;
    }
    
    void processRequest(const PendingRequest& request) {
        auto start = std::chrono::high_resolution_clock::now();
        std::string response;
        uint32_t tokens = 0;
        
        // Build JSON request for Arqon Inference Bridge (supports Ollama + Petals)
        std::string escaped_prompt = escapeJson(request.prompt);
        std::string json_request = "{\"action\":\"infer\",\"session_id\":" + std::to_string(request.session_id) +
            ",\"prompt\":\"" + escaped_prompt + 
            "\",\"max_tokens\":" + std::to_string(request.max_tokens) +
            ",\"temperature\":" + std::to_string(request.temperature) + "}";
        
        std::string json_response;
        bool bridge_success = connectToBridge(json_request, json_response);
        
        if (bridge_success) {
            // Parse response from Arqon Inference Bridge
            response = extractJsonString(json_response, "text");
            tokens = extractJsonInt(json_response, "tokens_generated");
            std::string error = extractJsonString(json_response, "error");
            std::string backend = extractJsonString(json_response, "backend");
            
            if (!error.empty() && error != "null") {
                std::cerr << "[Petals] Bridge error: " << error << std::endl;
            }
            
            // Stream tokens to callback (simplified - sends whole response)
            if (request.on_token && !response.empty()) {
                request.on_token(response);
            }
        } else {
            // Fallback: Bridge not available
            response = "[Inference bridge not running. Start with: python src/inference_bridge/arqon_inference_bridge.py]";
            tokens = 0;
            
            if (request.on_error) {
                request.on_error("Arqon Inference Bridge not available on port " + std::to_string(config_.bridge_port));
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        double tps = duration_ms > 0 ? (tokens * static_cast<double>(arqon::MS_PER_SECOND) / duration_ms) : 0;
        
        // Update session
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(request.session_id);
            if (it != sessions_.end()) {
                it->second.tokens_generated += tokens;
                it->second.total_latency_ms += duration_ms;
                it->second.avg_tokens_per_sec = tps;
                it->second.last_activity = std::chrono::system_clock::now().time_since_epoch().count();
            }
        }
        
        if (request.on_complete) {
            request.on_complete(response, tokens, tps);
        }
    }
    
public:
    explicit PetalsClient(const PetalsConfig& config = PetalsConfig())
        : config_(config) {}
    
    ~PetalsClient() {
        stop();
    }
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    void start() {
        if (running_) return;
        running_ = true;
        worker_thread_ = std::thread(&PetalsClient::workerLoop, this);
        std::cout << "[Petals] Client started" << std::endl;
    }
    
    void stop() {
        if (!running_) return;
        running_ = false;
        queue_cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        std::cout << "[Petals] Client stopped" << std::endl;
    }
    
    bool isRunning() const { return running_; }
    
    // ========================================================================
    // Health & Discovery
    // ========================================================================
    
    PetalsSwarmStatus getSwarmStatus(const std::string& model_id) const {
        PetalsSwarmStatus status;
        status.model_id = model_id;
        
        // Fetch real status from health.petals.dev
        std::string json_response;
        if (fetchSwarmStatusFromApi(model_id, json_response)) {
            parseSwarmStatus(json_response, status);
        } else {
            // API unavailable - return unknown status
            status.total_blocks = 0;
            status.available_blocks = 0;
            status.online_nodes = 0;
            status.total_gpu_memory_mb = 0;
            status.avg_throughput = 0;
        }
        
        return status;
    }
    
private:
    bool fetchSwarmStatusFromApi(const std::string& model_id, std::string& response) const {
        // Connect to health.petals.dev API
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        
        struct hostent* host = gethostbyname("health.petals.dev");
        if (!host) {
            close(sock);
            return false;
        }
        
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(443);  // HTTPS
        std::memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);
        
        // Set timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            // Fallback to HTTP on port 80
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) return false;
            
            server_addr.sin_port = htons(80);
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                close(sock);
                return false;
            }
        }
        
        // Build HTTP request
        std::string encoded_model = model_id;
        for (size_t i = 0; i < encoded_model.size(); i++) {
            if (encoded_model[i] == '/') encoded_model.replace(i, 1, "%2F");
        }
        
        std::string request = "GET /api/v1/state?model=" + encoded_model + " HTTP/1.1\r\n"
                              "Host: health.petals.dev\r\n"
                              "Connection: close\r\n\r\n";
        
        send(sock, request.c_str(), request.length(), 0);
        
        // Read response
        char buffer[arqon::SMALL_BUFFER_SIZE];
        std::string full_response;
        ssize_t bytes;
        while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes] = '\0';
            full_response += buffer;
        }
        close(sock);
        
        // Extract JSON body (after headers)
        size_t body_start = full_response.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            response = full_response.substr(body_start + 4);
            return !response.empty();
        }
        return false;
    }
    
    void parseSwarmStatus(const std::string& json, PetalsSwarmStatus& status) const {
        // Parse JSON response from health.petals.dev
        auto extractInt = [&json](const std::string& key) -> int {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return 0;
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            int value = 0;
            bool negative = false;
            if (json[pos] == '-') { negative = true; pos++; }
            while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9') {
                value = value * 10 + (json[pos] - '0');
                pos++;
            }
            return negative ? -value : value;
        };
        
        auto extractFloat = [&json](const std::string& key) -> double {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return 0.0;
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            return std::strtod(json.c_str() + pos, nullptr);
        };
        
        status.total_blocks = extractInt("n_blocks");
        status.available_blocks = extractInt("server_rows");
        status.online_nodes = extractInt("num_servers");
        status.total_gpu_memory_mb = extractInt("total_gpu_memory_mb");
        status.avg_throughput = extractFloat("throughput");
    }
    
public:
    
    std::vector<std::string> getAvailableModels() const {
        return config_.supported_models;
    }
    
    // ========================================================================
    // Session Management
    // ========================================================================
    
    uint64_t createSession(const std::string& model_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        
        uint64_t session_id = ++session_counter_;
        
        InferenceSession session;
        session.session_id = session_id;
        session.model_id = model_id;
        session.active = true;
        session.tokens_generated = 0;
        session.started_at = std::chrono::system_clock::now().time_since_epoch().count();
        session.last_activity = session.started_at;
        session.avg_tokens_per_sec = 0;
        session.total_latency_ms = 0;
        
        sessions_[session_id] = session;
        
        std::cout << "[Petals] Session " << session_id << " created for " << model_id << std::endl;
        
        return session_id;
    }
    
    void closeSession(uint64_t session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second.active = false;
            std::cout << "[Petals] Session " << session_id << " closed. "
                      << "Tokens: " << it->second.tokens_generated 
                      << ", Avg TPS: " << it->second.avg_tokens_per_sec << std::endl;
        }
    }
    
    const InferenceSession* getSession(uint64_t session_id) const {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        return it != sessions_.end() ? &it->second : nullptr;
    }
    
    // ========================================================================
    // Inference
    // ========================================================================
    
    void generate(uint64_t session_id, 
                  const std::string& prompt,
                  TokenCallback on_token = nullptr,
                  CompletionCallback on_complete = nullptr,
                  ErrorCallback on_error = nullptr,
                  uint32_t max_tokens = 0,
                  double temperature = -1,
                  double top_p = -1) {
        
        if (!running_) {
            if (on_error) on_error("Client not running");
            return;
        }
        
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end() || !it->second.active) {
                if (on_error) on_error("Invalid or inactive session");
                return;
            }
        }
        
        PendingRequest request;
        request.session_id = session_id;
        request.prompt = prompt;
        request.max_tokens = max_tokens > 0 ? max_tokens : config_.default_max_tokens;
        request.temperature = temperature >= 0 ? temperature : config_.default_temperature;
        request.top_p = top_p >= 0 ? top_p : config_.default_top_p;
        request.on_token = on_token;
        request.on_complete = on_complete;
        request.on_error = on_error;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            request_queue_.push(std::move(request));
        }
        queue_cv_.notify_one();
    }
    
    // Synchronous version
    std::string generateSync(uint64_t session_id,
                             const std::string& prompt,
                             uint32_t max_tokens = 0,
                             double temperature = -1,
                             double top_p = -1) {
        std::string result;
        std::mutex result_mutex;
        std::condition_variable result_cv;
        bool done = false;
        
        generate(session_id, prompt,
            nullptr,  // on_token
            [&](const std::string& response, uint32_t, double) {
                std::lock_guard<std::mutex> lock(result_mutex);
                result = response;
                done = true;
                result_cv.notify_one();
            },
            [&](const std::string& error) {
                std::lock_guard<std::mutex> lock(result_mutex);
                result = "ERROR: " + error;
                done = true;
                result_cv.notify_one();
            },
            max_tokens, temperature, top_p
        );
        
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait(lock, [&] { return done; });
        
        return result;
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct ClientStats {
        uint64_t total_sessions;
        uint64_t active_sessions;
        uint64_t total_tokens_generated;
        uint64_t pending_requests;
        double avg_tokens_per_sec;
    };
    
    ClientStats getStats() const {
        ClientStats stats{};
        
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            stats.total_sessions = sessions_.size();
            
            double total_tps = 0;
            for (const auto& [id, session] : sessions_) {
                if (session.active) stats.active_sessions++;
                stats.total_tokens_generated += session.tokens_generated;
                total_tps += session.avg_tokens_per_sec;
            }
            
            if (stats.active_sessions > 0) {
                stats.avg_tokens_per_sec = total_tps / stats.active_sessions;
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex_));
            stats.pending_requests = request_queue_.size();
        }
        
        return stats;
    }
    
    void printStats() const {
        auto stats = getStats();
        
        std::cout << "\n[Petals Client Stats]" << std::endl;
        std::cout << "  Sessions: " << stats.active_sessions << "/" << stats.total_sessions << " active" << std::endl;
        std::cout << "  Tokens Generated: " << stats.total_tokens_generated << std::endl;
        std::cout << "  Pending Requests: " << stats.pending_requests << std::endl;
        std::cout << "  Avg TPS: " << stats.avg_tokens_per_sec << std::endl;
    }
};

// ============================================================================
// Petals Server (for running as a node)
// ============================================================================

struct PetalsServerConfig {
    std::string model_id;
    std::vector<uint32_t> blocks_to_serve;  // Which transformer blocks
    uint16_t port = 31330;
    std::string public_name;
    bool announce_to_dht = true;
    
    // Hardware limits
    uint64_t max_batch_size = 8;
    uint64_t max_sequence_length = 2048;
};

class PetalsServer {
private:
    PetalsServerConfig config_;
    std::atomic<bool> running_{false};
    
    // Stats
    std::atomic<uint64_t> requests_served_{0};
    std::atomic<uint64_t> tokens_generated_{0};
    
public:
    explicit PetalsServer(const PetalsServerConfig& config)
        : config_(config) {}
    
    void start() {
        if (running_) return;
        running_ = true;
        
        std::cout << "[Petals Server] Starting on port " << config_.port << std::endl;
        std::cout << "[Petals Server] Model: " << config_.model_id << std::endl;
        std::cout << "[Petals Server] Blocks: ";
        for (size_t i = 0; i < config_.blocks_to_serve.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << config_.blocks_to_serve[i];
        }
        std::cout << std::endl;
        
        // In real implementation:
        // 1. Load model blocks into GPU memory
        // 2. Connect to Petals DHT
        // 3. Announce available blocks
        // 4. Start serving inference requests
    }
    
    void stop() {
        if (!running_) return;
        running_ = false;
        std::cout << "[Petals Server] Stopped" << std::endl;
    }
    
    bool isRunning() const { return running_; }
    
    uint64_t getRequestsServed() const { return requests_served_; }
    uint64_t getTokensGenerated() const { return tokens_generated_; }
    
    const PetalsServerConfig& getConfig() const { return config_; }
};

} // namespace inference
