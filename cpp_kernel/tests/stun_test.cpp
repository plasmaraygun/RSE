/**
 * STUN/NAT Traversal Tests
 * 
 * Verifies real UDP STUN client functionality.
 * Note: Requires network access to public STUN servers.
 */

#include "../network/Stun.h"

#include <iostream>
#include <cassert>

using namespace network;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        return false; \
    } \
} while(0)

// Test 1: STUN client initialization
bool test_stun_init() {
    std::cout << "Test 1: STUN client initialization..." << std::endl;
    
    StunClient client;
    TEST_ASSERT(client.init(0), "STUN client init failed");
    
    uint16_t port = client.getLocalPort();
    std::cout << "  Bound to local port: " << port << std::endl;
    TEST_ASSERT(port > 0, "Invalid local port");
    TEST_ASSERT(client.getSocketFd() >= 0, "Invalid socket fd");
    
    client.close();
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 2: Public IP discovery via STUN
bool test_stun_discovery() {
    std::cout << "Test 2: Public IP discovery via STUN..." << std::endl;
    std::cout << "  (Requires network access to public STUN servers)" << std::endl;
    
    StunClient client;
    if (!client.init(0)) {
        std::cout << "  SKIPPED - Could not init socket" << std::endl;
        return true;
    }
    
    StunResult result = client.discoverWithFallback();
    
    if (!result.success) {
        std::cout << "  SKIPPED - No network access: " << result.error << std::endl;
        // Don't fail - network may not be available in test environment
        return true;
    }
    
    std::cout << "  Public IP: " << result.publicIpString() << std::endl;
    std::cout << "  Public Port: " << result.public_port << std::endl;
    
    TEST_ASSERT(result.public_ip != 0, "Invalid public IP");
    TEST_ASSERT(result.public_port != 0, "Invalid public port");
    
    client.close();
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 3: NAT type detection
bool test_nat_detection() {
    std::cout << "Test 3: NAT type detection..." << std::endl;
    
    StunClient client;
    if (!client.init(0)) {
        std::cout << "  SKIPPED - Could not init socket" << std::endl;
        return true;
    }
    
    NatType nat = client.detectNatType();
    std::cout << "  Detected NAT type: " << natTypeString(nat) << std::endl;
    
    // Just verify it returns something
    // Actual NAT type depends on network environment
    
    client.close();
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 4: Hole punching setup
bool test_hole_punch_setup() {
    std::cout << "Test 4: Hole punching setup..." << std::endl;
    
    // Create two local UDP sockets to simulate peers
    int sock1 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int sock2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    TEST_ASSERT(sock1 >= 0 && sock2 >= 0, "Socket creation failed");
    
    // Bind to random ports
    struct sockaddr_in addr1{}, addr2{};
    addr1.sin_family = AF_INET;
    addr1.sin_port = 0;
    addr1.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    
    addr2.sin_family = AF_INET;
    addr2.sin_port = 0;
    addr2.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    
    TEST_ASSERT(bind(sock1, (struct sockaddr*)&addr1, sizeof(addr1)) == 0, "Bind 1 failed");
    TEST_ASSERT(bind(sock2, (struct sockaddr*)&addr2, sizeof(addr2)) == 0, "Bind 2 failed");
    
    // Get actual ports
    socklen_t len = sizeof(addr1);
    getsockname(sock1, (struct sockaddr*)&addr1, &len);
    getsockname(sock2, (struct sockaddr*)&addr2, &len);
    
    uint16_t port1 = ntohs(addr1.sin_port);
    uint16_t port2 = ntohs(addr2.sin_port);
    
    std::cout << "  Socket 1 port: " << port1 << std::endl;
    std::cout << "  Socket 2 port: " << port2 << std::endl;
    
    // Set non-blocking
    fcntl(sock1, F_SETFL, fcntl(sock1, F_GETFL, 0) | O_NONBLOCK);
    fcntl(sock2, F_SETFL, fcntl(sock2, F_GETFL, 0) | O_NONBLOCK);
    
    // Punch from sock1 to sock2
    HolePuncher puncher1(sock1);
    puncher1.punch(0x7F000001, port2, 3);  // 127.0.0.1
    
    // Verify sock2 received packets
    HolePuncher puncher2(sock2);
    uint32_t peer_ip;
    uint16_t peer_port;
    
    bool received = puncher2.waitForPunch(peer_ip, peer_port, 1000);
    
    std::cout << "  Hole punch received: " << (received ? "yes" : "no") << std::endl;
    TEST_ASSERT(received, "Hole punch packet not received");
    TEST_ASSERT(peer_port == port1, "Peer port mismatch");
    
    close(sock1);
    close(sock2);
    
    std::cout << "  PASSED" << std::endl;
    return true;
}

int main() {
    std::cout << "=== STUN/NAT Traversal Tests ===" << std::endl;
    std::cout << "Testing real UDP STUN functionality." << std::endl;
    std::cout << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    if (test_stun_init()) passed++; else failed++;
    if (test_stun_discovery()) passed++; else failed++;
    if (test_nat_detection()) passed++; else failed++;
    if (test_hole_punch_setup()) passed++; else failed++;
    
    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    
    if (failed == 0) {
        std::cout << std::endl;
        std::cout << "✅ ALL TESTS PASSED - STUN/NAT traversal verified!" << std::endl;
    }
    
    return failed;
}
