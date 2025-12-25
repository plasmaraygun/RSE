/**
 * Arqon CLI - Connected to Real State
 * 
 * Connects to running ArqonNode via API
 */

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ANSI colors
namespace color {
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string CYAN = "\033[36m";
}

std::string API_HOST = "127.0.0.1";
int API_PORT = 8080;

// HTTP GET request
std::string httpGet(const std::string& path) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";
    
    struct sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(API_PORT);
    inet_pton(AF_INET, API_HOST.c_str(), &server.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        close(sock);
        return "";
    }
    
    std::string request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + API_HOST + "\r\n";
    request += "Connection: close\r\n\r\n";
    
    send(sock, request.c_str(), request.size(), 0);
    
    char buffer[8192];
    std::string response;
    ssize_t n;
    while ((n = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        response += buffer;
    }
    
    close(sock);
    
    // Extract body (after \r\n\r\n)
    size_t body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        return response.substr(body_start + 4);
    }
    return response;
}

// Simple JSON value extractor
std::string jsonGet(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos += search.length();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    
    size_t end = pos;
    if (json[pos - 1] == '"') {
        // String value
        end = json.find('"', pos);
    } else {
        // Number or other
        while (end < json.size() && json[end] != ',' && json[end] != '}') end++;
    }
    
    return json.substr(pos, end - pos);
}

void printBanner() {
    std::cout << color::CYAN << R"(
    _                             
   / \   _ __ __ _  ___  _ __    
  / _ \ | '__/ _` |/ _ \| '_ \   
 / ___ \| | | (_| | (_) | | | |  
/_/   \_\_|  \__, |\___/|_| |_|  
                |_|  CLI v2.0 (Real)
)" << color::RESET << std::endl;
}

void printHelp() {
    std::cout << color::BOLD << "Usage:" << color::RESET << " arqon <command> [options]\n\n";
    std::cout << color::BOLD << "Commands:\n" << color::RESET;
    std::cout << "  status              Show network status (from node)\n";
    std::cout << "  balance <address>   Show wallet balance (from node)\n";
    std::cout << "  validators          List validators\n";
    std::cout << "  nodes               List inference nodes\n";
    std::cout << "  consensus           Show consensus state\n";
    std::cout << "  help                Show this help\n";
    std::cout << "\n" << color::YELLOW << "Note: Requires ArqonNode running on localhost:8080" << color::RESET << std::endl;
}

bool checkConnection() {
    std::string resp = httpGet("/api/status");
    return !resp.empty() && resp.find("online") != std::string::npos;
}

void cmdStatus() {
    std::string json = httpGet("/api/status");
    if (json.empty()) {
        std::cout << color::RED << "✗ Cannot connect to node at " << API_HOST << ":" << API_PORT << color::RESET << std::endl;
        std::cout << "  Make sure ArqonNode is running" << std::endl;
        return;
    }
    
    std::cout << color::BOLD << "═══ Network Status ═══" << color::RESET << std::endl;
    std::cout << "  Status:      " << color::GREEN << "● " << jsonGet(json, "status") << color::RESET << std::endl;
    std::cout << "  Epoch:       " << jsonGet(json, "epoch") << std::endl;
    std::cout << "  Height:      " << jsonGet(json, "height") << std::endl;
    std::cout << "  Validators:  " << jsonGet(json, "validators") << std::endl;
    std::cout << "  Total Staked: " << jsonGet(json, "total_staked") << " Q" << std::endl;
    std::cout << "  Inf. Nodes:  " << jsonGet(json, "inference_nodes") << std::endl;
    std::cout << "  GPU Nodes:   " << jsonGet(json, "gpu_nodes") << std::endl;
    std::cout << "  Total TFLOPS: " << jsonGet(json, "total_tflops") << std::endl;
}

void cmdBalance(const std::string& addr) {
    if (addr.empty()) {
        std::cout << color::RED << "Usage: arqon balance <address>" << color::RESET << std::endl;
        return;
    }
    
    std::string json = httpGet("/api/balance/" + addr);
    if (json.empty()) {
        std::cout << color::RED << "✗ Cannot connect to node" << color::RESET << std::endl;
        return;
    }
    
    if (json.find("error") != std::string::npos) {
        std::cout << color::RED << "Error: " << jsonGet(json, "error") << color::RESET << std::endl;
        return;
    }
    
    std::cout << color::BOLD << "═══ Account ═══" << color::RESET << std::endl;
    std::cout << "  Address:  " << color::CYAN << jsonGet(json, "address") << color::RESET << std::endl;
    std::cout << "  Balance:  " << color::GREEN << jsonGet(json, "balance_arqon") << " ARQN" << color::RESET << std::endl;
    std::cout << "  Staked:   " << color::YELLOW << jsonGet(json, "stake_arqon") << " ARQN" << color::RESET << std::endl;
    std::cout << "  Nonce:    " << jsonGet(json, "nonce") << std::endl;
}

void cmdValidators() {
    std::string json = httpGet("/api/validators");
    if (json.empty()) {
        std::cout << color::RED << "✗ Cannot connect to node" << color::RESET << std::endl;
        return;
    }
    
    std::cout << color::BOLD << "═══ Validators ═══" << color::RESET << std::endl;
    std::cout << "  Count: " << jsonGet(json, "count") << std::endl;
    
    // Parse validators array (simplified)
    size_t pos = json.find("\"validators\":[");
    if (pos != std::string::npos) {
        std::cout << "\n  " << color::BOLD << "Address                                      Stake" << color::RESET << std::endl;
        std::cout << "  ─────────────────────────────────────────────────────" << std::endl;
        
        // Find each validator object
        size_t start = pos;
        while ((start = json.find("{\"address\":", start)) != std::string::npos) {
            size_t end = json.find("}", start);
            std::string v = json.substr(start, end - start + 1);
            std::cout << "  " << jsonGet(v, "address").substr(0, 42) << "  " 
                      << jsonGet(v, "stake_arqon") << " ARQN" << std::endl;
            start = end;
        }
    }
}

void cmdNodes() {
    std::string json = httpGet("/api/nodes");
    if (json.empty()) {
        std::cout << color::RED << "✗ Cannot connect to node" << color::RESET << std::endl;
        return;
    }
    
    std::cout << color::BOLD << "═══ Inference Network ═══" << color::RESET << std::endl;
    std::cout << "  Total Nodes:  " << jsonGet(json, "total") << std::endl;
    std::cout << "  GPU Nodes:    " << jsonGet(json, "gpu") << std::endl;
    std::cout << "  Relay Nodes:  " << jsonGet(json, "relay") << std::endl;
    std::cout << "  Total TFLOPS: " << jsonGet(json, "tflops") << std::endl;
    std::cout << "  Epoch:        " << jsonGet(json, "epoch") << std::endl;
}

void cmdConsensus() {
    std::string json = httpGet("/api/consensus");
    if (json.empty()) {
        std::cout << color::RED << "✗ Cannot connect to node" << color::RESET << std::endl;
        return;
    }
    
    std::cout << color::BOLD << "═══ Consensus State ═══" << color::RESET << std::endl;
    std::cout << "  Epoch:        " << jsonGet(json, "epoch") << std::endl;
    std::cout << "  Height:       " << jsonGet(json, "height") << std::endl;
    std::cout << "  Validators:   " << jsonGet(json, "validators") << std::endl;
    std::cout << "  Total Staked: " << jsonGet(json, "total_staked_arqon") << " ARQN" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printBanner();
        printHelp();
        return 0;
    }
    
    std::string cmd = argv[1];
    
    // Check for --port flag
    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "--port") {
            API_PORT = std::stoi(argv[i + 1]);
        }
        if (std::string(argv[i]) == "--host") {
            API_HOST = argv[i + 1];
        }
    }
    
    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        printHelp();
    } else if (cmd == "status") {
        cmdStatus();
    } else if (cmd == "balance") {
        std::string addr = (argc > 2) ? argv[2] : "";
        cmdBalance(addr);
    } else if (cmd == "validators") {
        cmdValidators();
    } else if (cmd == "nodes") {
        cmdNodes();
    } else if (cmd == "consensus") {
        cmdConsensus();
    } else {
        std::cout << color::RED << "Unknown command: " << cmd << color::RESET << std::endl;
        printHelp();
        return 1;
    }
    
    return 0;
}
