#pragma once

/**
 * ARQON Distributed Torus Network
 * Phase 9: Network communication between tori across machines
 */

#include <cstdint>
#include <cstring>
#include <functional>
#include "../drivers/e1000.h"

namespace distributed {

// Network constants
constexpr uint16_t TORUS_PORT = 31330;
constexpr uint16_t DISCOVERY_PORT = 31331;
constexpr size_t MAX_PEERS = 64;
constexpr size_t MAX_PACKET_SIZE = 1500;
constexpr uint32_t TORUS_MAGIC = 0x41525130;  // "ARQ0"

// Message types
enum class MsgType : uint8_t {
    PING = 0x01,
    PONG = 0x02,
    DISCOVER = 0x10,
    ANNOUNCE = 0x11,
    PROJECTION = 0x20,
    PROJECTION_ACK = 0x21,
    STATE_REQUEST = 0x30,
    STATE_RESPONSE = 0x31,
    TRANSACTION = 0x40,
    TRANSACTION_ACK = 0x41,
    CONSENSUS = 0x50,
    VOTE = 0x51,
    HEARTBEAT = 0x60
};

// Torus coordinates
struct TorusCoord {
    uint8_t torus_id;  // 0, 1, 2
    uint8_t x, y, z;   // 0-31 each
    
    uint32_t toIndex() const {
        return (torus_id * 32768) + (x * 1024) + (y * 32) + z;
    }
    
    static TorusCoord fromIndex(uint32_t idx) {
        TorusCoord c;
        c.torus_id = idx / 32768;
        idx %= 32768;
        c.x = idx / 1024;
        idx %= 1024;
        c.y = idx / 32;
        c.z = idx % 32;
        return c;
    }
};

// Peer info
struct PeerInfo {
    uint8_t mac[6];
    uint32_t ip;
    uint16_t port;
    TorusCoord coord;
    uint64_t last_seen;
    uint64_t latency_us;
    bool active;
    
    void setIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        ip = (a << 24) | (b << 16) | (c << 8) | d;
    }
};

// Network packet header
struct PacketHeader {
    uint32_t magic;
    uint8_t version;
    MsgType type;
    uint16_t payload_len;
    uint64_t timestamp;
    TorusCoord src;
    TorusCoord dst;
    uint32_t sequence;
    uint8_t checksum;
    uint8_t reserved[3];
} __attribute__((packed));

static_assert(sizeof(PacketHeader) == 28, "PacketHeader must be 28 bytes");

// Projection packet
struct ProjectionPacket {
    PacketHeader header;
    uint64_t epoch;
    uint8_t state_hash[32];    // Blake2b hash of torus state
    uint16_t delta_count;
    uint8_t delta_data[1024];  // Compressed state deltas
} __attribute__((packed));

// Transaction packet
struct TransactionPacket {
    PacketHeader header;
    uint8_t tx_hash[32];
    uint8_t signature[64];
    uint8_t from_addr[20];
    uint8_t to_addr[20];
    uint64_t amount;
    uint64_t nonce;
    uint16_t data_len;
    uint8_t data[256];
} __attribute__((packed));

// Discovery/Announce packet
struct DiscoveryPacket {
    PacketHeader header;
    uint8_t node_pubkey[32];
    TorusCoord coord;
    uint16_t capabilities;
    uint32_t version;
    char hostname[64];
} __attribute__((packed));

// Heartbeat packet
struct HeartbeatPacket {
    PacketHeader header;
    uint64_t uptime;
    uint64_t processed_txs;
    uint64_t epoch;
    uint32_t peer_count;
    uint8_t load_percent;
    uint8_t reserved[3];
} __attribute__((packed));

// Network statistics
struct NetworkStats {
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t projections_sent;
    uint64_t projections_recv;
    uint64_t txs_relayed;
    uint64_t errors;
    uint64_t timeouts;
};

// Torus Network Manager
// Callback types for handling incoming data
using ProjectionCallback = std::function<void(const TorusCoord& src, const uint8_t* hash, 
                                               const uint8_t* data, size_t len)>;
using TransactionCallback = std::function<void(const TorusCoord& src, const uint8_t* hash,
                                                const uint8_t* data, size_t len)>;

class TorusNetwork {
private:
    drivers::E1000* nic_;
    PeerInfo peers_[MAX_PEERS];
    size_t peer_count_;
    TorusCoord local_coord_;
    uint32_t sequence_;
    NetworkStats stats_;
    bool running_;
    
    // Callbacks for incoming data
    ProjectionCallback projection_callback_;
    TransactionCallback transaction_callback_;
    
    // Packet buffers
    uint8_t tx_buffer_[MAX_PACKET_SIZE];
    uint8_t rx_buffer_[MAX_PACKET_SIZE];
    
    uint8_t computeChecksum(const uint8_t* data, size_t len) {
        uint8_t sum = 0;
        for (size_t i = 0; i < len; i++) {
            sum ^= data[i];
            sum = (sum << 1) | (sum >> 7);
        }
        return sum;
    }
    
    void buildHeader(PacketHeader* hdr, MsgType type, uint16_t payload_len, TorusCoord dst) {
        hdr->magic = TORUS_MAGIC;
        hdr->version = 1;
        hdr->type = type;
        hdr->payload_len = payload_len;
        hdr->timestamp = rdtsc();
        hdr->src = local_coord_;
        hdr->dst = dst;
        hdr->sequence = sequence_++;
        hdr->checksum = 0;
        hdr->reserved[0] = hdr->reserved[1] = hdr->reserved[2] = 0;
    }
    
    static uint64_t rdtsc() {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }

public:
    TorusNetwork() : nic_(nullptr), peer_count_(0), sequence_(0), running_(false) {
        memset(&stats_, 0, sizeof(stats_));
        memset(peers_, 0, sizeof(peers_));
    }
    
    bool init(drivers::E1000* nic, TorusCoord coord) {
        nic_ = nic;
        local_coord_ = coord;
        running_ = true;
        return true;
    }
    
    // Set callback for incoming projections
    void setProjectionCallback(ProjectionCallback cb) {
        projection_callback_ = std::move(cb);
    }
    
    // Set callback for incoming transactions
    void setTransactionCallback(TransactionCallback cb) {
        transaction_callback_ = std::move(cb);
    }
    
    // Send projection to sibling tori
    bool sendProjection(uint8_t target_torus, uint64_t epoch, 
                        const uint8_t* state_hash, 
                        const uint8_t* deltas, size_t delta_len) {
        if (!nic_ || !running_) return false;
        
        ProjectionPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        
        TorusCoord dst = local_coord_;
        dst.torus_id = target_torus;
        
        buildHeader(&pkt.header, MsgType::PROJECTION, 
                   sizeof(pkt) - sizeof(PacketHeader), dst);
        
        pkt.epoch = epoch;
        memcpy(pkt.state_hash, state_hash, 32);
        pkt.delta_count = delta_len > 1024 ? 1024 : delta_len;
        memcpy(pkt.delta_data, deltas, pkt.delta_count);
        
        pkt.header.checksum = computeChecksum(
            reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
        
        // Broadcast to all peers on target torus
        for (size_t i = 0; i < peer_count_; i++) {
            if (peers_[i].active && peers_[i].coord.torus_id == target_torus) {
                if (nic_->send(&pkt, sizeof(pkt))) {
                    stats_.packets_sent++;
                    stats_.bytes_sent += sizeof(pkt);
                    stats_.projections_sent++;
                }
            }
        }
        
        return true;
    }
    
    // Broadcast transaction to network
    bool broadcastTransaction(const uint8_t* tx_hash, const uint8_t* signature,
                             const uint8_t* from, const uint8_t* to,
                             uint64_t amount, uint64_t nonce,
                             const uint8_t* data, size_t data_len) {
        if (!nic_ || !running_) return false;
        
        TransactionPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        
        TorusCoord broadcast = {0xFF, 0xFF, 0xFF, 0xFF};  // Broadcast
        buildHeader(&pkt.header, MsgType::TRANSACTION,
                   sizeof(pkt) - sizeof(PacketHeader), broadcast);
        
        memcpy(pkt.tx_hash, tx_hash, 32);
        memcpy(pkt.signature, signature, 64);
        memcpy(pkt.from_addr, from, 20);
        memcpy(pkt.to_addr, to, 20);
        pkt.amount = amount;
        pkt.nonce = nonce;
        pkt.data_len = data_len > 256 ? 256 : data_len;
        if (data && data_len > 0) {
            memcpy(pkt.data, data, pkt.data_len);
        }
        
        pkt.header.checksum = computeChecksum(
            reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
        
        // Send to all active peers
        for (size_t i = 0; i < peer_count_; i++) {
            if (peers_[i].active) {
                nic_->send(&pkt, sizeof(pkt));
                stats_.packets_sent++;
                stats_.bytes_sent += sizeof(pkt);
            }
        }
        
        stats_.txs_relayed++;
        return true;
    }
    
    // Send discovery broadcast
    void sendDiscovery() {
        if (!nic_ || !running_) return;
        
        DiscoveryPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        
        TorusCoord broadcast = {0xFF, 0xFF, 0xFF, 0xFF};
        buildHeader(&pkt.header, MsgType::DISCOVER,
                   sizeof(pkt) - sizeof(PacketHeader), broadcast);
        
        pkt.coord = local_coord_;
        pkt.capabilities = 0x0001;  // Basic node
        pkt.version = 1;
        strcpy(pkt.hostname, "arqon-node");
        
        pkt.header.checksum = computeChecksum(
            reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
        
        nic_->send(&pkt, sizeof(pkt));
        stats_.packets_sent++;
        stats_.bytes_sent += sizeof(pkt);
    }
    
    // Send heartbeat to peers
    void sendHeartbeat(uint64_t uptime, uint64_t processed_txs, uint64_t epoch) {
        if (!nic_ || !running_) return;
        
        HeartbeatPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        
        TorusCoord broadcast = {0xFF, 0xFF, 0xFF, 0xFF};
        buildHeader(&pkt.header, MsgType::HEARTBEAT,
                   sizeof(pkt) - sizeof(PacketHeader), broadcast);
        
        pkt.uptime = uptime;
        pkt.processed_txs = processed_txs;
        pkt.epoch = epoch;
        pkt.peer_count = peer_count_;
        pkt.load_percent = static_cast<uint8_t>(stats_.packets_recv > 0 ? 
            std::min(100UL, (stats_.packets_recv * 100) / (stats_.packets_recv + 1000)) : 0);
        
        pkt.header.checksum = computeChecksum(
            reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
        
        for (size_t i = 0; i < peer_count_; i++) {
            if (peers_[i].active) {
                nic_->send(&pkt, sizeof(pkt));
                stats_.packets_sent++;
                stats_.bytes_sent += sizeof(pkt);
            }
        }
    }
    
    // Process incoming packet
    void processPacket(const uint8_t* data, size_t len) {
        if (len < sizeof(PacketHeader)) return;
        
        const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(data);
        
        // Validate magic
        if (hdr->magic != TORUS_MAGIC) return;
        
        // Validate checksum
        uint8_t expected = computeChecksum(data, len);
        if (hdr->checksum != 0 && expected != 0) {
            // Checksum mismatch
            stats_.errors++;
            return;
        }
        
        stats_.packets_recv++;
        stats_.bytes_recv += len;
        
        switch (hdr->type) {
            case MsgType::PING:
                handlePing(hdr);
                break;
            case MsgType::DISCOVER:
                handleDiscovery(reinterpret_cast<const DiscoveryPacket*>(data));
                break;
            case MsgType::ANNOUNCE:
                handleAnnounce(reinterpret_cast<const DiscoveryPacket*>(data));
                break;
            case MsgType::PROJECTION:
                handleProjection(reinterpret_cast<const ProjectionPacket*>(data));
                stats_.projections_recv++;
                break;
            case MsgType::TRANSACTION:
                handleTransaction(reinterpret_cast<const TransactionPacket*>(data));
                break;
            case MsgType::HEARTBEAT:
                handleHeartbeat(reinterpret_cast<const HeartbeatPacket*>(data));
                break;
            default:
                break;
        }
    }
    
    // Poll for incoming packets
    void poll() {
        if (!nic_ || !running_) return;
        
        int len = nic_->receive(rx_buffer_, MAX_PACKET_SIZE);
        if (len > 0) {
            processPacket(rx_buffer_, len);
        }
    }
    
    // Add peer
    bool addPeer(const PeerInfo& peer) {
        if (peer_count_ >= MAX_PEERS) return false;
        
        // Check if already exists
        for (size_t i = 0; i < peer_count_; i++) {
            if (peers_[i].ip == peer.ip && peers_[i].port == peer.port) {
                peers_[i] = peer;
                return true;
            }
        }
        
        peers_[peer_count_++] = peer;
        return true;
    }
    
    // Remove inactive peers
    void pruneInactivePeers(uint64_t timeout_us) {
        uint64_t now = rdtsc();
        
        for (size_t i = 0; i < peer_count_; i++) {
            if (now - peers_[i].last_seen > timeout_us * 3000) {  // Rough cycle estimate
                peers_[i].active = false;
            }
        }
    }
    
    // Getters
    size_t getPeerCount() const { return peer_count_; }
    const PeerInfo* getPeers() const { return peers_; }
    const NetworkStats& getStats() const { return stats_; }
    const TorusCoord& getLocalCoord() const { return local_coord_; }
    
    void stop() { running_ = false; }

private:
    void handlePing(const PacketHeader* hdr) {
        // Send PONG response
        PacketHeader pong;
        buildHeader(&pong, MsgType::PONG, 0, hdr->src);
        pong.checksum = computeChecksum(
            reinterpret_cast<uint8_t*>(&pong), sizeof(pong));
        nic_->send(&pong, sizeof(pong));
        stats_.packets_sent++;
        stats_.bytes_sent += sizeof(pong);
    }
    
    void handleDiscovery(const DiscoveryPacket* pkt) {
        // Add peer and send announce response
        PeerInfo peer = {};
        peer.coord = pkt->coord;
        peer.last_seen = rdtsc();
        peer.active = true;
        addPeer(peer);
        
        // Send announce
        DiscoveryPacket response;
        memset(&response, 0, sizeof(response));
        buildHeader(&response.header, MsgType::ANNOUNCE,
                   sizeof(response) - sizeof(PacketHeader), pkt->coord);
        response.coord = local_coord_;
        response.capabilities = 0x0001;
        response.version = 1;
        strcpy(response.hostname, "arqon-node");
        response.header.checksum = computeChecksum(
            reinterpret_cast<uint8_t*>(&response), sizeof(response));
        nic_->send(&response, sizeof(response));
        stats_.packets_sent++;
        stats_.bytes_sent += sizeof(response);
    }
    
    void handleAnnounce(const DiscoveryPacket* pkt) {
        PeerInfo peer = {};
        peer.coord = pkt->coord;
        peer.last_seen = rdtsc();
        peer.active = true;
        addPeer(peer);
    }
    
    void handleProjection(const ProjectionPacket* pkt) {
        // Apply projection to local torus state
        // Projections contain boundary state from sibling tori for synchronization
        if (projection_callback_) {
            projection_callback_(pkt->header.src, pkt->boundary_hash, 
                                 pkt->boundary_data, sizeof(pkt->boundary_data));
        }
        stats_.projections_recv++;
    }
    
    void handleTransaction(const TransactionPacket* pkt) {
        // Validate and process transaction
        // Route to transaction callback for mempool/execution
        if (transaction_callback_) {
            transaction_callback_(pkt->header.src, pkt->tx_hash,
                                  pkt->tx_data, pkt->tx_len);
        }
    }
    
    void handleHeartbeat(const HeartbeatPacket* pkt) {
        // Update peer's last seen time
        for (size_t i = 0; i < peer_count_; i++) {
            if (peers_[i].coord.torus_id == pkt->header.src.torus_id &&
                peers_[i].coord.x == pkt->header.src.x &&
                peers_[i].coord.y == pkt->header.src.y &&
                peers_[i].coord.z == pkt->header.src.z) {
                peers_[i].last_seen = rdtsc();
                peers_[i].active = true;
                break;
            }
        }
    }
};

// Geographic distribution support
struct GeoLocation {
    double latitude;
    double longitude;
    char region[32];
    char datacenter[32];
    uint32_t latency_zone;  // 0 = local, 1 = regional, 2 = continental, 3 = global
};

class GeoRouter {
private:
    GeoLocation local_;
    GeoLocation peers_[MAX_PEERS];
    size_t peer_count_;
    
public:
    GeoRouter() : peer_count_(0) {}
    
    void setLocal(const GeoLocation& loc) {
        local_ = loc;
    }
    
    // Calculate approximate distance in km
    static double distance(const GeoLocation& a, const GeoLocation& b) {
        // Haversine formula (simplified)
        double lat1 = a.latitude * 0.0174533;  // deg to rad
        double lat2 = b.latitude * 0.0174533;
        double dlat = (b.latitude - a.latitude) * 0.0174533;
        double dlon = (b.longitude - a.longitude) * 0.0174533;
        
        double h = sin(dlat/2) * sin(dlat/2) +
                   cos(lat1) * cos(lat2) * sin(dlon/2) * sin(dlon/2);
        
        return 6371 * 2 * atan2(sqrt(h), sqrt(1-h));  // Earth radius = 6371 km
    }
    
    // Find nearest peer for routing
    size_t findNearestPeer() const {
        if (peer_count_ == 0) return SIZE_MAX;
        
        size_t nearest = 0;
        double min_dist = distance(local_, peers_[0]);
        
        for (size_t i = 1; i < peer_count_; i++) {
            double d = distance(local_, peers_[i]);
            if (d < min_dist) {
                min_dist = d;
                nearest = i;
            }
        }
        
        return nearest;
    }
    
    // Get expected latency zone
    uint32_t getLatencyZone(const GeoLocation& peer) const {
        double d = distance(local_, peer);
        if (d < 100) return 0;       // < 100km: local
        if (d < 1000) return 1;      // < 1000km: regional
        if (d < 5000) return 2;      // < 5000km: continental
        return 3;                     // > 5000km: global
    }
};

// Multi-machine fault tolerance
class FaultTolerance {
public:
    enum class NodeState {
        HEALTHY,
        DEGRADED,
        FAILED,
        RECOVERING
    };
    
    struct NodeHealth {
        TorusCoord coord;
        NodeState state;
        uint64_t last_heartbeat;
        uint32_t missed_heartbeats;
        uint32_t error_count;
        float availability;  // 0.0 - 1.0
    };
    
private:
    NodeHealth nodes_[MAX_PEERS];
    size_t node_count_;
    uint32_t heartbeat_interval_ms_;
    uint32_t failure_threshold_;
    
public:
    FaultTolerance() : node_count_(0), heartbeat_interval_ms_(1000), failure_threshold_(5) {}
    
    void updateHealth(TorusCoord coord, bool heartbeat_received) {
        // Find or create node entry
        NodeHealth* node = nullptr;
        for (size_t i = 0; i < node_count_; i++) {
            if (nodes_[i].coord.toIndex() == coord.toIndex()) {
                node = &nodes_[i];
                break;
            }
        }
        
        if (!node && node_count_ < MAX_PEERS) {
            node = &nodes_[node_count_++];
            node->coord = coord;
            node->state = NodeState::HEALTHY;
            node->missed_heartbeats = 0;
            node->error_count = 0;
            node->availability = 1.0f;
        }
        
        if (!node) return;
        
        if (heartbeat_received) {
            node->last_heartbeat = rdtsc();
            node->missed_heartbeats = 0;
            
            if (node->state == NodeState::FAILED || node->state == NodeState::RECOVERING) {
                node->state = NodeState::RECOVERING;
            } else {
                node->state = NodeState::HEALTHY;
            }
        } else {
            node->missed_heartbeats++;
            
            if (node->missed_heartbeats >= failure_threshold_) {
                node->state = NodeState::FAILED;
            } else if (node->missed_heartbeats >= failure_threshold_ / 2) {
                node->state = NodeState::DEGRADED;
            }
        }
        
        // Update availability (exponential moving average)
        float current = heartbeat_received ? 1.0f : 0.0f;
        node->availability = 0.9f * node->availability + 0.1f * current;
    }
    
    // Check if we have quorum for consensus
    bool hasQuorum() const {
        size_t healthy = 0;
        for (size_t i = 0; i < node_count_; i++) {
            if (nodes_[i].state == NodeState::HEALTHY || 
                nodes_[i].state == NodeState::DEGRADED) {
                healthy++;
            }
        }
        
        // Need majority (>50%) for quorum
        return healthy > node_count_ / 2;
    }
    
    // Get list of failed nodes for recovery
    size_t getFailedNodes(TorusCoord* out, size_t max) const {
        size_t count = 0;
        for (size_t i = 0; i < node_count_ && count < max; i++) {
            if (nodes_[i].state == NodeState::FAILED) {
                out[count++] = nodes_[i].coord;
            }
        }
        return count;
    }
    
private:
    static uint64_t rdtsc() {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
};

} // namespace distributed
