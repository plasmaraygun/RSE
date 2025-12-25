/**
 * ARQON Wallet CLI
 * 
 * Command-line wallet for interacting with the ARQON blockchain.
 * Features:
 * - Key generation and management
 * - Balance checking
 * - Transaction signing and sending
 * - Staking operations
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <termios.h>
#include <unistd.h>

#include "../core/Crypto.h"
#include "../core/Economics.h"
#include "../core/Persistence.h"
#include "../network/TcpSocket.h"

using namespace crypto;
using namespace economics;
using namespace persistence;
using namespace network;

// ============================================================================
// Wallet Storage
// ============================================================================

struct WalletFile {
    static constexpr uint32_t MAGIC = 0x41524B57;  // "ARKW"
    static constexpr uint32_t VERSION = 1;
    
    uint32_t magic;
    uint32_t version;
    uint8_t encrypted;
    uint8_t salt[16];
    uint8_t nonce[24];
    PrivateKey encrypted_key;
    PublicKey public_key;
    Address address;
    uint32_t checksum;
};

class Wallet {
private:
    KeyPair keypair_;
    std::string wallet_path_;
    bool loaded_;
    
    // Simple password-based encryption using XOR with key derived from password
    // Note: Production should use proper KDF like Argon2
    void deriveKey(const std::string& password, const uint8_t* salt, uint8_t* key, size_t key_len) {
        Hash combined_hash = Blake2b::hash(
            reinterpret_cast<const uint8_t*>(password.data()), password.size());
        
        // Mix with salt
        for (size_t i = 0; i < 16 && i < key_len; i++) {
            combined_hash[i] ^= salt[i];
        }
        
        // Stretch key
        for (size_t i = 0; i < key_len; i++) {
            key[i] = combined_hash[i % HASH_SIZE];
        }
    }
    
    std::string readPassword(const std::string& prompt) {
        std::cout << prompt;
        std::cout.flush();
        
        // Disable echo
        struct termios old_term, new_term;
        tcgetattr(STDIN_FILENO, &old_term);
        new_term = old_term;
        new_term.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        
        std::string password;
        std::getline(std::cin, password);
        
        // Restore echo
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        std::cout << std::endl;
        
        return password;
    }

public:
    Wallet() : loaded_(false) {}
    
    // Generate new wallet
    bool generate(const std::string& path, const std::string& password) {
        wallet_path_ = path;
        keypair_.generate();
        
        return save(password);
    }
    
    // Save wallet to file
    bool save(const std::string& password) {
        WalletFile wf;
        wf.magic = WalletFile::MAGIC;
        wf.version = WalletFile::VERSION;
        wf.encrypted = !password.empty() ? 1 : 0;
        
        // Generate salt
        for (int i = 0; i < 16; i++) {
            wf.salt[i] = static_cast<uint8_t>(rand() % 256);
        }
        
        // Generate nonce
        for (int i = 0; i < 24; i++) {
            wf.nonce[i] = static_cast<uint8_t>(rand() % 256);
        }
        
        // Copy keys
        wf.public_key = keypair_.getPublicKey();
        wf.address = keypair_.getAddress();
        
        if (wf.encrypted) {
            // Encrypt private key
            uint8_t key[PRIVATE_KEY_SIZE];
            deriveKey(password, wf.salt, key, PRIVATE_KEY_SIZE);
            
            const auto& priv = keypair_.getPrivateKey();
            for (size_t i = 0; i < PRIVATE_KEY_SIZE; i++) {
                wf.encrypted_key[i] = priv[i] ^ key[i];
            }
        } else {
            wf.encrypted_key = keypair_.getPrivateKey();
        }
        
        // Compute checksum
        wf.checksum = 0;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&wf);
        for (size_t i = 0; i < sizeof(wf) - 4; i++) {
            wf.checksum ^= data[i];
            wf.checksum = (wf.checksum << 1) | (wf.checksum >> 31);
        }
        
        // Write to file
        std::ofstream file(wallet_path_, std::ios::binary);
        if (!file) {
            std::cerr << "Error: Could not create wallet file" << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(&wf), sizeof(wf));
        file.close();
        
        loaded_ = true;
        return true;
    }
    
    // Load wallet from file
    bool load(const std::string& path, const std::string& password = "") {
        wallet_path_ = path;
        
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "Error: Could not open wallet file" << std::endl;
            return false;
        }
        
        WalletFile wf;
        file.read(reinterpret_cast<char*>(&wf), sizeof(wf));
        file.close();
        
        // Verify magic
        if (wf.magic != WalletFile::MAGIC) {
            std::cerr << "Error: Invalid wallet file format" << std::endl;
            return false;
        }
        
        // Verify checksum
        uint32_t saved_checksum = wf.checksum;
        wf.checksum = 0;
        uint32_t computed = 0;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&wf);
        for (size_t i = 0; i < sizeof(wf) - 4; i++) {
            computed ^= data[i];
            computed = (computed << 1) | (computed >> 31);
        }
        
        if (computed != saved_checksum) {
            std::cerr << "Error: Wallet file corrupted (checksum mismatch)" << std::endl;
            return false;
        }
        
        // Decrypt private key
        PrivateKey decrypted_key;
        if (wf.encrypted) {
            std::string pwd = password.empty() ? readPassword("Enter wallet password: ") : password;
            
            uint8_t key[PRIVATE_KEY_SIZE];
            deriveKey(pwd, wf.salt, key, PRIVATE_KEY_SIZE);
            
            for (size_t i = 0; i < PRIVATE_KEY_SIZE; i++) {
                decrypted_key[i] = wf.encrypted_key[i] ^ key[i];
            }
        } else {
            decrypted_key = wf.encrypted_key;
        }
        
        // Load into keypair
        keypair_.loadSecretKey(decrypted_key);
        
        // Verify address matches
        if (keypair_.getAddress() != wf.address) {
            std::cerr << "Error: Invalid password or corrupted wallet" << std::endl;
            return false;
        }
        
        loaded_ = true;
        return true;
    }
    
    // Get address
    std::string getAddress() const {
        return AddressUtil::toHex(keypair_.getAddress());
    }
    
    // Sign transaction
    Transaction sign(const Address& to, uint64_t value, uint64_t gas_price, 
                     uint64_t gas_limit, uint64_t nonce) {
        Transaction tx;
        tx.to = to;
        tx.value = value;
        tx.gas_price = gas_price;
        tx.gas_limit = gas_limit;
        tx.nonce = nonce;
        tx.sign(keypair_);
        return tx;
    }
    
    bool isLoaded() const { return loaded_; }
    const KeyPair& getKeyPair() const { return keypair_; }
};

// ============================================================================
// RPC Client
// ============================================================================

class RpcClient {
private:
    std::string host_;
    uint16_t port_;
    
public:
    RpcClient(const std::string& host = "127.0.0.1", uint16_t port = 8545)
        : host_(host), port_(port) {}
    
    // Get account balance
    uint64_t getBalance(const Address& addr) {
        TcpSocket sock;
        if (!sock.connect(host_, port_, 5000)) {
            return 0;
        }
        
        std::string req = "{\"method\":\"eth_getBalance\",\"params\":[\"" + 
                          AddressUtil::toHex(addr) + "\",\"latest\"],\"id\":1}";
        
        sock.sendAll(req.c_str(), req.length());
        
        char buf[1024] = {0};
        sock.recv(buf, sizeof(buf) - 1);
        
        // Parse response (simplified)
        std::string response(buf);
        size_t pos = response.find("\"result\":\"");
        if (pos != std::string::npos) {
            pos += 10;
            size_t end = response.find("\"", pos);
            std::string hex = response.substr(pos, end - pos);
            return std::stoull(hex, nullptr, 16);
        }
        
        return 0;
    }
    
    // Get transaction count (nonce)
    uint64_t getNonce(const Address& addr) {
        TcpSocket sock;
        if (!sock.connect(host_, port_, 5000)) {
            return 0;
        }
        
        std::string req = "{\"method\":\"eth_getTransactionCount\",\"params\":[\"" + 
                          AddressUtil::toHex(addr) + "\",\"latest\"],\"id\":1}";
        
        sock.sendAll(req.c_str(), req.length());
        
        char buf[1024] = {0};
        sock.recv(buf, sizeof(buf) - 1);
        
        std::string response(buf);
        size_t pos = response.find("\"result\":\"");
        if (pos != std::string::npos) {
            pos += 10;
            size_t end = response.find("\"", pos);
            std::string hex = response.substr(pos, end - pos);
            return std::stoull(hex, nullptr, 16);
        }
        
        return 0;
    }
    
    // Send raw transaction
    std::string sendTransaction(const Transaction& tx) {
        TcpSocket sock;
        if (!sock.connect(host_, port_, 5000)) {
            return "Error: Could not connect to node";
        }
        
        // Serialize transaction
        uint8_t tx_data[1024];
        size_t tx_len;
        tx.serialize(tx_data, tx_len);
        
        // Hex encode
        std::stringstream hex;
        hex << "0x";
        for (size_t i = 0; i < tx_len; i++) {
            hex << std::hex << std::setfill('0') << std::setw(2) << (int)tx_data[i];
        }
        
        std::string req = "{\"method\":\"eth_sendRawTransaction\",\"params\":[\"" + 
                          hex.str() + "\"],\"id\":1}";
        
        sock.sendAll(req.c_str(), req.length());
        
        char buf[1024] = {0};
        sock.recv(buf, sizeof(buf) - 1);
        
        return std::string(buf);
    }
};

// ============================================================================
// CLI Commands
// ============================================================================

void printUsage() {
    std::cout << "ARQON Wallet CLI v1.0.0\n\n";
    std::cout << "Usage: arqon-wallet <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  new <wallet-file>           Create new wallet\n";
    std::cout << "  import <wallet-file> <key>  Import private key\n";
    std::cout << "  address <wallet-file>       Show wallet address\n";
    std::cout << "  balance <wallet-file>       Check balance\n";
    std::cout << "  send <wallet> <to> <amount> Send ARQN\n";
    std::cout << "  stake <wallet> <amount>     Stake to become validator\n";
    std::cout << "  unstake <wallet> <amount>   Unstake tokens\n";
    std::cout << "  export <wallet-file>        Export private key (DANGER)\n";
    std::cout << "  sign <wallet> <message>     Sign a message\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --rpc <host:port>           RPC endpoint (default: 127.0.0.1:8545)\n";
    std::cout << "  --password <pass>           Wallet password\n";
}

int cmdNew(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: arqon-wallet new <wallet-file>" << std::endl;
        return 1;
    }
    
    std::string path = argv[2];
    
    // Check if file exists
    std::ifstream test(path);
    if (test.good()) {
        std::cerr << "Error: Wallet file already exists: " << path << std::endl;
        return 1;
    }
    test.close();
    
    // Get password
    std::cout << "Creating new wallet: " << path << std::endl;
    
    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~ECHO;
    
    std::cout << "Enter password (or empty for no encryption): ";
    std::cout.flush();
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    
    std::string password;
    std::getline(std::cin, password);
    
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    std::cout << std::endl;
    
    if (!password.empty()) {
        std::cout << "Confirm password: ";
        std::cout.flush();
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        
        std::string confirm;
        std::getline(std::cin, confirm);
        
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        std::cout << std::endl;
        
        if (password != confirm) {
            std::cerr << "Error: Passwords do not match" << std::endl;
            return 1;
        }
    }
    
    // Create wallet
    Wallet wallet;
    if (!wallet.generate(path, password)) {
        return 1;
    }
    
    std::cout << "\n✅ Wallet created successfully!\n";
    std::cout << "Address: " << wallet.getAddress() << std::endl;
    std::cout << "\n⚠️  IMPORTANT: Back up your wallet file and remember your password!\n";
    
    return 0;
}

int cmdAddress(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: arqon-wallet address <wallet-file>" << std::endl;
        return 1;
    }
    
    Wallet wallet;
    if (!wallet.load(argv[2])) {
        return 1;
    }
    
    std::cout << wallet.getAddress() << std::endl;
    return 0;
}

int cmdBalance(int argc, char** argv, const std::string& rpc_host, uint16_t rpc_port) {
    if (argc < 3) {
        std::cerr << "Usage: arqon-wallet balance <wallet-file>" << std::endl;
        return 1;
    }
    
    Wallet wallet;
    if (!wallet.load(argv[2])) {
        return 1;
    }
    
    RpcClient rpc(rpc_host, rpc_port);
    uint64_t balance = rpc.getBalance(wallet.getKeyPair().getAddress());
    
    double arqn = static_cast<double>(balance) / Q_PER_ARQON;
    
    std::cout << "Address: " << wallet.getAddress() << std::endl;
    std::cout << "Balance: " << std::fixed << std::setprecision(9) << arqn << " ARQN" << std::endl;
    std::cout << "         (" << balance << " Q)" << std::endl;
    
    return 0;
}

int cmdSend(int argc, char** argv, const std::string& rpc_host, uint16_t rpc_port) {
    if (argc < 5) {
        std::cerr << "Usage: arqon-wallet send <wallet-file> <to-address> <amount>" << std::endl;
        return 1;
    }
    
    Wallet wallet;
    if (!wallet.load(argv[2])) {
        return 1;
    }
    
    // Parse destination
    Address to;
    if (!AddressUtil::fromHex(argv[3], to)) {
        std::cerr << "Error: Invalid destination address" << std::endl;
        return 1;
    }
    
    // Parse amount
    double amount = std::stod(argv[4]);
    uint64_t value = static_cast<uint64_t>(amount * Q_PER_ARQON);
    
    RpcClient rpc(rpc_host, rpc_port);
    
    // Get nonce
    uint64_t nonce = rpc.getNonce(wallet.getKeyPair().getAddress());
    
    // Sign transaction
    Transaction tx = wallet.sign(to, value, 10, 21000, nonce);
    
    std::cout << "Sending " << amount << " ARQN to " << argv[3] << std::endl;
    std::cout << "Nonce: " << nonce << std::endl;
    
    // Send
    std::string result = rpc.sendTransaction(tx);
    std::cout << "Result: " << result << std::endl;
    
    return 0;
}

int cmdExport(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: arqon-wallet export <wallet-file>" << std::endl;
        return 1;
    }
    
    std::cout << "⚠️  WARNING: This will display your private key!\n";
    std::cout << "Anyone with this key can steal all your funds.\n";
    std::cout << "Type 'I understand the risks' to continue: ";
    
    std::string confirm;
    std::getline(std::cin, confirm);
    
    if (confirm != "I understand the risks") {
        std::cout << "Export cancelled." << std::endl;
        return 1;
    }
    
    Wallet wallet;
    if (!wallet.load(argv[2])) {
        return 1;
    }
    
    const auto& priv = wallet.getKeyPair().getPrivateKey();
    
    std::cout << "\nPrivate Key: 0x";
    for (size_t i = 0; i < PRIVATE_KEY_SIZE; i++) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)priv[i];
    }
    std::cout << std::dec << std::endl;
    
    return 0;
}

int cmdSign(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: arqon-wallet sign <wallet-file> <message>" << std::endl;
        return 1;
    }
    
    Wallet wallet;
    if (!wallet.load(argv[2])) {
        return 1;
    }
    
    std::string message = argv[3];
    
    Signature sig = wallet.getKeyPair().sign(
        reinterpret_cast<const uint8_t*>(message.data()), message.size());
    
    std::cout << "Message: " << message << std::endl;
    std::cout << "Signature: 0x";
    for (size_t i = 0; i < SIGNATURE_SIZE; i++) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)sig[i];
    }
    std::cout << std::dec << std::endl;
    
    return 0;
}

int main(int argc, char** argv) {
    init_crypto();
    srand(time(nullptr));
    
    if (argc < 2) {
        printUsage();
        return 1;
    }
    
    std::string cmd = argv[1];
    std::string rpc_host = "127.0.0.1";
    uint16_t rpc_port = 8545;
    
    // Parse options
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--rpc" && i + 1 < argc) {
            std::string rpc = argv[++i];
            size_t colon = rpc.find(':');
            if (colon != std::string::npos) {
                rpc_host = rpc.substr(0, colon);
                rpc_port = std::stoi(rpc.substr(colon + 1));
            }
        }
    }
    
    if (cmd == "new") return cmdNew(argc, argv);
    if (cmd == "address") return cmdAddress(argc, argv);
    if (cmd == "balance") return cmdBalance(argc, argv, rpc_host, rpc_port);
    if (cmd == "send") return cmdSend(argc, argv, rpc_host, rpc_port);
    if (cmd == "export") return cmdExport(argc, argv);
    if (cmd == "sign") return cmdSign(argc, argv);
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }
    
    std::cerr << "Unknown command: " << cmd << std::endl;
    printUsage();
    return 1;
}
