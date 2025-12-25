#pragma once
/**
 * Bootstrap Node Infrastructure
 * 
 * Provides peer discovery and initial network connection for new nodes.
 * Bootstrap nodes maintain a list of active peers and help new nodes
 * find their way into the network.
 */

#include "TcpSocket.h"
#include "P2PNode.h"
#include "../core/Crypto.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>

namespace network {

using namespace crypto;

// ============================================================================
// Bootstrap Protocol
// ============================================================================

constexpr uint16_t BOOTSTRAP_PORT = 8334;
constexpr uint32_t BOOTSTRAP_MAGIC = 0x41425354;  // "ABST"
constexpr size_t MAX_BOOTSTRAP_PEERS = 1000;
constexpr size_t PEERS_PER_RESPONSE = 25;

enum class BootstrapMsgType : uint8_t {
    GET_PEERS = 0x01,
    PEERS = 0x02,
    ANNOUNCE = 0x03,
    ANNOUNCE_ACK = 0x04,
    PING = 0x05,
    PONG = 0x06
};

struct BootstrapHeader {
    uint32_t magic;
    BootstrapMsgType type;
    uint16_t payload_len;
    uint8_t reserved;
    
    static constexpr size_t SIZE = 8;
    
    void serialize(uint8_t* buf) const {
        std::memcpy(buf, &magic, 4);
        buf[4] = static_cast<uint8_t>(type);
        std::memcpy(buf + 5, &payload_len, 2);
        buf[7] = reserved;
    }
    
    bool deserialize(const uint8_t* buf) {
        std::memcpy(&magic, buf, 4);
        type = static_cast<BootstrapMsgType>(buf[4]);
        std::memcpy(&payload_len, buf + 5, 2);
        reserved = buf[7];
        return magic == BOOTSTRAP_MAGIC;
    }
};

struct PeerEntry {
    uint32_t ip;
    uint16_t port;
    uint64_t last_seen;
    uint32_t torus_id;
    uint8_t services;
    
    static constexpr size_t SIZE = 4 + 2 + 8 + 4 + 1;
    
    void serialize(uint8_t* buf) const {
        size_t pos = 0;
        std::memcpy(buf + pos, &ip, 4); pos += 4;
        std::memcpy(buf + pos, &port, 2); pos += 2;
        std::memcpy(buf + pos, &last_seen, 8); pos += 8;
        std::memcpy(buf + pos, &torus_id, 4); pos += 4;
        buf[pos] = services;
    }
    
    void deserialize(const uint8_t* buf) {
        size_t pos = 0;
        std::memcpy(&ip, buf + pos, 4); pos += 4;
        std::memcpy(&port, buf + pos, 2); pos += 2;
        std::memcpy(&last_seen, buf + pos, 8); pos += 8;
        std::memcpy(&torus_id, buf + pos, 4); pos += 4;
        services = buf[pos];
    }
    
    std::string key() const {
        return std::to_string(ip) + ":" + std::to_string(port);
    }
};

// ============================================================================
// Bootstrap Server
// ============================================================================

class BootstrapServer {
private:
    TcpSocket listener_;
    std::unordered_map<std::string, PeerEntry> peers_;
    mutable std::mutex peers_mutex_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::thread cleanup_thread_;
    
    uint16_t port_;
    std::string peers_file_;
    
    // Statistics
    std::atomic<uint64_t> total_requests_;
    std::atomic<uint64_t> total_announces_;
    std::atomic<uint64_t> total_peers_served_;
    
    void acceptLoop() {
        std::cout << "[Bootstrap] Accept thread started on port " << port_ << std::endl;
        
        while (running_) {
            uint32_t client_ip;
            uint16_t client_port;
            
            TcpSocket client = listener_.accept(client_ip, client_port);
            
            if (client.isValid()) {
                std::thread(&BootstrapServer::handleClient, this, 
                           std::move(client), client_ip, client_port).detach();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "[Bootstrap] Accept thread stopped" << std::endl;
    }
    
    void handleClient(TcpSocket client, uint32_t client_ip, uint16_t client_port) {
        uint8_t header_buf[BootstrapHeader::SIZE];
        
        if (!client.recvAll(header_buf, BootstrapHeader::SIZE, 5000)) {
            return;
        }
        
        BootstrapHeader header;
        if (!header.deserialize(header_buf)) {
            return;
        }
        
        // Read payload if any
        std::vector<uint8_t> payload;
        if (header.payload_len > 0 && header.payload_len < 4096) {
            payload.resize(header.payload_len);
            if (!client.recvAll(payload.data(), header.payload_len, 5000)) {
                return;
            }
        }
        
        switch (header.type) {
            case BootstrapMsgType::GET_PEERS:
                handleGetPeers(client);
                total_requests_++;
                break;
                
            case BootstrapMsgType::ANNOUNCE:
                handleAnnounce(client, client_ip, payload);
                total_announces_++;
                break;
                
            case BootstrapMsgType::PING:
                handlePing(client);
                break;
                
            default:
                break;
        }
    }
    
    void handleGetPeers(TcpSocket& client) {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        // Select random subset of peers
        std::vector<PeerEntry> selected;
        selected.reserve(PEERS_PER_RESPONSE);
        
        uint64_t now = std::chrono::system_clock::now().time_since_epoch().count();
        uint64_t max_age = 3600ULL * 1000000000ULL;  // 1 hour in nanoseconds
        
        for (const auto& [key, peer] : peers_) {
            if (now - peer.last_seen < max_age) {
                selected.push_back(peer);
                if (selected.size() >= PEERS_PER_RESPONSE) break;
            }
        }
        
        // Build response
        std::vector<uint8_t> response;
        response.reserve(BootstrapHeader::SIZE + selected.size() * PeerEntry::SIZE + 4);
        
        BootstrapHeader resp_header;
        resp_header.magic = BOOTSTRAP_MAGIC;
        resp_header.type = BootstrapMsgType::PEERS;
        resp_header.payload_len = static_cast<uint16_t>(4 + selected.size() * PeerEntry::SIZE);
        resp_header.reserved = 0;
        
        response.resize(BootstrapHeader::SIZE);
        resp_header.serialize(response.data());
        
        // Write peer count
        uint32_t count = static_cast<uint32_t>(selected.size());
        response.insert(response.end(), 
                        reinterpret_cast<uint8_t*>(&count),
                        reinterpret_cast<uint8_t*>(&count) + 4);
        
        // Write peers
        for (const auto& peer : selected) {
            uint8_t peer_buf[PeerEntry::SIZE];
            peer.serialize(peer_buf);
            response.insert(response.end(), peer_buf, peer_buf + PeerEntry::SIZE);
        }
        
        client.sendAll(response.data(), response.size());
        total_peers_served_ += selected.size();
    }
    
    void handleAnnounce(TcpSocket& client, uint32_t client_ip, 
                        const std::vector<uint8_t>& payload) {
        if (payload.size() < 6) return;
        
        PeerEntry peer;
        peer.ip = client_ip;
        std::memcpy(&peer.port, payload.data(), 2);
        std::memcpy(&peer.torus_id, payload.data() + 2, 4);
        peer.services = payload.size() > 6 ? payload[6] : 1;
        peer.last_seen = std::chrono::system_clock::now().time_since_epoch().count();
        
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            
            if (peers_.size() < MAX_BOOTSTRAP_PEERS) {
                peers_[peer.key()] = peer;
            }
        }
        
        // Send ACK
        BootstrapHeader ack;
        ack.magic = BOOTSTRAP_MAGIC;
        ack.type = BootstrapMsgType::ANNOUNCE_ACK;
        ack.payload_len = 0;
        ack.reserved = 0;
        
        uint8_t ack_buf[BootstrapHeader::SIZE];
        ack.serialize(ack_buf);
        client.sendAll(ack_buf, BootstrapHeader::SIZE);
        
        std::cout << "[Bootstrap] Peer announced: " << IpAddress::toString(client_ip) 
                  << ":" << peer.port << " (torus " << peer.torus_id << ")" << std::endl;
    }
    
    void handlePing(TcpSocket& client) {
        BootstrapHeader pong;
        pong.magic = BOOTSTRAP_MAGIC;
        pong.type = BootstrapMsgType::PONG;
        pong.payload_len = 0;
        pong.reserved = 0;
        
        uint8_t buf[BootstrapHeader::SIZE];
        pong.serialize(buf);
        client.sendAll(buf, BootstrapHeader::SIZE);
    }
    
    void cleanupLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::minutes(5));
            
            uint64_t now = std::chrono::system_clock::now().time_since_epoch().count();
            uint64_t max_age = 3600ULL * 1000000000ULL;  // 1 hour
            
            std::lock_guard<std::mutex> lock(peers_mutex_);
            
            size_t removed = 0;
            for (auto it = peers_.begin(); it != peers_.end(); ) {
                if (now - it->second.last_seen > max_age) {
                    it = peers_.erase(it);
                    removed++;
                } else {
                    ++it;
                }
            }
            
            if (removed > 0) {
                std::cout << "[Bootstrap] Removed " << removed << " stale peers" << std::endl;
            }
            
            // Save peers to file
            savePeers();
        }
    }
    
    void savePeers() {
        if (peers_file_.empty()) return;
        
        std::ofstream file(peers_file_);
        if (!file) return;
        
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        for (const auto& [key, peer] : peers_) {
            file << IpAddress::toString(peer.ip) << ":"
                 << peer.port << ":"
                 << peer.torus_id << ":"
                 << peer.last_seen << "\n";
        }
    }
    
    void loadPeers() {
        if (peers_file_.empty()) return;
        
        std::ifstream file(peers_file_);
        if (!file) return;
        
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string ip_str, port_str, torus_str, time_str;
            
            std::getline(iss, ip_str, ':');
            std::getline(iss, port_str, ':');
            std::getline(iss, torus_str, ':');
            std::getline(iss, time_str, ':');
            
            PeerEntry peer;
            peer.ip = IpAddress::fromString(ip_str);
            peer.port = static_cast<uint16_t>(std::stoi(port_str));
            peer.torus_id = static_cast<uint32_t>(std::stoul(torus_str));
            peer.last_seen = std::stoull(time_str);
            peer.services = 1;
            
            peers_[peer.key()] = peer;
        }
        
        std::cout << "[Bootstrap] Loaded " << peers_.size() << " peers from file" << std::endl;
    }

public:
    BootstrapServer(uint16_t port = BOOTSTRAP_PORT, const std::string& peers_file = "")
        : running_(false), port_(port), peers_file_(peers_file),
          total_requests_(0), total_announces_(0), total_peers_served_(0) {}
    
    ~BootstrapServer() {
        stop();
    }
    
    bool start() {
        if (running_) return true;
        
        if (!listener_.create()) {
            std::cerr << "[Bootstrap] Failed to create socket" << std::endl;
            return false;
        }
        
        if (!listener_.bind(port_)) {
            std::cerr << "[Bootstrap] Failed to bind to port " << port_ << std::endl;
            return false;
        }
        
        if (!listener_.listen(128)) {
            std::cerr << "[Bootstrap] Failed to listen" << std::endl;
            return false;
        }
        
        loadPeers();
        
        running_ = true;
        accept_thread_ = std::thread(&BootstrapServer::acceptLoop, this);
        cleanup_thread_ = std::thread(&BootstrapServer::cleanupLoop, this);
        
        std::cout << "[Bootstrap] Server started on port " << port_ << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_) return;
        
        running_ = false;
        listener_.close();
        
        if (accept_thread_.joinable()) accept_thread_.join();
        if (cleanup_thread_.joinable()) cleanup_thread_.join();
        
        savePeers();
        
        std::cout << "[Bootstrap] Server stopped. Stats: "
                  << total_requests_ << " requests, "
                  << total_announces_ << " announces, "
                  << total_peers_served_ << " peers served" << std::endl;
    }
    
    // Add seed peer manually
    void addSeedPeer(uint32_t ip, uint16_t port, uint32_t torus_id = 0) {
        PeerEntry peer;
        peer.ip = ip;
        peer.port = port;
        peer.torus_id = torus_id;
        peer.services = 1;
        peer.last_seen = std::chrono::system_clock::now().time_since_epoch().count();
        
        std::lock_guard<std::mutex> lock(peers_mutex_);
        peers_[peer.key()] = peer;
    }
    
    size_t getPeerCount() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        return peers_.size();
    }
};

// ============================================================================
// Bootstrap Client
// ============================================================================

class BootstrapClient {
private:
    std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes_;
    
public:
    BootstrapClient() {
        // Default bootstrap nodes (would be real IPs in production)
        addBootstrapNode("boot1.arqon.network", BOOTSTRAP_PORT);
        addBootstrapNode("boot2.arqon.network", BOOTSTRAP_PORT);
        addBootstrapNode("boot3.arqon.network", BOOTSTRAP_PORT);
    }
    
    void addBootstrapNode(const std::string& host, uint16_t port) {
        bootstrap_nodes_.push_back({host, port});
    }
    
    void clearBootstrapNodes() {
        bootstrap_nodes_.clear();
    }
    
    // Get peers from bootstrap nodes
    std::vector<PeerEntry> getPeers() {
        std::vector<PeerEntry> all_peers;
        
        for (const auto& [host, port] : bootstrap_nodes_) {
            auto peers = getPeersFrom(host, port);
            all_peers.insert(all_peers.end(), peers.begin(), peers.end());
        }
        
        return all_peers;
    }
    
    // Get peers from specific bootstrap node
    std::vector<PeerEntry> getPeersFrom(const std::string& host, uint16_t port) {
        std::vector<PeerEntry> peers;
        
        TcpSocket sock;
        if (!sock.connect(host, port, 5000)) {
            std::cerr << "[Bootstrap] Failed to connect to " << host << ":" << port << std::endl;
            return peers;
        }
        
        // Send GET_PEERS request
        BootstrapHeader req;
        req.magic = BOOTSTRAP_MAGIC;
        req.type = BootstrapMsgType::GET_PEERS;
        req.payload_len = 0;
        req.reserved = 0;
        
        uint8_t req_buf[BootstrapHeader::SIZE];
        req.serialize(req_buf);
        
        if (!sock.sendAll(req_buf, BootstrapHeader::SIZE)) {
            return peers;
        }
        
        // Receive response header
        uint8_t resp_buf[BootstrapHeader::SIZE];
        if (!sock.recvAll(resp_buf, BootstrapHeader::SIZE, 10000)) {
            return peers;
        }
        
        BootstrapHeader resp;
        if (!resp.deserialize(resp_buf) || resp.type != BootstrapMsgType::PEERS) {
            return peers;
        }
        
        // Receive payload
        std::vector<uint8_t> payload(resp.payload_len);
        if (!sock.recvAll(payload.data(), resp.payload_len, 10000)) {
            return peers;
        }
        
        // Parse peers
        uint32_t count;
        std::memcpy(&count, payload.data(), 4);
        
        size_t pos = 4;
        for (uint32_t i = 0; i < count && pos + PeerEntry::SIZE <= payload.size(); i++) {
            PeerEntry peer;
            peer.deserialize(payload.data() + pos);
            peers.push_back(peer);
            pos += PeerEntry::SIZE;
        }
        
        std::cout << "[Bootstrap] Got " << peers.size() << " peers from " 
                  << host << ":" << port << std::endl;
        
        return peers;
    }
    
    // Announce ourselves to bootstrap nodes
    bool announce(uint16_t listen_port, uint32_t torus_id) {
        bool success = false;
        
        for (const auto& [host, port] : bootstrap_nodes_) {
            if (announceTo(host, port, listen_port, torus_id)) {
                success = true;
            }
        }
        
        return success;
    }
    
    // Announce to specific bootstrap node
    bool announceTo(const std::string& host, uint16_t boot_port, 
                    uint16_t listen_port, uint32_t torus_id) {
        TcpSocket sock;
        if (!sock.connect(host, boot_port, 5000)) {
            return false;
        }
        
        // Build announce message
        std::vector<uint8_t> msg;
        msg.resize(BootstrapHeader::SIZE + 7);
        
        BootstrapHeader hdr;
        hdr.magic = BOOTSTRAP_MAGIC;
        hdr.type = BootstrapMsgType::ANNOUNCE;
        hdr.payload_len = 7;
        hdr.reserved = 0;
        hdr.serialize(msg.data());
        
        std::memcpy(msg.data() + BootstrapHeader::SIZE, &listen_port, 2);
        std::memcpy(msg.data() + BootstrapHeader::SIZE + 2, &torus_id, 4);
        msg[BootstrapHeader::SIZE + 6] = 1;  // services
        
        if (!sock.sendAll(msg.data(), msg.size())) {
            return false;
        }
        
        // Wait for ACK
        uint8_t ack_buf[BootstrapHeader::SIZE];
        if (!sock.recvAll(ack_buf, BootstrapHeader::SIZE, 5000)) {
            return false;
        }
        
        BootstrapHeader ack;
        if (!ack.deserialize(ack_buf) || ack.type != BootstrapMsgType::ANNOUNCE_ACK) {
            return false;
        }
        
        std::cout << "[Bootstrap] Announced to " << host << ":" << boot_port << std::endl;
        return true;
    }
};

// ============================================================================
// DNS Seeds (for production deployment)
// ============================================================================

class DnsSeeder {
public:
    // Well-known DNS seeds for ARQON network
    static std::vector<std::string> getSeeds() {
        return {
            "seed1.arqon.network",
            "seed2.arqon.network",
            "seed3.arqon.network",
            "dnsseed.arqon.io"
        };
    }
    
    // Resolve DNS seed to IP addresses
    static std::vector<uint32_t> resolve(const std::string& seed) {
        std::vector<uint32_t> ips;
        
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(seed.c_str(), nullptr, &hints, &result) != 0) {
            return ips;
        }
        
        for (struct addrinfo* p = result; p != nullptr; p = p->ai_next) {
            struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
            ips.push_back(ntohl(addr->sin_addr.s_addr));
        }
        
        freeaddrinfo(result);
        return ips;
    }
    
    // Get all bootstrap node IPs from DNS seeds
    static std::vector<std::pair<uint32_t, uint16_t>> getAllBootstrapNodes() {
        std::vector<std::pair<uint32_t, uint16_t>> nodes;
        
        for (const auto& seed : getSeeds()) {
            auto ips = resolve(seed);
            for (uint32_t ip : ips) {
                nodes.push_back({ip, BOOTSTRAP_PORT});
            }
        }
        
        return nodes;
    }
};

} // namespace network
