#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <cstring>
#include <iostream>
#include <memory>

// POSIX sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

/**
 * Production-Grade TCP/UDP Networking
 * 
 * Real socket implementation using POSIX APIs.
 */

namespace network_real {

// ============================================================================
// Socket Utilities
// ============================================================================

class SocketUtil {
public:
    // Set socket to non-blocking mode
    static bool setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
    }
    
    // Set socket reuse address
    static bool setReuseAddr(int fd) {
        int opt = 1;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) >= 0;
    }
    
    // Set TCP no delay (disable Nagle's algorithm)
    static bool setTcpNoDelay(int fd) {
        int opt = 1;
        return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) >= 0;
    }
    
    // Set socket timeout
    static bool setTimeout(int fd, int seconds) {
        struct timeval tv;
        tv.tv_sec = seconds;
        tv.tv_usec = 0;
        return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) >= 0 &&
               setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) >= 0;
    }
};

// ============================================================================
// TCP Connection
// ============================================================================

class TCPConnection {
private:
    int fd_;
    std::string remote_ip_;
    uint16_t remote_port_;
    bool connected_;
    
    std::mutex send_mutex_;
    std::mutex recv_mutex_;
    
public:
    TCPConnection() : fd_(-1), remote_port_(0), connected_(false) {}
    
    TCPConnection(int fd, const std::string& ip, uint16_t port)
        : fd_(fd), remote_ip_(ip), remote_port_(port), connected_(true) {
        SocketUtil::setNonBlocking(fd_);
        SocketUtil::setTcpNoDelay(fd_);
    }
    
    ~TCPConnection() {
        close();
    }
    
    // Connect to remote host
    bool connect(const std::string& ip, uint16_t port) {
        if (connected_) return false;
        
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            std::cerr << "[TCP] Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        SocketUtil::setNonBlocking(fd_);
        SocketUtil::setTcpNoDelay(fd_);
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "[TCP] Invalid IP address: " << ip << std::endl;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        
        int result = ::connect(fd_, (struct sockaddr*)&addr, sizeof(addr));
        
        if (result < 0 && errno != EINPROGRESS) {
            std::cerr << "[TCP] Connect failed: " << strerror(errno) << std::endl;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        
        // Wait for connection with poll
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLOUT;
        
        int poll_result = poll(&pfd, 1, 5000);  // 5 second timeout
        
        if (poll_result <= 0) {
            std::cerr << "[TCP] Connection timeout" << std::endl;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        
        // Check if connection succeeded
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            std::cerr << "[TCP] Connection failed: " << strerror(error) << std::endl;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        
        remote_ip_ = ip;
        remote_port_ = port;
        connected_ = true;
        
        return true;
    }
    
    // Send data
    ssize_t send(const uint8_t* data, size_t len) {
        if (!connected_) return -1;
        
        std::lock_guard<std::mutex> lock(send_mutex_);
        
        ssize_t total_sent = 0;
        while (total_sent < static_cast<ssize_t>(len)) {
            ssize_t sent = ::send(fd_, data + total_sent, len - total_sent, MSG_NOSIGNAL);
            
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Wait for socket to be writable
                    struct pollfd pfd;
                    pfd.fd = fd_;
                    pfd.events = POLLOUT;
                    
                    if (poll(&pfd, 1, 1000) <= 0) {
                        return -1;  // Timeout or error
                    }
                    continue;
                }
                
                std::cerr << "[TCP] Send failed: " << strerror(errno) << std::endl;
                return -1;
            }
            
            total_sent += sent;
        }
        
        return total_sent;
    }
    
    // Receive data
    ssize_t recv(uint8_t* buffer, size_t len) {
        if (!connected_) return -1;
        
        std::lock_guard<std::mutex> lock(recv_mutex_);
        
        ssize_t received = ::recv(fd_, buffer, len, 0);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;  // No data available
            }
            
            std::cerr << "[TCP] Recv failed: " << strerror(errno) << std::endl;
            return -1;
        }
        
        if (received == 0) {
            // Connection closed
            connected_ = false;
            return -1;
        }
        
        return received;
    }
    
    // Close connection
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        connected_ = false;
    }
    
    bool isConnected() const { return connected_; }
    int getFd() const { return fd_; }
    const std::string& getRemoteIP() const { return remote_ip_; }
    uint16_t getRemotePort() const { return remote_port_; }
};

// ============================================================================
// TCP Server
// ============================================================================

class TCPServer {
private:
    int listen_fd_;
    uint16_t port_;
    bool running_;
    
    std::thread accept_thread_;
    std::function<void(TCPConnection*)> connection_handler_;
    
    void acceptLoop() {
        while (running_) {
            struct pollfd pfd;
            pfd.fd = listen_fd_;
            pfd.events = POLLIN;
            
            int poll_result = poll(&pfd, 1, 100);  // 100ms timeout
            
            if (poll_result <= 0) continue;
            
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &addr_len);
            
            if (client_fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "[TCPServer] Accept failed: " << strerror(errno) << std::endl;
                }
                continue;
            }
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            uint16_t client_port = ntohs(client_addr.sin_port);
            
            std::cout << "[TCPServer] New connection from " << ip_str << ":" << client_port << std::endl;
            
            auto conn = std::make_unique<TCPConnection>(client_fd, ip_str, client_port);
            
            if (connection_handler_) {
                connection_handler_(conn.release());  // Handler takes ownership
            }
            // If no handler, unique_ptr automatically cleans up
        }
    }
    
public:
    TCPServer() : listen_fd_(-1), port_(0), running_(false) {}
    
    ~TCPServer() {
        stop();
    }
    
    bool start(uint16_t port, std::function<void(TCPConnection*)> handler) {
        if (running_) return false;
        
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "[TCPServer] Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        SocketUtil::setReuseAddr(listen_fd_);
        SocketUtil::setNonBlocking(listen_fd_);
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[TCPServer] Bind failed: " << strerror(errno) << std::endl;
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        
        if (listen(listen_fd_, 128) < 0) {
            std::cerr << "[TCPServer] Listen failed: " << strerror(errno) << std::endl;
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        
        port_ = port;
        connection_handler_ = handler;
        running_ = true;
        
        accept_thread_ = std::thread(&TCPServer::acceptLoop, this);
        
        std::cout << "[TCPServer] Listening on port " << port << std::endl;
        
        return true;
    }
    
    void stop() {
        if (!running_) return;
        
        running_ = false;
        
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
        
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        
        std::cout << "[TCPServer] Stopped" << std::endl;
    }
    
    bool isRunning() const { return running_; }
    uint16_t getPort() const { return port_; }
};

// ============================================================================
// UDP Socket
// ============================================================================

class UDPSocket {
private:
    int fd_;
    uint16_t port_;
    bool bound_;
    
public:
    UDPSocket() : fd_(-1), port_(0), bound_(false) {}
    
    ~UDPSocket() {
        close();
    }
    
    bool bind(uint16_t port) {
        if (bound_) return false;
        
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) {
            std::cerr << "[UDP] Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        SocketUtil::setNonBlocking(fd_);
        SocketUtil::setReuseAddr(fd_);
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (::bind(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[UDP] Bind failed: " << strerror(errno) << std::endl;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        
        port_ = port;
        bound_ = true;
        
        return true;
    }
    
    ssize_t sendTo(const uint8_t* data, size_t len, const std::string& ip, uint16_t port) {
        if (!bound_) return -1;
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            return -1;
        }
        
        return sendto(fd_, data, len, 0, (struct sockaddr*)&addr, sizeof(addr));
    }
    
    ssize_t recvFrom(uint8_t* buffer, size_t len, std::string& from_ip, uint16_t& from_port) {
        if (!bound_) return -1;
        
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        
        ssize_t received = recvfrom(fd_, buffer, len, 0, (struct sockaddr*)&addr, &addr_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;  // No data available
            }
            return -1;
        }
        
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
        from_ip = ip_str;
        from_port = ntohs(addr.sin_port);
        
        return received;
    }
    
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        bound_ = false;
    }
    
    bool isBound() const { return bound_; }
    int getFd() const { return fd_; }
    uint16_t getPort() const { return port_; }
};

} // namespace network_real
