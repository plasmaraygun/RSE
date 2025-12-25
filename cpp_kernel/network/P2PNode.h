#pragma once

#include "../core/Crypto.h"
#include "TcpSocket.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>

/**
 * P2P Network Layer for RSE
 * 
 * Real TCP socket implementation for distributed torus operation:
 * - Peer discovery
 * - Message gossip protocol
 * - Connection management
 * - Real TCP connections (no simulation)
 */

namespace network {

using namespace crypto;

// ============================================================================
// Network Constants
// ============================================================================

constexpr uint16_t DEFAULT_PORT = 8333;
constexpr size_t MAX_PEERS = 125;
constexpr size_t MIN_PEERS = 8;
constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1MB

constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint32_t MAGIC_BYTES = 0x52534500;  // "RSE\0"

// ============================================================================
// Message Types
// ============================================================================

enum class MessageType : uint8_t {
    // Handshake
    VERSION = 0x01,
    VERACK = 0x02,
    
    // Peer discovery
    GETADDR = 0x10,
    ADDR = 0x11,
    
    // Blockchain
    INV = 0x20,           // Inventory (announce new data)
    GETDATA = 0x21,       // Request data
    TX = 0x22,            // Transaction
    BLOCK = 0x23,         // Block (braid interval)
    
    // Projection sync
    GETPROJECTION = 0x30,
    PROJECTION = 0x31,
    
    // Heartbeat
    PING = 0x40,
    PONG = 0x41,
};

// ============================================================================
// Network Address
// ============================================================================

struct NetAddr {
    uint32_t ip;        // IPv4 address (network byte order)
    uint16_t port;      // Port number
    uint64_t services;  // Service flags
    uint64_t timestamp; // Last seen
    
    NetAddr() : ip(0), port(0), services(0), timestamp(0) {}
    
    NetAddr(uint32_t ip_, uint16_t port_) 
        : ip(ip_), port(port_), services(1), 
          timestamp(std::chrono::system_clock::now().time_since_epoch().count()) {}
    
    std::string toString() const {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d:%d",
                 (ip >> 24) & 0xFF,
                 (ip >> 16) & 0xFF,
                 (ip >> 8) & 0xFF,
                 ip & 0xFF,
                 port);
        return std::string(buf);
    }
    
    bool operator==(const NetAddr& other) const {
        return ip == other.ip && port == other.port;
    }
};

// Hash function for NetAddr (for unordered_set)
struct NetAddrHash {
    size_t operator()(const NetAddr& addr) const {
        return std::hash<uint64_t>()((static_cast<uint64_t>(addr.ip) << 16) | addr.port);
    }
};

// ============================================================================
// Message Header
// ============================================================================

struct MessageHeader {
    uint32_t magic;         // Magic bytes (MAGIC_BYTES)
    MessageType type;       // Message type
    uint32_t length;        // Payload length
    uint32_t checksum;      // Payload checksum
    
    MessageHeader() : magic(MAGIC_BYTES), type(MessageType::PING), length(0), checksum(0) {}
    
    static constexpr size_t SIZE = 13;
    
    void serialize(uint8_t* buffer) const {
        size_t pos = 0;
        std::memcpy(buffer + pos, &magic, 4); pos += 4;
        std::memcpy(buffer + pos, &type, 1); pos += 1;
        std::memcpy(buffer + pos, &length, 4); pos += 4;
        std::memcpy(buffer + pos, &checksum, 4); pos += 4;
    }
    
    bool deserialize(const uint8_t* buffer) {
        size_t pos = 0;
        std::memcpy(&magic, buffer + pos, 4); pos += 4;
        std::memcpy(&type, buffer + pos, 1); pos += 1;
        std::memcpy(&length, buffer + pos, 4); pos += 4;
        std::memcpy(&checksum, buffer + pos, 4); pos += 4;
        
        return magic == MAGIC_BYTES;
    }
};

// ============================================================================
// Message
// ============================================================================

struct Message {
    MessageHeader header;
    std::vector<uint8_t> payload;
    
    Message() = default;
    
    Message(MessageType type, const std::vector<uint8_t>& data) {
        header.type = type;
        header.length = static_cast<uint32_t>(data.size());
        payload = data;
        
        // Simple checksum (XOR of all bytes)
        header.checksum = 0;
        for (uint8_t b : data) {
            header.checksum ^= b;
        }
    }
    
    bool verify() const {
        uint32_t check = 0;
        for (uint8_t b : payload) {
            check ^= b;
        }
        return check == header.checksum;
    }
};

// ============================================================================
// Peer Connection
// ============================================================================

struct Peer {
    NetAddr addr;
    uint32_t version;
    bool connected;
    uint64_t last_seen;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    
    // Torus ID (which torus this peer operates)
    uint32_t torus_id;
    
    // Real TCP socket connection
    std::shared_ptr<TcpSocket> socket;
    
    Peer() : version(0), connected(false), last_seen(0), 
             bytes_sent(0), bytes_received(0), torus_id(0), socket(nullptr) {}
    
    Peer(const NetAddr& addr_) : addr(addr_), version(0), connected(false),
                                  last_seen(0), bytes_sent(0), bytes_received(0), 
                                  torus_id(0), socket(nullptr) {}
};

// ============================================================================
// P2P Node
// ============================================================================

class P2PNode {
private:
    // Node identity
    Address node_address_;
    uint32_t torus_id_;
    
    // Network state
    NetAddr listen_addr_;
    std::atomic<bool> running_;
    
    // Real TCP listener socket
    TcpSocket listener_socket_;
    
    // Peer management
    std::unordered_map<std::string, Peer> peers_;
    std::unordered_set<NetAddr, NetAddrHash> known_addrs_;
    mutable std::mutex peers_mutex_;
    
    // Message queues
    std::queue<Message> outbound_queue_;
    std::queue<std::pair<NetAddr, Message>> inbound_queue_;
    std::mutex queue_mutex_;
    
    // Worker threads with real socket I/O
    std::thread listener_thread_;
    std::thread gossip_thread_;
    std::thread receiver_thread_;
    
    // Statistics
    std::atomic<uint64_t> messages_sent_;
    std::atomic<uint64_t> messages_received_;
    std::atomic<uint64_t> bytes_sent_;
    std::atomic<uint64_t> bytes_received_;
    
    std::string peerKey(const NetAddr& addr) const {
        return addr.toString();
    }
    
    // Real TCP listener - accepts incoming connections
    void listenerLoop() {
        std::cout << "[P2P] Listener thread started on port " << listen_addr_.port << std::endl;
        
        while (running_) {
            uint32_t client_ip;
            uint16_t client_port;
            
            TcpSocket client = listener_socket_.accept(client_ip, client_port);
            
            if (client.isValid()) {
                NetAddr addr(client_ip, client_port);
                std::cout << "[P2P] Accepted connection from " << IpAddress::toString(client_ip) 
                          << ":" << client_port << std::endl;
                
                // Add peer with real socket
                std::lock_guard<std::mutex> lock(peers_mutex_);
                std::string key = peerKey(addr);
                
                if (peers_.size() < MAX_PEERS) {
                    Peer peer(addr);
                    peer.socket = std::make_shared<TcpSocket>(std::move(client));
                    peer.connected = true;
                    peer.last_seen = std::chrono::system_clock::now().time_since_epoch().count();
                    peers_[key] = peer;
                    known_addrs_.insert(addr);
                }
            } else if (listener_socket_.getLastError() != SocketError::WOULD_BLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "[P2P] Listener thread stopped" << std::endl;
    }
    
    // Real TCP sender - sends queued messages to peers
    void gossipLoop() {
        std::cout << "[P2P] Gossip thread started" << std::endl;
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Get messages to send
            std::vector<Message> to_send;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                while (!outbound_queue_.empty()) {
                    to_send.push_back(outbound_queue_.front());
                    outbound_queue_.pop();
                }
            }
            
            if (to_send.empty()) continue;
            
            // Send to all connected peers via real TCP
            std::lock_guard<std::mutex> lock(peers_mutex_);
            
            for (auto& [key, peer] : peers_) {
                if (!peer.connected || !peer.socket || !peer.socket->isConnected()) {
                    continue;
                }
                
                for (const auto& msg : to_send) {
                    // Serialize header
                    uint8_t header_buf[MessageHeader::SIZE];
                    msg.header.serialize(header_buf);
                    
                    // Send header
                    if (!peer.socket->sendAll(header_buf, MessageHeader::SIZE)) {
                        std::cerr << "[P2P] Failed to send header to " << key << std::endl;
                        peer.connected = false;
                        continue;
                    }
                    
                    // Send payload
                    if (msg.payload.size() > 0) {
                        if (!peer.socket->sendAll(msg.payload.data(), msg.payload.size())) {
                            std::cerr << "[P2P] Failed to send payload to " << key << std::endl;
                            peer.connected = false;
                            continue;
                        }
                    }
                    
                    peer.bytes_sent += MessageHeader::SIZE + msg.payload.size();
                    bytes_sent_ += MessageHeader::SIZE + msg.payload.size();
                    messages_sent_++;
                }
            }
        }
        
        std::cout << "[P2P] Gossip thread stopped" << std::endl;
    }
    
    // Real TCP receiver - reads messages from all peers
    void receiverLoop() {
        std::cout << "[P2P] Receiver thread started" << std::endl;
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            std::lock_guard<std::mutex> lock(peers_mutex_);
            
            for (auto& [key, peer] : peers_) {
                if (!peer.connected || !peer.socket) continue;
                
                // Check if data available
                if (!peer.socket->hasData(0)) continue;
                
                // Read header
                uint8_t header_buf[MessageHeader::SIZE];
                if (!peer.socket->recvAll(header_buf, MessageHeader::SIZE, 1000)) {
                    if (peer.socket->getLastError() == SocketError::CLOSED) {
                        std::cout << "[P2P] Peer " << key << " disconnected" << std::endl;
                        peer.connected = false;
                    }
                    continue;
                }
                
                Message msg;
                if (!msg.header.deserialize(header_buf)) {
                    std::cerr << "[P2P] Invalid message header from " << key << std::endl;
                    continue;
                }
                
                // Read payload
                if (msg.header.length > 0 && msg.header.length <= MAX_MESSAGE_SIZE) {
                    msg.payload.resize(msg.header.length);
                    if (!peer.socket->recvAll(msg.payload.data(), msg.header.length, 5000)) {
                        std::cerr << "[P2P] Failed to read payload from " << key << std::endl;
                        continue;
                    }
                }
                
                // Verify checksum
                if (!msg.verify()) {
                    std::cerr << "[P2P] Checksum mismatch from " << key << std::endl;
                    continue;
                }
                
                peer.bytes_received += MessageHeader::SIZE + msg.payload.size();
                bytes_received_ += MessageHeader::SIZE + msg.payload.size();
                peer.last_seen = std::chrono::system_clock::now().time_since_epoch().count();
                
                // Queue for processing
                {
                    std::lock_guard<std::mutex> qlock(queue_mutex_);
                    inbound_queue_.push({peer.addr, msg});
                    messages_received_++;
                }
            }
        }
        
        std::cout << "[P2P] Receiver thread stopped" << std::endl;
    }
    
public:
    P2PNode(const Address& addr, uint32_t torus_id, uint16_t port = DEFAULT_PORT)
        : node_address_(addr), torus_id_(torus_id), running_(false),
          messages_sent_(0), messages_received_(0), bytes_sent_(0), bytes_received_(0) {
        listen_addr_.ip = 0;  // 0.0.0.0 (all interfaces)
        listen_addr_.port = port;
    }
    
    ~P2PNode() {
        stop();
    }
    
    // Start P2P node with real TCP listener
    void start() {
        if (running_) return;
        
        // Create and bind real TCP listener socket
        if (!listener_socket_.create()) {
            std::cerr << "[P2P] Failed to create listener socket" << std::endl;
            return;
        }
        
        if (!listener_socket_.bind(listen_addr_.port)) {
            std::cerr << "[P2P] Failed to bind to port " << listen_addr_.port << std::endl;
            return;
        }
        
        if (!listener_socket_.listen(128)) {
            std::cerr << "[P2P] Failed to listen on port " << listen_addr_.port << std::endl;
            return;
        }
        
        running_ = true;
        
        // Start worker threads with real socket I/O
        listener_thread_ = std::thread(&P2PNode::listenerLoop, this);
        gossip_thread_ = std::thread(&P2PNode::gossipLoop, this);
        receiver_thread_ = std::thread(&P2PNode::receiverLoop, this);
        
        std::cout << "[P2P] Node started on port " << listen_addr_.port 
                  << " (Torus " << torus_id_ << ") - REAL TCP" << std::endl;
    }
    
    // Stop P2P node
    void stop() {
        if (!running_) return;
        
        running_ = false;
        
        // Close listener socket to unblock accept()
        listener_socket_.close();
        
        // Close all peer sockets
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            for (auto& [key, peer] : peers_) {
                if (peer.socket) {
                    peer.socket->close();
                }
            }
            peers_.clear();
        }
        
        if (listener_thread_.joinable()) listener_thread_.join();
        if (gossip_thread_.joinable()) gossip_thread_.join();
        if (receiver_thread_.joinable()) receiver_thread_.join();
        
        std::cout << "[P2P] Node stopped (sent " << bytes_sent_ << " bytes, received " 
                  << bytes_received_ << " bytes)" << std::endl;
    }
    
    // Connect to peer via real TCP
    bool connectPeer(const NetAddr& addr) {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        std::string key = peerKey(addr);
        
        if (peers_.find(key) != peers_.end() && peers_[key].connected) {
            return true;  // Already connected
        }
        
        if (peers_.size() >= MAX_PEERS) {
            return false;  // Too many peers
        }
        
        // Create real TCP connection
        auto socket = std::make_shared<TcpSocket>();
        if (!socket->connect(addr.ip, addr.port, 5000)) {
            std::cerr << "[P2P] Failed to connect to " << IpAddress::toString(addr.ip) 
                      << ":" << addr.port << " - " << socketErrorString(socket->getLastError()) << std::endl;
            return false;
        }
        
        Peer peer(addr);
        peer.socket = socket;
        peer.connected = true;
        peer.last_seen = std::chrono::system_clock::now().time_since_epoch().count();
        
        peers_[key] = peer;
        known_addrs_.insert(addr);
        
        std::cout << "[P2P] Connected to peer " << IpAddress::toString(addr.ip) 
                  << ":" << addr.port << " via TCP" << std::endl;
        
        // Send VERSION message
        sendVersion(addr);
        
        return true;
    }
    
    // Disconnect peer and close socket
    void disconnectPeer(const NetAddr& addr) {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        std::string key = peerKey(addr);
        auto it = peers_.find(key);
        
        if (it != peers_.end()) {
            if (it->second.socket) {
                it->second.socket->close();
            }
            std::cout << "[P2P] Disconnected from peer " << addr.toString() << std::endl;
            peers_.erase(it);
        }
    }
    
    // Broadcast message to all peers
    void broadcast(const Message& msg) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        outbound_queue_.push(msg);
    }
    
    // Send message to specific peer via real TCP
    void send(const NetAddr& addr, const Message& msg) {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        std::string key = peerKey(addr);
        auto it = peers_.find(key);
        
        if (it == peers_.end() || !it->second.socket || !it->second.connected) {
            return;  // Peer not connected
        }
        
        // Serialize and send directly
        uint8_t header_buf[MessageHeader::SIZE];
        msg.header.serialize(header_buf);
        
        if (!it->second.socket->sendAll(header_buf, MessageHeader::SIZE)) {
            it->second.connected = false;
            return;
        }
        
        if (msg.payload.size() > 0) {
            if (!it->second.socket->sendAll(msg.payload.data(), msg.payload.size())) {
                it->second.connected = false;
                return;
            }
        }
        
        it->second.bytes_sent += MessageHeader::SIZE + msg.payload.size();
        bytes_sent_ += MessageHeader::SIZE + msg.payload.size();
        messages_sent_++;
    }
    
    // Receive messages (non-blocking)
    bool receive(NetAddr& from, Message& msg) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        if (inbound_queue_.empty()) {
            return false;
        }
        
        auto [addr, message] = inbound_queue_.front();
        inbound_queue_.pop();
        
        from = addr;
        msg = message;
        
        messages_received_++;
        
        return true;
    }
    
    // Get peer count
    size_t getPeerCount() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        return peers_.size();
    }
    
    // Get connected peers
    std::vector<NetAddr> getPeers() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        std::vector<NetAddr> result;
        for (const auto& [key, peer] : peers_) {
            if (peer.connected) {
                result.push_back(peer.addr);
            }
        }
        
        return result;
    }
    
    // Protocol messages
    void sendVersion(const NetAddr& addr) {
        std::vector<uint8_t> payload;
        payload.resize(8);
        
        uint32_t version = PROTOCOL_VERSION;
        std::memcpy(payload.data(), &version, 4);
        std::memcpy(payload.data() + 4, &torus_id_, 4);
        
        Message msg(MessageType::VERSION, payload);
        send(addr, msg);
    }
    
    void sendPing(const NetAddr& addr) {
        std::vector<uint8_t> payload(8);
        uint64_t nonce = std::chrono::system_clock::now().time_since_epoch().count();
        std::memcpy(payload.data(), &nonce, 8);
        
        Message msg(MessageType::PING, payload);
        send(addr, msg);
    }
    
    void sendGetAddr() {
        Message msg(MessageType::GETADDR, {});
        broadcast(msg);
    }
    
    void sendAddr(const std::vector<NetAddr>& addrs) {
        std::vector<uint8_t> payload;
        payload.resize(addrs.size() * 14);  // 4 (ip) + 2 (port) + 8 (timestamp)
        
        size_t pos = 0;
        for (const auto& addr : addrs) {
            std::memcpy(payload.data() + pos, &addr.ip, 4); pos += 4;
            std::memcpy(payload.data() + pos, &addr.port, 2); pos += 2;
            std::memcpy(payload.data() + pos, &addr.timestamp, 8); pos += 8;
        }
        
        Message msg(MessageType::ADDR, payload);
        broadcast(msg);
    }
    
    // Statistics
    uint64_t getMessagesSent() const { return messages_sent_; }
    uint64_t getMessagesReceived() const { return messages_received_; }
    
    const Address& getAddress() const { return node_address_; }
    uint32_t getTorusId() const { return torus_id_; }
};

// ============================================================================
// Peer Discovery (Simplified DHT)
// ============================================================================

class PeerDiscovery {
private:
    P2PNode& node_;
    
    // Bootstrap nodes (hardcoded seed nodes)
    std::vector<NetAddr> bootstrap_nodes_;
    
    // Known peers
    std::unordered_set<NetAddr, NetAddrHash> discovered_peers_;
    mutable std::mutex discovery_mutex_;
    
public:
    PeerDiscovery(P2PNode& node) : node_(node) {
        // Add bootstrap nodes (in production: DNS seeds or hardcoded IPs)
        // For now: empty (local testing)
    }
    
    void addBootstrapNode(const NetAddr& addr) {
        bootstrap_nodes_.push_back(addr);
    }
    
    // Start discovery process
    void discover() {
        // Connect to bootstrap nodes
        for (const auto& addr : bootstrap_nodes_) {
            node_.connectPeer(addr);
        }
        
        // Request peer lists
        node_.sendGetAddr();
    }
    
    // Process ADDR message
    void processAddr(const std::vector<NetAddr>& addrs) {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        
        for (const auto& addr : addrs) {
            if (discovered_peers_.find(addr) == discovered_peers_.end()) {
                discovered_peers_.insert(addr);
                
                // Try to connect if we need more peers
                if (node_.getPeerCount() < MIN_PEERS) {
                    node_.connectPeer(addr);
                }
            }
        }
    }
    
    size_t getDiscoveredCount() const {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        return discovered_peers_.size();
    }
};

} // namespace network
