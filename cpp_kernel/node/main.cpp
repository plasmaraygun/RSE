/**
 * Arqon Node Main Entry Point
 */

#include "ArqonNode.h"
#include <csignal>
#include <iostream>

node::ArqonNode* g_node = nullptr;

void signalHandler(int sig) {
    std::cout << "\n[Main] Received signal " << sig << ", shutting down..." << std::endl;
    if (g_node) g_node->stop();
}

int main(int argc, char* argv[]) {
    node::NodeConfig config;
    
    // Parse args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--data-dir" && i + 1 < argc) {
            config.data_dir = argv[++i];
        } else if (arg == "--p2p-port" && i + 1 < argc) {
            config.p2p_port = std::stoi(argv[++i]);
        } else if (arg == "--api-port" && i + 1 < argc) {
            config.api_port = std::stoi(argv[++i]);
        } else if (arg == "--bootstrap" && i + 1 < argc) {
            config.bootstrap_peers.push_back(argv[++i]);
        } else if (arg == "--no-inference") {
            config.enable_inference = false;
        } else if (arg == "--no-api") {
            config.enable_api = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: arqon-node [options]\n";
            std::cout << "  --data-dir <path>    Data directory (default: ./arqon_data)\n";
            std::cout << "  --p2p-port <port>    P2P port (default: 31330)\n";
            std::cout << "  --api-port <port>    API port (default: 8080)\n";
            std::cout << "  --bootstrap <addr>   Bootstrap peer (ip:port)\n";
            std::cout << "  --no-inference       Disable inference\n";
            std::cout << "  --no-api             Disable API server\n";
            return 0;
        }
    }
    
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create and start node
    node::ArqonNode node(config);
    g_node = &node;
    
    if (!node.start()) {
        std::cerr << "[Main] Failed to start node" << std::endl;
        return 1;
    }
    
    // Wait for shutdown
    node.wait();
    
    return 0;
}
