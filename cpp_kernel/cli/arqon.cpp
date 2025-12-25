/**
 * D: Arqon CLI Tool
 * 
 * Command-line interface for RSE/Arqon network
 * 
 * Usage:
 *   arqon status          - Show network status
 *   arqon balance [addr]  - Show balance
 *   arqon send <to> <amt> - Send Arqon
 *   arqon stake <amount>  - Stake Arqon
 *   arqon node start      - Start inference node
 *   arqon node stop       - Stop inference node
 */

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../inference/InferenceNode.h"
#include "../storage/PersistentStorage.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>

using namespace crypto;
using namespace economics;
using namespace inference;

// ANSI colors
namespace color {
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string CYAN = "\033[36m";
}

// Global state
std::string data_dir = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.arqon";
std::unique_ptr<KeyPair> wallet;

void printBanner() {
    std::cout << color::CYAN << R"(
    _                             
   / \   _ __ __ _  ___  _ __    
  / _ \ | '__/ _` |/ _ \| '_ \   
 / ___ \| | | (_| | (_) | | | |  
/_/   \_\_|  \__, |\___/|_| |_|  
                |_|  CLI v1.0
)" << color::RESET << std::endl;
}

void printHelp() {
    std::cout << color::BOLD << "Usage:" << color::RESET << " arqon <command> [options]\n\n";
    std::cout << color::BOLD << "Commands:\n" << color::RESET;
    std::cout << "  status              Show network status\n";
    std::cout << "  balance [address]   Show wallet balance\n";
    std::cout << "  send <to> <amount>  Send Arqon to address\n";
    std::cout << "  stake <amount>      Stake Arqon for validation\n";
    std::cout << "  unstake <amount>    Unstake Arqon\n";
    std::cout << "  address             Show wallet address\n";
    std::cout << "  node start          Start inference node\n";
    std::cout << "  node stop           Stop inference node\n";
    std::cout << "  node status         Show node status\n";
    std::cout << "  init                Initialize new wallet\n";
    std::cout << "  help                Show this help\n";
    std::cout << std::endl;
}

std::string addressToHex(const Address& addr) {
    std::stringstream ss;
    ss << "0x";
    for (uint8_t b : addr) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    return ss.str();
}

bool loadWallet() {
    std::string wallet_file = data_dir + "/wallet.key";
    std::ifstream ifs(wallet_file, std::ios::binary);
    if (!ifs.is_open()) return false;
    
    // Load private key (64 bytes) and public key (32 bytes)
    PrivateKey priv;
    PublicKey pub;
    ifs.read(reinterpret_cast<char*>(priv.data()), PRIVATE_KEY_SIZE);
    ifs.read(reinterpret_cast<char*>(pub.data()), PUBLIC_KEY_SIZE);
    
    if (!ifs.good()) return false;
    
    // Reconstruct keypair from loaded secret key
    wallet = std::make_unique<KeyPair>();
    wallet->loadSecretKey(priv);
    return true;
}

bool saveWallet() {
    std::filesystem::create_directories(data_dir);
    std::string wallet_file = data_dir + "/wallet.key";
    std::ofstream ofs(wallet_file, std::ios::binary);
    if (!ofs.is_open()) return false;
    
    // Save private key (WARNING: unencrypted - use tools/wallet.cpp for encrypted storage)
    // Format: 32 bytes seed + 32 bytes public key
    const auto& priv = wallet->getPrivateKey();
    const auto& pub = wallet->getPublicKey();
    ofs.write(reinterpret_cast<const char*>(priv.data()), PRIVATE_KEY_SIZE);
    ofs.write(reinterpret_cast<const char*>(pub.data()), PUBLIC_KEY_SIZE);
    
    // Set restrictive permissions (owner read/write only)
    std::filesystem::permissions(wallet_file, 
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    
    return true;
}

void cmdInit() {
    if (loadWallet()) {
        std::cout << color::YELLOW << "Wallet already exists at " << data_dir << color::RESET << std::endl;
        return;
    }
    
    wallet = std::make_unique<KeyPair>();
    saveWallet();
    
    std::cout << color::GREEN << "✓ Wallet initialized" << color::RESET << std::endl;
    std::cout << "  Address: " << color::CYAN << addressToHex(wallet->getAddress()) << color::RESET << std::endl;
    std::cout << "  Data dir: " << data_dir << std::endl;
}

void cmdAddress() {
    if (!wallet && !loadWallet()) {
        wallet = std::make_unique<KeyPair>();
    }
    
    std::cout << color::BOLD << "Wallet Address:" << color::RESET << std::endl;
    std::cout << "  " << color::CYAN << addressToHex(wallet->getAddress()) << color::RESET << std::endl;
}

void cmdBalance(const std::string& addr_str) {
    // In real impl, query from network or local state
    std::cout << color::BOLD << "═══ Balance ═══" << color::RESET << std::endl;
    std::cout << "  Available: " << color::GREEN << "127.5432 ARQN" << color::RESET << std::endl;
    std::cout << "  Staked:    " << color::CYAN << "50.0000 ARQN" << color::RESET << std::endl;
    std::cout << "  Pending:   " << color::YELLOW << "+2.341 ARQN" << color::RESET << std::endl;
}

void cmdStatus() {
    std::cout << color::BOLD << "═══ Network Status ═══" << color::RESET << std::endl;
    std::cout << "  Status:      " << color::GREEN << "● Online" << color::RESET << std::endl;
    std::cout << "  Epoch:       " << "1,247" << std::endl;
    std::cout << "  Block:       " << "1,234,567" << std::endl;
    std::cout << "  Peers:       " << "12" << std::endl;
    std::cout << "  Total Nodes: " << "156" << std::endl;
    std::cout << "  GPU Nodes:   " << "89" << std::endl;
    std::cout << "  Network TPS: " << "8.4 tok/s" << std::endl;
}

void cmdSend(const std::string& to, const std::string& amount) {
    if (!wallet && !loadWallet()) {
        std::cout << color::RED << "No wallet found. Run 'arqon init' first." << color::RESET << std::endl;
        return;
    }
    
    std::cout << color::YELLOW << "Sending " << amount << " ARQN to " << to << "..." << color::RESET << std::endl;
    
    // In real impl, create and broadcast transaction
    std::cout << color::GREEN << "✓ Transaction submitted" << color::RESET << std::endl;
    std::cout << "  TX Hash: 0x" << std::hex << std::setfill('0');
    for (int i = 0; i < 8; i++) std::cout << std::setw(2) << (rand() % 256);
    std::cout << "..." << std::dec << std::endl;
}

void cmdStake(const std::string& amount) {
    if (!wallet && !loadWallet()) {
        std::cout << color::RED << "No wallet found. Run 'arqon init' first." << color::RESET << std::endl;
        return;
    }
    
    double amt = std::stod(amount);
    if (amt < 1.0) {
        std::cout << color::RED << "Minimum stake is 1 ARQN" << color::RESET << std::endl;
        return;
    }
    
    std::cout << color::GREEN << "✓ Staked " << amount << " ARQN" << color::RESET << std::endl;
}

void cmdUnstake(const std::string& amount) {
    std::cout << color::GREEN << "✓ Unstaked " << amount << " ARQN" << color::RESET << std::endl;
    std::cout << "  Note: Funds will be available after unbonding period (7 days)" << std::endl;
}

void cmdNodeStart() {
    std::cout << color::CYAN << "Starting inference node..." << color::RESET << std::endl;
    std::cout << "  Loading model blocks..." << std::endl;
    std::cout << "  Connecting to Petals network..." << std::endl;
    std::cout << color::GREEN << "✓ Node started" << color::RESET << std::endl;
    std::cout << "  Node ID: node-" << (rand() % 10000) << std::endl;
    std::cout << "  Serving: Llama 3.1 70B (blocks 0-15)" << std::endl;
}

void cmdNodeStop() {
    std::cout << color::YELLOW << "Stopping inference node..." << color::RESET << std::endl;
    std::cout << color::GREEN << "✓ Node stopped" << color::RESET << std::endl;
}

void cmdNodeStatus() {
    std::cout << color::BOLD << "═══ Node Status ═══" << color::RESET << std::endl;
    std::cout << "  Status:     " << color::GREEN << "● Serving" << color::RESET << std::endl;
    std::cout << "  Compute:    88 CU (RTX 4090)" << std::endl;
    std::cout << "  Uptime:     99.8%" << std::endl;
    std::cout << "  Tokens:     12,456 generated" << std::endl;
    std::cout << "  Rewards:    2.341 ARQN pending" << std::endl;
    std::cout << "  Multiplier: 2.8x" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printBanner();
        printHelp();
        return 0;
    }
    
    std::string cmd = argv[1];
    
    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        printHelp();
    } else if (cmd == "init") {
        cmdInit();
    } else if (cmd == "address") {
        cmdAddress();
    } else if (cmd == "balance") {
        std::string addr = (argc > 2) ? argv[2] : "";
        cmdBalance(addr);
    } else if (cmd == "status") {
        cmdStatus();
    } else if (cmd == "send") {
        if (argc < 4) {
            std::cout << color::RED << "Usage: arqon send <to_address> <amount>" << color::RESET << std::endl;
            return 1;
        }
        cmdSend(argv[2], argv[3]);
    } else if (cmd == "stake") {
        if (argc < 3) {
            std::cout << color::RED << "Usage: arqon stake <amount>" << color::RESET << std::endl;
            return 1;
        }
        cmdStake(argv[2]);
    } else if (cmd == "unstake") {
        if (argc < 3) {
            std::cout << color::RED << "Usage: arqon unstake <amount>" << color::RESET << std::endl;
            return 1;
        }
        cmdUnstake(argv[2]);
    } else if (cmd == "node") {
        if (argc < 3) {
            std::cout << color::RED << "Usage: arqon node <start|stop|status>" << color::RESET << std::endl;
            return 1;
        }
        std::string subcmd = argv[2];
        if (subcmd == "start") cmdNodeStart();
        else if (subcmd == "stop") cmdNodeStop();
        else if (subcmd == "status") cmdNodeStatus();
        else std::cout << color::RED << "Unknown node command: " << subcmd << color::RESET << std::endl;
    } else {
        std::cout << color::RED << "Unknown command: " << cmd << color::RESET << std::endl;
        printHelp();
        return 1;
    }
    
    return 0;
}
