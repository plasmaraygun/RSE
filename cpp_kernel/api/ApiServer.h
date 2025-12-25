#pragma once

/**
 * Real API Backend for Dashboard
 * 
 * HTTP/JSON API server for RSE network
 * Single-header implementation using POSIX sockets
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../inference/InferenceNode.h"
#include "../storage/PersistentStorage.h"
#include "../consensus/ProofOfStake.h"
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace api {

using namespace crypto;
using namespace economics;
using namespace inference;
using namespace storage;
using namespace consensus;

// Simple JSON builder
class JSON {
public:
    static std::string object(std::initializer_list<std::pair<std::string, std::string>> fields) {
        std::string out = "{";
        bool first = true;
        for (const auto& [k, v] : fields) {
            if (!first) out += ",";
            out += "\"" + k + "\":" + v;
            first = false;
        }
        return out + "}";
    }
    
    static std::string array(const std::vector<std::string>& items) {
        std::string out = "[";
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0) out += ",";
            out += items[i];
        }
        return out + "]";
    }
    
    static std::string str(const std::string& s) { return "\"" + s + "\""; }
    static std::string num(uint64_t n) { return std::to_string(n); }
    static std::string num(double n) { return std::to_string(n); }
    static std::string boolean(bool b) { return b ? "true" : "false"; }
    
    // JSON Parser - extract string value
    static std::string getString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return "";
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }
    
    // JSON Parser - extract number value
    static uint64_t getNumber(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return 0;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        std::string num_str;
        while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9')) {
            num_str += json[pos++];
        }
        return num_str.empty() ? 0 : std::stoull(num_str);
    }
};

// HTTP response
struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    
    std::string build() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " OK\r\n";
        oss << "Content-Type: " << content_type << "\r\n";
        oss << "Access-Control-Allow-Origin: *\r\n";
        oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        oss << "Access-Control-Allow-Headers: Content-Type\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "\r\n";
        oss << body;
        return oss.str();
    }
};

// API Server
class ApiServer {
public:
    ApiServer(AccountManager& accounts, InferenceNetworkManager& inference, PoSConsensus& consensus)
        : accounts_(accounts), inference_(inference), consensus_(consensus), 
          running_(false), server_fd_(-1) {}
    
    ~ApiServer() { stop(); }
    
    bool start(uint16_t port = 8080) {
        if (running_) return true;
        
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) return false;
        
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(server_fd_);
            return false;
        }
        
        if (listen(server_fd_, 10) < 0) {
            close(server_fd_);
            return false;
        }
        
        running_ = true;
        server_thread_ = std::thread([this]() { acceptLoop(); });
        
        std::cout << "[API] Server started on port " << port << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_) return;
        running_ = false;
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        std::cout << "[API] Server stopped" << std::endl;
    }

private:
    void acceptLoop() {
        while (running_) {
            struct sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) continue;
            
            // Handle in thread
            std::thread([this, client_fd]() {
                handleClient(client_fd);
                close(client_fd);
            }).detach();
        }
    }
    
    void handleClient(int fd) {
        char buffer[4096];
        ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) return;
        buffer[n] = '\0';
        
        std::string request(buffer);
        HttpResponse response = handleRequest(request);
        std::string resp_str = response.build();
        send(fd, resp_str.c_str(), resp_str.size(), 0);
    }
    
    HttpResponse handleRequest(const std::string& request) {
        // Parse method and path
        std::string method, path;
        std::istringstream iss(request);
        iss >> method >> path;
        
        // Handle CORS preflight
        if (method == "OPTIONS") {
            return {200, "text/plain", ""};
        }
        
        // Route requests
        if (path == "/api/status") return getStatus();
        if (path == "/api/accounts") return getAccounts();
        if (path == "/api/validators") return getValidators();
        if (path == "/api/nodes") return getNodes();
        if (path == "/api/consensus") return getConsensus();
        if (path.rfind("/api/balance/", 0) == 0) {
            std::string addr = path.substr(13);
            return getBalance(addr);
        }
        if (path.rfind("/api/account/", 0) == 0) {
            std::string addr = path.substr(13);
            return getAccount(addr);
        }
        
        // POST endpoints
        if (method == "POST") {
            // Extract body
            size_t body_start = request.find("\r\n\r\n");
            std::string body = (body_start != std::string::npos) ? request.substr(body_start + 4) : "";
            
            if (path == "/api/transfer") return postTransfer(body);
            if (path == "/api/tx") return postTransfer(body);  // Alias
            if (path == "/api/stake") return postStake(body);
            if (path == "/api/unstake") return postUnstake(body);
            if (path == "/api/inference") return postInference(body);
        }
        
        return {404, "application/json", JSON::object({{"error", JSON::str("Not found")}})};
    }
    
    // GET /api/status
    HttpResponse getStatus() {
        auto inf_stats = inference_.getStats();
        std::string body = JSON::object({
            {"status", JSON::str("online")},
            {"epoch", JSON::num(consensus_.currentEpoch())},
            {"height", JSON::num(consensus_.currentHeight())},
            {"validators", JSON::num(consensus_.validatorCount())},
            {"total_staked", JSON::num(consensus_.totalStaked())},
            {"inference_nodes", JSON::num(inf_stats.total_nodes)},
            {"gpu_nodes", JSON::num(inf_stats.gpu_nodes)},
            {"total_tflops", JSON::num(inf_stats.network_avg_tps)}
        });
        return {200, "application/json", body};
    }
    
    // GET /api/balance/:address
    HttpResponse getBalance(const std::string& addr_hex) {
        Address addr{};
        AddressUtil::fromHex(addr_hex, addr);
        const auto& account = accounts_.getAccount(addr);
        return {200, "application/json", JSON::object({
            {"address", JSON::str(addr_hex)},
            {"balance", JSON::num(account.balance)},
            {"balance_arqon", JSON::num(qToArqon(account.balance))},
            {"stake", JSON::num(account.stake)},
            {"stake_arqon", JSON::num(qToArqon(account.stake))},
            {"nonce", JSON::num(account.nonce)}
        })};
    }
    
    // GET /api/account/:address
    HttpResponse getAccount(const std::string& addr_hex) {
        return getBalance(addr_hex);  // Same for now
    }
    
    // GET /api/accounts
    HttpResponse getAccounts() {
        return {200, "application/json", JSON::object({
            {"count", JSON::num(accounts_.getAccountCount())},
            {"total_supply", JSON::num(accounts_.getTotalSupply())}
        })};
    }
    
    // GET /api/validators
    HttpResponse getValidators() {
        const auto& validators = accounts_.getValidators();
        std::vector<std::string> items;
        for (const auto& v : validators) {
            const auto& acc = accounts_.getAccount(v);
            items.push_back(JSON::object({
                {"address", JSON::str(AddressUtil::toHex(v))},
                {"stake", JSON::num(acc.stake)},
                {"stake_arqon", JSON::num(qToArqon(acc.stake))}
            }));
        }
        return {200, "application/json", JSON::object({
            {"count", JSON::num(validators.size())},
            {"validators", JSON::array(items)}
        })};
    }
    
    // GET /api/nodes
    HttpResponse getNodes() {
        auto stats = inference_.getStats();
        std::string body = JSON::object({
            {"total", JSON::num(stats.total_nodes)},
            {"gpu", JSON::num(stats.gpu_nodes)},
            {"relay", JSON::num(stats.relay_nodes)},
            {"tflops", JSON::num(stats.network_avg_tps)},
            {"epoch", JSON::num(stats.current_epoch)}
        });
        return {200, "application/json", body};
    }
    
    // GET /api/consensus
    HttpResponse getConsensus() {
        return {200, "application/json", JSON::object({
            {"epoch", JSON::num(consensus_.currentEpoch())},
            {"height", JSON::num(consensus_.currentHeight())},
            {"validators", JSON::num(consensus_.validatorCount())},
            {"total_staked", JSON::num(consensus_.totalStaked())},
            {"total_staked_arqon", JSON::num(qToArqon(consensus_.totalStaked()))}
        })};
    }
    
    // POST /api/transfer - REAL implementation
    HttpResponse postTransfer(const std::string& body) {
        std::string from_hex = JSON::getString(body, "from");
        std::string to_hex = JSON::getString(body, "to");
        uint64_t amount = JSON::getNumber(body, "amount");
        
        if (from_hex.empty() || to_hex.empty() || amount == 0) {
            return {400, "application/json", JSON::object({{"error", JSON::str("Missing or invalid fields: from, to, amount")}})};
        }
        
        Address from_addr{}, to_addr{};
        AddressUtil::fromHex(from_hex, from_addr);
        AddressUtil::fromHex(to_hex, to_addr);
        
        // Check balance
        const auto& from_acc = accounts_.getAccount(from_addr);
        if (from_acc.balance < amount) {
            return {400, "application/json", JSON::object({{"error", JSON::str("Insufficient balance")}})};
        }
        
        // Execute transfer
        bool success = accounts_.transfer(from_addr, to_addr, amount);
        if (!success) {
            return {500, "application/json", JSON::object({{"error", JSON::str("Transfer failed")}})};
        }
        
        // Add to mempool for consensus
        mempool_.push_back({from_addr, to_addr, amount, from_acc.nonce, TxType::TRANSFER});
        
        return {200, "application/json", JSON::object({
            {"status", JSON::str("success")},
            {"from", JSON::str(from_hex)},
            {"to", JSON::str(to_hex)},
            {"amount", JSON::num(amount)},
            {"amount_arqon", JSON::num(qToArqon(amount))}
        })};
    }
    
    // POST /api/stake - REAL implementation
    HttpResponse postStake(const std::string& body) {
        std::string addr_hex = JSON::getString(body, "address");
        uint64_t amount = JSON::getNumber(body, "amount");
        
        if (addr_hex.empty() || amount == 0) {
            return {400, "application/json", JSON::object({{"error", JSON::str("Missing address or amount")}})};
        }
        
        Address addr{};
        AddressUtil::fromHex(addr_hex, addr);
        
        // Check balance
        const auto& acc = accounts_.getAccount(addr);
        if (acc.balance < amount) {
            return {400, "application/json", JSON::object({{"error", JSON::str("Insufficient balance for stake")}})};
        }
        
        // Execute stake
        bool success = accounts_.stake(addr, amount);
        if (!success) {
            return {500, "application/json", JSON::object({{"error", JSON::str("Stake failed")}})};
        }
        
        // Register as validator if meets threshold
        if (acc.stake + amount >= MIN_STAKE) {
            consensus_.registerValidator(addr);
        }
        
        mempool_.push_back({addr, addr, amount, acc.nonce, TxType::STAKE});
        
        return {200, "application/json", JSON::object({
            {"status", JSON::str("success")},
            {"address", JSON::str(addr_hex)},
            {"staked", JSON::num(amount)},
            {"total_stake", JSON::num(acc.stake + amount)}
        })};
    }
    
    // POST /api/unstake - REAL implementation
    HttpResponse postUnstake(const std::string& body) {
        std::string addr_hex = JSON::getString(body, "address");
        uint64_t amount = JSON::getNumber(body, "amount");
        
        if (addr_hex.empty() || amount == 0) {
            return {400, "application/json", JSON::object({{"error", JSON::str("Missing address or amount")}})};
        }
        
        Address addr{};
        AddressUtil::fromHex(addr_hex, addr);
        
        const auto& acc = accounts_.getAccount(addr);
        if (acc.stake < amount) {
            return {400, "application/json", JSON::object({{"error", JSON::str("Insufficient stake")}})};
        }
        
        bool success = accounts_.unstake(addr, amount);
        if (!success) {
            return {500, "application/json", JSON::object({{"error", JSON::str("Unstake failed")}})};
        }
        
        mempool_.push_back({addr, addr, amount, acc.nonce, TxType::UNSTAKE});
        
        return {200, "application/json", JSON::object({
            {"status", JSON::str("success")},
            {"address", JSON::str(addr_hex)},
            {"unstaked", JSON::num(amount)},
            {"remaining_stake", JSON::num(acc.stake - amount)}
        })};
    }
    
    // POST /api/inference - Execute inference via Petals bridge
    HttpResponse postInference(const std::string& body) {
        std::string prompt = JSON::getString(body, "prompt");
        std::string model = JSON::getString(body, "model");
        
        if (prompt.empty()) {
            return {400, "application/json", JSON::object({{"error", JSON::str("Missing prompt")}})};
        }
        
        if (model.empty()) {
            model = "meta-llama/Llama-3.1-70B";
        }
        
        // Call Petals bridge (Python process on port 8765)
        std::string response = callPetalsBridge(prompt, model);
        
        if (response.empty()) {
            return {503, "application/json", JSON::object({
                {"error", JSON::str("Inference unavailable - Petals bridge not running")}
            })};
        }
        
        return {200, "application/json", JSON::object({
            {"response", JSON::str(response)},
            {"model", JSON::str(model)},
            {"tokens", JSON::num(response.size() / 4)}  // Rough estimate
        })};
    }
    
    std::string callPetalsBridge(const std::string& prompt, const std::string& model) {
        // Connect to Petals bridge on localhost:8765
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8765);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            return "";
        }
        
        // Send request
        std::string request = JSON::object({
            {"prompt", JSON::str(prompt)},
            {"model", JSON::str(model)}
        });
        send(sock, request.c_str(), request.size(), 0);
        
        // Receive response
        char buffer[8192];
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        close(sock);
        
        if (n <= 0) return "";
        buffer[n] = '\0';
        
        return JSON::getString(std::string(buffer), "response");
    }
    
    // Mempool transaction types
    enum class TxType { TRANSFER, STAKE, UNSTAKE };
    struct MempoolTx {
        Address from;
        Address to;
        uint64_t amount;
        uint64_t nonce;
        TxType type;
    };
    
    std::vector<MempoolTx> mempool_;
    
    AccountManager& accounts_;
    InferenceNetworkManager& inference_;
    PoSConsensus& consensus_;
    
    std::atomic<bool> running_;
    int server_fd_;
    std::thread server_thread_;
};

} // namespace api
