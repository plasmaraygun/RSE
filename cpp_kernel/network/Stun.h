#pragma once
/**
 * STUN Client Implementation for NAT Traversal
 * 
 * RFC 5389 compliant STUN client for discovering public IP/port.
 * Real UDP socket implementation - no simulation.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <random>
#include <chrono>
#include <iostream>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <thread>

namespace network {

// STUN message types (RFC 5389)
constexpr uint16_t STUN_BINDING_REQUEST = 0x0001;
constexpr uint16_t STUN_BINDING_RESPONSE = 0x0101;
constexpr uint16_t STUN_BINDING_ERROR = 0x0111;

// STUN attribute types
constexpr uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;
constexpr uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;
constexpr uint16_t STUN_ATTR_ERROR_CODE = 0x0009;
constexpr uint16_t STUN_ATTR_SOFTWARE = 0x8022;
constexpr uint16_t STUN_ATTR_FINGERPRINT = 0x8028;

// STUN magic cookie (RFC 5389)
constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442;

// STUN header size
constexpr size_t STUN_HEADER_SIZE = 20;

// ============================================================================
// STUN Message Header
// ============================================================================

struct StunHeader {
    uint16_t type;
    uint16_t length;
    uint32_t magic_cookie;
    uint8_t transaction_id[12];
    
    void serialize(uint8_t* buf) const {
        uint16_t type_be = htons(type);
        uint16_t length_be = htons(length);
        uint32_t magic_be = htonl(magic_cookie);
        
        std::memcpy(buf, &type_be, 2);
        std::memcpy(buf + 2, &length_be, 2);
        std::memcpy(buf + 4, &magic_be, 4);
        std::memcpy(buf + 8, transaction_id, 12);
    }
    
    bool deserialize(const uint8_t* buf) {
        uint16_t type_be, length_be;
        uint32_t magic_be;
        
        std::memcpy(&type_be, buf, 2);
        std::memcpy(&length_be, buf + 2, 2);
        std::memcpy(&magic_be, buf + 4, 4);
        std::memcpy(transaction_id, buf + 8, 12);
        
        type = ntohs(type_be);
        length = ntohs(length_be);
        magic_cookie = ntohl(magic_be);
        
        return magic_cookie == STUN_MAGIC_COOKIE;
    }
};

// ============================================================================
// NAT Type Detection Result
// ============================================================================

enum class NatType {
    UNKNOWN,
    OPEN,                    // No NAT, public IP
    FULL_CONE,               // Any external host can send
    RESTRICTED_CONE,         // Only hosts we've sent to can reply
    PORT_RESTRICTED_CONE,    // Only host:port we've sent to can reply
    SYMMETRIC                // Different mapping for each destination
};

inline const char* natTypeString(NatType type) {
    switch (type) {
        case NatType::OPEN: return "Open Internet";
        case NatType::FULL_CONE: return "Full Cone NAT";
        case NatType::RESTRICTED_CONE: return "Restricted Cone NAT";
        case NatType::PORT_RESTRICTED_CONE: return "Port Restricted Cone NAT";
        case NatType::SYMMETRIC: return "Symmetric NAT";
        default: return "Unknown";
    }
}

// ============================================================================
// STUN Result
// ============================================================================

struct StunResult {
    bool success;
    uint32_t public_ip;
    uint16_t public_port;
    NatType nat_type;
    std::string error;
    
    StunResult() : success(false), public_ip(0), public_port(0), 
                   nat_type(NatType::UNKNOWN) {}
    
    std::string publicIpString() const {
        struct in_addr addr;
        addr.s_addr = htonl(public_ip);
        return inet_ntoa(addr);
    }
};

// ============================================================================
// STUN Client
// ============================================================================

class StunClient {
private:
    int socket_fd_;
    uint16_t local_port_;
    
    // Well-known public STUN servers
    static constexpr const char* STUN_SERVERS[] = {
        "stun.l.google.com",
        "stun1.l.google.com",
        "stun2.l.google.com",
        "stun.stunprotocol.org",
        "stun.voip.blackberry.com"
    };
    static constexpr uint16_t STUN_PORT = 19302;
    
    void generateTransactionId(uint8_t* id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
        
        for (int i = 0; i < 12; i += 4) {
            uint32_t r = dist(gen);
            std::memcpy(id + i, &r, 4);
        }
    }
    
    bool resolveHost(const char* hostname, uint32_t& ip) {
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        
        if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) {
            return false;
        }
        
        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        ip = ntohl(addr->sin_addr.s_addr);
        
        freeaddrinfo(result);
        return true;
    }
    
    bool sendBindingRequest(uint32_t server_ip, uint16_t server_port,
                            const uint8_t* transaction_id) {
        StunHeader header;
        header.type = STUN_BINDING_REQUEST;
        header.length = 0;  // No attributes
        header.magic_cookie = STUN_MAGIC_COOKIE;
        std::memcpy(header.transaction_id, transaction_id, 12);
        
        uint8_t buf[STUN_HEADER_SIZE];
        header.serialize(buf);
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(server_port);
        addr.sin_addr.s_addr = htonl(server_ip);
        
        ssize_t sent = sendto(socket_fd_, buf, STUN_HEADER_SIZE, 0,
                              reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        
        return sent == STUN_HEADER_SIZE;
    }
    
    bool receiveResponse(uint8_t* buf, size_t buf_size, size_t& received,
                         int timeout_ms = 3000) {
        struct pollfd pfd{};
        pfd.fd = socket_fd_;
        pfd.events = POLLIN;
        
        int result = poll(&pfd, 1, timeout_ms);
        if (result <= 0) {
            return false;  // Timeout or error
        }
        
        struct sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        
        ssize_t n = recvfrom(socket_fd_, buf, buf_size, 0,
                             reinterpret_cast<struct sockaddr*>(&from), &from_len);
        
        if (n <= 0) {
            return false;
        }
        
        received = static_cast<size_t>(n);
        return true;
    }
    
    bool parseXorMappedAddress(const uint8_t* attr_data, size_t attr_len,
                                uint32_t& ip, uint16_t& port,
                                const uint8_t* transaction_id) {
        if (attr_len < 8) return false;
        
        // Skip reserved byte, check family
        uint8_t family = attr_data[1];
        if (family != 0x01) return false;  // IPv4 only
        
        // XOR port with magic cookie high 16 bits
        uint16_t xor_port;
        std::memcpy(&xor_port, attr_data + 2, 2);
        port = ntohs(xor_port) ^ (STUN_MAGIC_COOKIE >> 16);
        
        // XOR IP with magic cookie
        uint32_t xor_ip;
        std::memcpy(&xor_ip, attr_data + 4, 4);
        ip = ntohl(xor_ip) ^ STUN_MAGIC_COOKIE;
        
        return true;
    }
    
    bool parseMappedAddress(const uint8_t* attr_data, size_t attr_len,
                            uint32_t& ip, uint16_t& port) {
        if (attr_len < 8) return false;
        
        uint8_t family = attr_data[1];
        if (family != 0x01) return false;  // IPv4 only
        
        uint16_t port_be;
        uint32_t ip_be;
        std::memcpy(&port_be, attr_data + 2, 2);
        std::memcpy(&ip_be, attr_data + 4, 4);
        
        port = ntohs(port_be);
        ip = ntohl(ip_be);
        
        return true;
    }

public:
    StunClient() : socket_fd_(-1), local_port_(0) {}
    
    ~StunClient() {
        close();
    }
    
    /**
     * Initialize STUN client with UDP socket
     */
    bool init(uint16_t local_port = 0) {
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_fd_ < 0) {
            return false;
        }
        
        // Set non-blocking
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
        
        // Bind to local port
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(local_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Get actual bound port
        socklen_t addr_len = sizeof(addr);
        getsockname(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
        local_port_ = ntohs(addr.sin_port);
        
        return true;
    }
    
    void close() {
        if (socket_fd_ >= 0) {
            ::close(socket_fd_);
            socket_fd_ = -1;
        }
    }
    
    /**
     * Discover public IP and port using STUN
     */
    StunResult discover(const char* stun_server = nullptr, uint16_t stun_port = STUN_PORT) {
        StunResult result;
        
        if (socket_fd_ < 0) {
            result.error = "Socket not initialized";
            return result;
        }
        
        // Resolve STUN server
        uint32_t server_ip = 0;
        const char* server = stun_server ? stun_server : STUN_SERVERS[0];
        
        if (!resolveHost(server, server_ip)) {
            result.error = "Failed to resolve STUN server: " + std::string(server);
            return result;
        }
        
        // Generate transaction ID
        uint8_t transaction_id[12];
        generateTransactionId(transaction_id);
        
        // Send binding request
        if (!sendBindingRequest(server_ip, stun_port, transaction_id)) {
            result.error = "Failed to send STUN request";
            return result;
        }
        
        // Receive response
        uint8_t response[512];
        size_t response_len;
        
        if (!receiveResponse(response, sizeof(response), response_len)) {
            result.error = "STUN request timed out";
            return result;
        }
        
        // Parse header
        if (response_len < STUN_HEADER_SIZE) {
            result.error = "Response too short";
            return result;
        }
        
        StunHeader header;
        if (!header.deserialize(response)) {
            result.error = "Invalid STUN response";
            return result;
        }
        
        // Verify transaction ID
        if (std::memcmp(header.transaction_id, transaction_id, 12) != 0) {
            result.error = "Transaction ID mismatch";
            return result;
        }
        
        // Check response type
        if (header.type == STUN_BINDING_ERROR) {
            result.error = "STUN binding error";
            return result;
        }
        
        if (header.type != STUN_BINDING_RESPONSE) {
            result.error = "Unexpected response type";
            return result;
        }
        
        // Parse attributes
        size_t pos = STUN_HEADER_SIZE;
        bool found_address = false;
        
        while (pos + 4 <= response_len) {
            uint16_t attr_type_be, attr_len_be;
            std::memcpy(&attr_type_be, response + pos, 2);
            std::memcpy(&attr_len_be, response + pos + 2, 2);
            
            uint16_t attr_type = ntohs(attr_type_be);
            uint16_t attr_len = ntohs(attr_len_be);
            
            pos += 4;
            
            if (pos + attr_len > response_len) break;
            
            if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                if (parseXorMappedAddress(response + pos, attr_len,
                                          result.public_ip, result.public_port,
                                          transaction_id)) {
                    found_address = true;
                }
            } else if (attr_type == STUN_ATTR_MAPPED_ADDRESS && !found_address) {
                if (parseMappedAddress(response + pos, attr_len,
                                       result.public_ip, result.public_port)) {
                    found_address = true;
                }
            }
            
            // Align to 4 bytes
            pos += attr_len;
            if (attr_len % 4 != 0) {
                pos += 4 - (attr_len % 4);
            }
        }
        
        if (!found_address) {
            result.error = "No mapped address in response";
            return result;
        }
        
        result.success = true;
        result.nat_type = NatType::UNKNOWN;  // Would need multiple tests to determine
        
        return result;
    }
    
    /**
     * Discover using multiple STUN servers for reliability
     */
    StunResult discoverWithFallback() {
        for (const char* server : STUN_SERVERS) {
            std::cout << "[STUN] Trying " << server << "..." << std::endl;
            
            StunResult result = discover(server, STUN_PORT);
            if (result.success) {
                std::cout << "[STUN] Success! Public endpoint: " 
                          << result.publicIpString() << ":" << result.public_port << std::endl;
                return result;
            }
            
            std::cout << "[STUN] Failed: " << result.error << std::endl;
        }
        
        StunResult failed;
        failed.error = "All STUN servers failed";
        return failed;
    }
    
    /**
     * Detect NAT type (requires multiple STUN tests)
     */
    NatType detectNatType() {
        // Test 1: Basic STUN request
        StunResult result1 = discoverWithFallback();
        if (!result1.success) {
            return NatType::UNKNOWN;
        }
        
        // Compare local and public addresses
        struct sockaddr_in local_addr{};
        socklen_t addr_len = sizeof(local_addr);
        getsockname(socket_fd_, reinterpret_cast<struct sockaddr*>(&local_addr), &addr_len);
        
        uint32_t local_ip = ntohl(local_addr.sin_addr.s_addr);
        
        // If public IP matches local IP, we're on open internet
        if (result1.public_ip == local_ip && result1.public_port == local_port_) {
            return NatType::OPEN;
        }
        
        // For full NAT type detection, we'd need:
        // - Test with different STUN server (same port)
        // - Test with same server, different port
        // - Compare results to determine cone type vs symmetric
        
        // For now, return a reasonable default
        if (result1.public_port == local_port_) {
            return NatType::FULL_CONE;  // Port preserved
        }
        
        return NatType::PORT_RESTRICTED_CONE;  // Port changed
    }
    
    uint16_t getLocalPort() const { return local_port_; }
    int getSocketFd() const { return socket_fd_; }
};

// ============================================================================
// UDP Hole Punching Helper
// ============================================================================

class HolePuncher {
private:
    int socket_fd_;
    
public:
    HolePuncher(int socket_fd) : socket_fd_(socket_fd) {}
    
    /**
     * Punch a hole to allow incoming packets from target
     * Both peers must call this simultaneously
     */
    bool punch(uint32_t target_ip, uint16_t target_port, int attempts = 5) {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(target_port);
        addr.sin_addr.s_addr = htonl(target_ip);
        
        // Send multiple packets to punch through NAT
        uint8_t punch_packet[] = "ARQON_PUNCH";
        
        for (int i = 0; i < attempts; i++) {
            sendto(socket_fd_, punch_packet, sizeof(punch_packet), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "[HolePunch] Sent " << attempts << " packets to " 
                  << inet_ntoa(addr.sin_addr) << ":" << target_port << std::endl;
        
        return true;
    }
    
    /**
     * Wait for punch packet from peer
     */
    bool waitForPunch(uint32_t& peer_ip, uint16_t& peer_port, int timeout_ms = 5000) {
        struct pollfd pfd{};
        pfd.fd = socket_fd_;
        pfd.events = POLLIN;
        
        auto start = std::chrono::steady_clock::now();
        
        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            
            if (elapsed >= timeout_ms) {
                return false;
            }
            
            int remaining = timeout_ms - static_cast<int>(elapsed);
            int result = poll(&pfd, 1, remaining);
            
            if (result <= 0) continue;
            
            uint8_t buf[64];
            struct sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            
            ssize_t n = recvfrom(socket_fd_, buf, sizeof(buf), 0,
                                 reinterpret_cast<struct sockaddr*>(&from), &from_len);
            
            if (n > 0) {
                peer_ip = ntohl(from.sin_addr.s_addr);
                peer_port = ntohs(from.sin_port);
                
                std::cout << "[HolePunch] Received packet from " 
                          << inet_ntoa(from.sin_addr) << ":" << peer_port << std::endl;
                
                return true;
            }
        }
    }
};

} // namespace network
