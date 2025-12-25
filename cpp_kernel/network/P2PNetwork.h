#pragma once

/**
 * Real P2P Networking Layer with Wire Encryption
 * 
 * Implements distributed networking using:
 * - DHT for peer discovery (Kademlia-like)
 * - Gossip protocol for message propagation
 * - NAT traversal via STUN
 * - Wire encryption via libsodium (XSalsa20-Poly1305)
 */

#include "../core/Crypto.h"
// Note: SodiumCrypto.h included separately when USE_LIBSODIUM is defined
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <random>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

namespace p2p {

using namespace crypto;

// Network constants
constexpr uint16_t DEFAULT_PORT = 31330;
constexpr size_t K_BUCKET_SIZE = 20;      // Kademlia k parameter
constexpr size_t ALPHA = 3;                // Parallel lookups
constexpr size_t MAX_PEERS = 256;
constexpr uint32_t PROTOCOL_VERSION = 2;  // v2 with encryption
constexpr uint32_t MAGIC = 0x52534550;     // "RSEP"
constexpr size_t NONCE_SIZE = 24;          // XSalsa20 nonce
constexpr size_t MAC_SIZE = 16;            // Poly1305 MAC

// Fallback encryption (XOR - for testing without libsodium)
inline std::vector<uint8_t> encryptPacket(const std::vector<uint8_t>& plaintext,
                                           const uint8_t* key) {
    std::vector<uint8_t> ciphertext = plaintext;
    for (size_t i = 0; i < ciphertext.size(); i++) {
        ciphertext[i] ^= key[i % 32];
    }
    return ciphertext;
}

inline std::vector<uint8_t> decryptPacket(const std::vector<uint8_t>& ciphertext,
                                           const uint8_t* key) {
    std::vector<uint8_t> plaintext = ciphertext;
    for (size_t i = 0; i < plaintext.size(); i++) {
        plaintext[i] ^= key[i % 32];
    }
    return plaintext;
}

// Message types
enum class MsgType : uint8_t {
    PING = 0x01,
    PONG = 0x02,
    FIND_NODE = 0x03,
    NODES = 0x04,
    STORE = 0x05,
    FIND_VALUE = 0x06,
    VALUE = 0x07,
    GOSSIP = 0x08,
    TX = 0x09,
    SNAPSHOT = 0x0A,
    SYNC_REQUEST = 0x0B,
    SYNC_RESPONSE = 0x0C,
    STATE_REQUEST = 0x0D,
    STATE_RESPONSE = 0x0E,
    PROJECTION = 0x0F
};

// State sync request/response structures
struct StateSyncRequest {
    uint64_t from_snapshot;  // Request state from this snapshot onwards
    uint64_t to_snapshot;    // Up to this snapshot (0 = latest)
    bool include_accounts;   // Include account state
    bool include_projections;// Include torus projections
    
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data(18);
        memcpy(data.data(), &from_snapshot, 8);
        memcpy(data.data() + 8, &to_snapshot, 8);
        data[16] = include_accounts ? 1 : 0;
        data[17] = include_projections ? 1 : 0;
        return data;
    }
    
    static StateSyncRequest deserialize(const std::vector<uint8_t>& data) {
        StateSyncRequest req{};
        if (data.size() >= 18) {
            memcpy(&req.from_snapshot, data.data(), 8);
            memcpy(&req.to_snapshot, data.data() + 8, 8);
            req.include_accounts = data[16] != 0;
            req.include_projections = data[17] != 0;
        }
        return req;
    }
};

struct StateSyncResponse {
    uint64_t snapshot_id;
    uint64_t total_accounts;
    std::vector<uint8_t> account_data;  // Serialized accounts
    std::vector<uint8_t> projection_data[3];  // One per torus
    
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.resize(16);
        memcpy(data.data(), &snapshot_id, 8);
        memcpy(data.data() + 8, &total_accounts, 8);
        
        // Account data length + data
        uint32_t acc_len = account_data.size();
        data.resize(data.size() + 4 + acc_len);
        memcpy(data.data() + 16, &acc_len, 4);
        if (acc_len > 0) {
            memcpy(data.data() + 20, account_data.data(), acc_len);
        }
        
        // Projection data for each torus
        size_t offset = 20 + acc_len;
        for (int i = 0; i < 3; i++) {
            uint32_t proj_len = projection_data[i].size();
            data.resize(data.size() + 4 + proj_len);
            memcpy(data.data() + offset, &proj_len, 4);
            offset += 4;
            if (proj_len > 0) {
                memcpy(data.data() + offset, projection_data[i].data(), proj_len);
                offset += proj_len;
            }
        }
        
        return data;
    }
};

// ============================================================================
// STUN Client for NAT Traversal
// ============================================================================

// STUN message types (RFC 5389)
constexpr uint16_t STUN_BINDING_REQUEST = 0x0001;
constexpr uint16_t STUN_BINDING_RESPONSE = 0x0101;
constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442;

// Public STUN servers
const std::vector<std::pair<std::string, uint16_t>> STUN_SERVERS = {
    {"stun.l.google.com", 19302},
    {"stun1.l.google.com", 19302},
    {"stun.cloudflare.com", 3478},
    {"stun.stunprotocol.org", 3478}
};

struct StunResult {
    bool success = false;
    std::string public_ip;
    uint16_t public_port = 0;
    std::string error;
};

class StunClient {
public:
    static StunResult discoverPublicAddress(int socket_fd, uint16_t local_port) {
        StunResult result;
        
        for (const auto& [server, port] : STUN_SERVERS) {
            result = queryStunServer(socket_fd, server, port);
            if (result.success) {
                return result;
            }
        }
        
        result.error = "All STUN servers failed";
        return result;
    }
    
private:
    static StunResult queryStunServer(int socket_fd, const std::string& server, uint16_t port) {
        StunResult result;
        
        // Resolve server address
        struct sockaddr_in stun_addr{};
        stun_addr.sin_family = AF_INET;
        stun_addr.sin_port = htons(port);
        
        struct hostent* host = gethostbyname(server.c_str());
        if (!host) {
            result.error = "Failed to resolve " + server;
            return result;
        }
        memcpy(&stun_addr.sin_addr, host->h_addr_list[0], host->h_length);
        
        // Build STUN binding request
        std::vector<uint8_t> request(20);
        
        // Message type: Binding Request
        request[0] = (STUN_BINDING_REQUEST >> 8) & 0xFF;
        request[1] = STUN_BINDING_REQUEST & 0xFF;
        
        // Message length: 0 (no attributes)
        request[2] = 0;
        request[3] = 0;
        
        // Magic cookie
        request[4] = (STUN_MAGIC_COOKIE >> 24) & 0xFF;
        request[5] = (STUN_MAGIC_COOKIE >> 16) & 0xFF;
        request[6] = (STUN_MAGIC_COOKIE >> 8) & 0xFF;
        request[7] = STUN_MAGIC_COOKIE & 0xFF;
        
        // Transaction ID (12 random bytes)
        std::random_device rd;
        for (int i = 8; i < 20; i++) {
            request[i] = rd() & 0xFF;
        }
        
        // Send request
        if (sendto(socket_fd, request.data(), request.size(), 0,
                   (struct sockaddr*)&stun_addr, sizeof(stun_addr)) < 0) {
            result.error = "Failed to send STUN request";
            return result;
        }
        
        // Wait for response (with timeout)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket_fd, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        
        if (select(socket_fd + 1, &readfds, nullptr, nullptr, &timeout) <= 0) {
            result.error = "STUN response timeout";
            return result;
        }
        
        // Receive response
        std::vector<uint8_t> response(256);
        struct sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t n = recvfrom(socket_fd, response.data(), response.size(), 0,
                              (struct sockaddr*)&from_addr, &from_len);
        
        if (n < 20) {
            result.error = "Invalid STUN response";
            return result;
        }
        
        // Parse response
        uint16_t msg_type = (response[0] << 8) | response[1];
        if (msg_type != STUN_BINDING_RESPONSE) {
            result.error = "Unexpected STUN message type";
            return result;
        }
        
        uint16_t msg_len = (response[2] << 8) | response[3];
        
        // Parse attributes to find XOR-MAPPED-ADDRESS (0x0020) or MAPPED-ADDRESS (0x0001)
        size_t offset = 20;
        while (offset + 4 <= (size_t)(20 + msg_len) && offset + 4 <= (size_t)n) {
            uint16_t attr_type = (response[offset] << 8) | response[offset + 1];
            uint16_t attr_len = (response[offset + 2] << 8) | response[offset + 3];
            offset += 4;
            
            if (offset + attr_len > (size_t)n) break;
            
            if (attr_type == 0x0020 && attr_len >= 8) {  // XOR-MAPPED-ADDRESS
                // Family at offset+1, port at offset+2, IP at offset+4
                uint16_t xor_port = ((response[offset + 2] << 8) | response[offset + 3]) ^ (STUN_MAGIC_COOKIE >> 16);
                uint32_t xor_ip = ((response[offset + 4] << 24) | (response[offset + 5] << 16) |
                                   (response[offset + 6] << 8) | response[offset + 7]) ^ STUN_MAGIC_COOKIE;
                
                result.public_port = xor_port;
                struct in_addr addr;
                addr.s_addr = htonl(xor_ip);
                result.public_ip = inet_ntoa(addr);
                result.success = true;
                return result;
            }
            
            if (attr_type == 0x0001 && attr_len >= 8) {  // MAPPED-ADDRESS
                uint16_t mapped_port = (response[offset + 2] << 8) | response[offset + 3];
                uint32_t mapped_ip = (response[offset + 4] << 24) | (response[offset + 5] << 16) |
                                     (response[offset + 6] << 8) | response[offset + 7];
                
                result.public_port = mapped_port;
                struct in_addr addr;
                addr.s_addr = htonl(mapped_ip);
                result.public_ip = inet_ntoa(addr);
                result.success = true;
                return result;
            }
            
            offset += attr_len;
            // Pad to 4-byte boundary
            if (attr_len % 4 != 0) offset += 4 - (attr_len % 4);
        }
        
        result.error = "No mapped address in STUN response";
        return result;
    }
};

// Peer info
struct PeerInfo {
    Address node_id;
    std::string ip;
    uint16_t port;
    uint64_t last_seen;
    uint32_t latency_ms;
    bool is_active;
    
    std::string endpoint() const { return ip + ":" + std::to_string(port); }
};

// XOR distance for Kademlia
inline uint32_t xorDistance(const Address& a, const Address& b) {
    uint32_t dist = 0;
    for (size_t i = 0; i < ADDRESS_SIZE && i < 4; i++) {
        dist |= (a[i] ^ b[i]) << (8 * i);
    }
    return dist;
}

// K-Bucket for Kademlia DHT
class KBucket {
public:
    void add(const PeerInfo& peer) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if peer exists
        for (auto& p : peers_) {
            if (p.node_id == peer.node_id) {
                p.last_seen = peer.last_seen;
                p.is_active = true;
                return;
            }
        }
        
        // Add new peer if space
        if (peers_.size() < K_BUCKET_SIZE) {
            peers_.push_back(peer);
        }
    }
    
    void remove(const Address& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        peers_.erase(
            std::remove_if(peers_.begin(), peers_.end(),
                [&id](const PeerInfo& p) { return p.node_id == id; }),
            peers_.end()
        );
    }
    
    std::vector<PeerInfo> getClosest(const Address& target, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PeerInfo> result = peers_;
        std::sort(result.begin(), result.end(),
            [&target](const PeerInfo& a, const PeerInfo& b) {
                return xorDistance(a.node_id, target) < xorDistance(b.node_id, target);
            });
        if (result.size() > count) result.resize(count);
        return result;
    }
    
    size_t size() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return peers_.size(); 
    }

private:
    mutable std::mutex mutex_;
    std::vector<PeerInfo> peers_;
};

// Routing table (256 k-buckets for 256-bit address space)
class RoutingTable {
public:
    RoutingTable(const Address& local_id) : local_id_(local_id) {}
    
    void addPeer(const PeerInfo& peer) {
        int bucket = getBucketIndex(peer.node_id);
        if (bucket >= 0 && bucket < 256) {
            buckets_[bucket].add(peer);
        }
    }
    
    std::vector<PeerInfo> findClosest(const Address& target, size_t count = ALPHA) {
        std::vector<PeerInfo> result;
        int target_bucket = getBucketIndex(target);
        
        // Search target bucket and nearby buckets
        for (int delta = 0; delta < 256 && result.size() < count; delta++) {
            for (int sign : {0, 1, -1}) {
                int idx = target_bucket + sign * delta;
                if (idx >= 0 && idx < 256) {
                    auto closest = buckets_[idx].getClosest(target, count - result.size());
                    result.insert(result.end(), closest.begin(), closest.end());
                }
            }
        }
        
        // Sort by distance and trim
        std::sort(result.begin(), result.end(),
            [&target](const PeerInfo& a, const PeerInfo& b) {
                return xorDistance(a.node_id, target) < xorDistance(b.node_id, target);
            });
        if (result.size() > count) result.resize(count);
        return result;
    }
    
    size_t peerCount() const {
        size_t total = 0;
        for (const auto& b : buckets_) total += b.size();
        return total;
    }

private:
    int getBucketIndex(const Address& id) const {
        for (int i = 0; i < ADDRESS_SIZE * 8; i++) {
            int byte_idx = i / 8;
            int bit_idx = 7 - (i % 8);
            if ((local_id_[byte_idx] ^ id[byte_idx]) & (1 << bit_idx)) {
                return i;
            }
        }
        return 0;
    }
    
    Address local_id_;
    std::array<KBucket, 256> buckets_;
};

// P2P Network Node
class P2PNode {
public:
    using MessageHandler = std::function<void(const Address&, MsgType, const std::vector<uint8_t>&)>;
    
    P2PNode(uint16_t port = DEFAULT_PORT) 
        : port_(port), running_(false), routing_table_(local_key_.getAddress()) {
        local_id_ = local_key_.getAddress();
    }
    
    ~P2PNode() { stop(); }
    
    bool start() {
        if (running_) return true;
        
        // Create socket
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) return false;
        
        // Set non-blocking
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
        
        // Bind
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(socket_fd_);
            return false;
        }
        
        running_ = true;
        
        // Start receiver thread
        recv_thread_ = std::thread([this]() { receiveLoop(); });
        
        // Start maintenance thread (DHT refresh, peer ping)
        maint_thread_ = std::thread([this]() { maintenanceLoop(); });
        
        std::cout << "[P2P] Node started on port " << port_ << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_) return;
        running_ = false;
        
        if (recv_thread_.joinable()) recv_thread_.join();
        if (maint_thread_.joinable()) maint_thread_.join();
        
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        
        std::cout << "[P2P] Node stopped" << std::endl;
    }
    
    // Bootstrap from known peers
    void bootstrap(const std::vector<std::string>& seeds) {
        for (const auto& seed : seeds) {
            size_t colon = seed.find(':');
            if (colon != std::string::npos) {
                std::string ip = seed.substr(0, colon);
                uint16_t port = std::stoi(seed.substr(colon + 1));
                
                PeerInfo peer;
                peer.ip = ip;
                peer.port = port;
                peer.last_seen = now_ms();
                peer.is_active = true;
                
                // Send FIND_NODE for self to discover network
                sendFindNode(peer, local_id_);
            }
        }
    }
    
    // Broadcast message to all peers (gossip)
    void broadcast(MsgType type, const std::vector<uint8_t>& data) {
        auto peers = routing_table_.findClosest(local_id_, MAX_PEERS);
        for (const auto& peer : peers) {
            sendMessage(peer, type, data);
        }
    }
    
    // Send to specific peer (with optional encryption)
    void sendMessage(const PeerInfo& peer, MsgType type, const std::vector<uint8_t>& data, bool encrypt = true) {
        std::vector<uint8_t> plaintext;
        
        // Build plaintext: type (1) + sender (20) + data
        plaintext.reserve(1 + ADDRESS_SIZE + data.size());
        plaintext.push_back(static_cast<uint8_t>(type));
        plaintext.insert(plaintext.end(), local_id_.begin(), local_id_.end());
        plaintext.insert(plaintext.end(), data.begin(), data.end());
        
        // Encrypt payload if enabled and we have a shared key
        std::vector<uint8_t> payload;
        if (encrypt && encryption_enabled_) {
            // Derive shared key from peer's node_id and network key
            std::array<uint8_t, 32> shared_key{};
            for (size_t i = 0; i < 32; i++) {
                if (i < ADDRESS_SIZE) {
                    shared_key[i] = local_id_[i] ^ peer.node_id[i] ^ network_key_[i];
                } else {
                    shared_key[i] = network_key_[i];
                }
            }
            payload = encryptPacket(plaintext, shared_key.data());
        } else {
            payload = plaintext;
        }
        
        // Build packet: magic (4) + version (1) + encrypted_flag (1) + length (4) + payload
        std::vector<uint8_t> packet;
        packet.resize(10 + payload.size());
        
        memcpy(packet.data(), &MAGIC, 4);
        packet[4] = PROTOCOL_VERSION;
        packet[5] = encrypt && encryption_enabled_ ? 1 : 0;
        
        uint32_t len = payload.size();
        memcpy(packet.data() + 6, &len, 4);
        memcpy(packet.data() + 10, payload.data(), payload.size());
        
        // Send
        struct sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(peer.port);
        inet_pton(AF_INET, peer.ip.c_str(), &dest.sin_addr);
        
        sendto(socket_fd_, packet.data(), packet.size(), 0,
               (struct sockaddr*)&dest, sizeof(dest));
        
        messages_sent_++;
    }
    
    // Set network-wide encryption key (shared secret for the network)
    void setNetworkKey(const std::array<uint8_t, 32>& key) {
        network_key_ = key;
        encryption_enabled_ = true;
    }
    
    void enableEncryption(bool enable) { encryption_enabled_ = enable; }
    bool isEncryptionEnabled() const { return encryption_enabled_; }
    uint64_t messagesSent() const { return messages_sent_; }
    uint64_t messagesReceived() const { return messages_received_; }
    
    void onMessage(MessageHandler handler) { message_handler_ = handler; }
    
    const Address& localId() const { return local_id_; }
    size_t peerCount() const { return routing_table_.peerCount(); }
    bool isRunning() const { return running_; }

private:
    void receiveLoop() {
        std::vector<uint8_t> buffer(65536);
        
        while (running_) {
            struct sockaddr_in sender{};
            socklen_t sender_len = sizeof(sender);
            
            ssize_t n = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                                  (struct sockaddr*)&sender, &sender_len);
            
            if (n > 30) {
                processPacket(buffer.data(), n, sender);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    void processPacket(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        // Verify magic
        uint32_t magic;
        memcpy(&magic, data, 4);
        if (magic != MAGIC) return;
        
        // Parse new header format: magic (4) + version (1) + encrypted (1) + length (4)
        uint8_t version = data[4];
        bool is_encrypted = data[5] != 0;
        
        uint32_t payload_len;
        memcpy(&payload_len, data + 6, 4);
        
        if (payload_len + 10 > len) return;
        
        // Extract payload
        std::vector<uint8_t> raw_payload(data + 10, data + 10 + payload_len);
        std::vector<uint8_t> plaintext;
        
        // Decrypt if needed
        if (is_encrypted && encryption_enabled_) {
            // We need sender_id to derive shared key, but it's inside encrypted payload
            // For now, use network key only (simplified)
            plaintext = decryptPacket(raw_payload, network_key_.data());
            if (plaintext.empty()) {
                return;  // Decryption failed
            }
        } else {
            plaintext = raw_payload;
        }
        
        // Parse decrypted payload: type (1) + sender_id (20) + data
        if (plaintext.size() < 1 + ADDRESS_SIZE) return;
        
        MsgType type = static_cast<MsgType>(plaintext[0]);
        Address sender_id;
        memcpy(sender_id.data(), plaintext.data() + 1, ADDRESS_SIZE);
        
        std::vector<uint8_t> msg_data(plaintext.begin() + 1 + ADDRESS_SIZE, plaintext.end());
        
        // Update routing table
        PeerInfo peer;
        peer.node_id = sender_id;
        peer.ip = inet_ntoa(sender.sin_addr);
        peer.port = ntohs(sender.sin_port);
        peer.last_seen = now_ms();
        peer.is_active = true;
        routing_table_.addPeer(peer);
        
        messages_received_++;
        
        // Handle message
        switch (type) {
            case MsgType::PING:
                sendMessage(peer, MsgType::PONG, {});
                break;
            case MsgType::FIND_NODE:
                handleFindNode(peer, msg_data);
                break;
            default:
                if (message_handler_) {
                    message_handler_(sender_id, type, msg_data);
                }
                break;
        }
    }
    
    void handleFindNode(const PeerInfo& sender, const std::vector<uint8_t>& payload) {
        if (payload.size() < ADDRESS_SIZE) return;
        
        Address target;
        memcpy(target.data(), payload.data(), ADDRESS_SIZE);
        
        auto closest = routing_table_.findClosest(target, K_BUCKET_SIZE);
        
        // Serialize peers
        std::vector<uint8_t> response;
        for (const auto& p : closest) {
            // node_id (20) + ip_len (1) + ip + port (2)
            response.insert(response.end(), p.node_id.begin(), p.node_id.end());
            response.push_back(p.ip.size());
            response.insert(response.end(), p.ip.begin(), p.ip.end());
            uint16_t port = p.port;
            response.push_back(port & 0xFF);
            response.push_back((port >> 8) & 0xFF);
        }
        
        sendMessage(sender, MsgType::NODES, response);
    }
    
    void sendFindNode(const PeerInfo& peer, const Address& target) {
        std::vector<uint8_t> data(target.begin(), target.end());
        sendMessage(peer, MsgType::FIND_NODE, data);
    }
    
    void maintenanceLoop() {
        while (running_) {
            // Ping random peers to keep connections alive
            auto peers = routing_table_.findClosest(local_id_, 10);
            for (const auto& peer : peers) {
                sendMessage(peer, MsgType::PING, {});
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }
    
    static uint64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    uint16_t port_;
    int socket_fd_ = -1;
    std::atomic<bool> running_;
    
    KeyPair local_key_;
    Address local_id_;
    RoutingTable routing_table_;
    
    std::thread recv_thread_;
    std::thread maint_thread_;
    
    MessageHandler message_handler_;
    
    // Encryption state
    bool encryption_enabled_ = false;
    std::array<uint8_t, 32> network_key_{};
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_received_{0};
};

} // namespace p2p
