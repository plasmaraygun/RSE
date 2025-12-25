#pragma once
/**
 * Real TCP Socket Implementation for ARQON P2P Network
 * 
 * Uses POSIX sockets for actual network I/O.
 * No simulation, no stubs - real TCP connections.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <cerrno>

#include <chrono>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace network {

// Socket error codes
enum class SocketError {
    NONE = 0,
    WOULD_BLOCK,
    CONNECTION_REFUSED,
    CONNECTION_RESET,
    TIMEOUT,
    HOST_UNREACHABLE,
    INVALID_ADDRESS,
    BIND_FAILED,
    LISTEN_FAILED,
    ACCEPT_FAILED,
    SEND_FAILED,
    RECV_FAILED,
    CLOSED
};

inline const char* socketErrorString(SocketError err) {
    switch (err) {
        case SocketError::NONE: return "No error";
        case SocketError::WOULD_BLOCK: return "Would block";
        case SocketError::CONNECTION_REFUSED: return "Connection refused";
        case SocketError::CONNECTION_RESET: return "Connection reset";
        case SocketError::TIMEOUT: return "Timeout";
        case SocketError::HOST_UNREACHABLE: return "Host unreachable";
        case SocketError::INVALID_ADDRESS: return "Invalid address";
        case SocketError::BIND_FAILED: return "Bind failed";
        case SocketError::LISTEN_FAILED: return "Listen failed";
        case SocketError::ACCEPT_FAILED: return "Accept failed";
        case SocketError::SEND_FAILED: return "Send failed";
        case SocketError::RECV_FAILED: return "Recv failed";
        case SocketError::CLOSED: return "Socket closed";
        default: return "Unknown error";
    }
}

/**
 * TCP Socket wrapper for real network I/O
 */
class TcpSocket {
private:
    int fd_;
    bool connected_;
    bool listening_;
    SocketError last_error_;
    
    void setNonBlocking(bool nonblocking) {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0) return;
        
        if (nonblocking) {
            fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        } else {
            fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
    
    void setTcpNoDelay(bool nodelay) {
        int flag = nodelay ? 1 : 0;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    }
    
    void setReuseAddr(bool reuse) {
        int flag = reuse ? 1 : 0;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    }
    
    SocketError translateErrno() {
        switch (errno) {
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                return SocketError::WOULD_BLOCK;
            case ECONNREFUSED:
                return SocketError::CONNECTION_REFUSED;
            case ECONNRESET:
            case EPIPE:
                return SocketError::CONNECTION_RESET;
            case ETIMEDOUT:
                return SocketError::TIMEOUT;
            case EHOSTUNREACH:
            case ENETUNREACH:
                return SocketError::HOST_UNREACHABLE;
            default:
                return SocketError::SEND_FAILED;
        }
    }

public:
    TcpSocket() : fd_(-1), connected_(false), listening_(false), last_error_(SocketError::NONE) {}
    
    TcpSocket(int fd) : fd_(fd), connected_(fd >= 0), listening_(false), last_error_(SocketError::NONE) {
        if (fd_ >= 0) {
            setNonBlocking(true);
            setTcpNoDelay(true);
        }
    }
    
    ~TcpSocket() {
        close();
    }
    
    // Move only, no copy
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    
    TcpSocket(TcpSocket&& other) noexcept 
        : fd_(other.fd_), connected_(other.connected_), 
          listening_(other.listening_), last_error_(other.last_error_) {
        other.fd_ = -1;
        other.connected_ = false;
        other.listening_ = false;
    }
    
    TcpSocket& operator=(TcpSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            connected_ = other.connected_;
            listening_ = other.listening_;
            last_error_ = other.last_error_;
            other.fd_ = -1;
            other.connected_ = false;
            other.listening_ = false;
        }
        return *this;
    }
    
    /**
     * Create socket
     */
    bool create() {
        if (fd_ >= 0) return true;
        
        fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd_ < 0) {
            last_error_ = SocketError::BIND_FAILED;
            return false;
        }
        
        setNonBlocking(true);
        setTcpNoDelay(true);
        setReuseAddr(true);
        
        return true;
    }
    
    /**
     * Bind to address and port
     */
    bool bind(uint16_t port, uint32_t ip = INADDR_ANY) {
        if (fd_ < 0 && !create()) return false;
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(ip);
        
        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            last_error_ = SocketError::BIND_FAILED;
            return false;
        }
        
        return true;
    }
    
    /**
     * Listen for connections
     */
    bool listen(int backlog = 128) {
        if (fd_ < 0) return false;
        
        if (::listen(fd_, backlog) < 0) {
            last_error_ = SocketError::LISTEN_FAILED;
            return false;
        }
        
        listening_ = true;
        return true;
    }
    
    /**
     * Accept incoming connection
     * Returns new socket for the connection, or invalid socket if no pending connection
     */
    TcpSocket accept(uint32_t& client_ip, uint16_t& client_port) {
        if (!listening_) return TcpSocket();
        
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                last_error_ = SocketError::WOULD_BLOCK;
            } else {
                last_error_ = SocketError::ACCEPT_FAILED;
            }
            return TcpSocket();
        }
        
        client_ip = ntohl(client_addr.sin_addr.s_addr);
        client_port = ntohs(client_addr.sin_port);
        
        return TcpSocket(client_fd);
    }
    
    /**
     * Connect to remote host
     */
    bool connect(uint32_t ip, uint16_t port, int timeout_ms = 5000) {
        if (fd_ < 0 && !create()) return false;
        
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(ip);
        
        int result = ::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        
        if (result < 0) {
            if (errno == EINPROGRESS) {
                // Non-blocking connect in progress, wait for completion
                struct pollfd pfd{};
                pfd.fd = fd_;
                pfd.events = POLLOUT;
                
                result = poll(&pfd, 1, timeout_ms);
                
                if (result <= 0) {
                    last_error_ = SocketError::TIMEOUT;
                    return false;
                }
                
                // Check if connection succeeded
                int error = 0;
                socklen_t len = sizeof(error);
                getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len);
                
                if (error != 0) {
                    errno = error;
                    last_error_ = translateErrno();
                    return false;
                }
            } else {
                last_error_ = translateErrno();
                return false;
            }
        }
        
        connected_ = true;
        return true;
    }
    
    /**
     * Connect using string IP address
     */
    bool connect(const std::string& ip_str, uint16_t port, int timeout_ms = 5000) {
        struct in_addr addr;
        if (inet_pton(AF_INET, ip_str.c_str(), &addr) != 1) {
            last_error_ = SocketError::INVALID_ADDRESS;
            return false;
        }
        return connect(ntohl(addr.s_addr), port, timeout_ms);
    }
    
    /**
     * Send data
     * Returns bytes sent, or -1 on error
     */
    ssize_t send(const void* data, size_t len) {
        if (!connected_ && !listening_) return -1;
        
        ssize_t sent = ::send(fd_, data, len, MSG_NOSIGNAL);
        
        if (sent < 0) {
            last_error_ = translateErrno();
            if (last_error_ == SocketError::CONNECTION_RESET) {
                connected_ = false;
            }
            return -1;
        }
        
        return sent;
    }
    
    /**
     * Send all data (blocking until complete or error)
     */
    bool sendAll(const void* data, size_t len) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        size_t remaining = len;
        
        while (remaining > 0) {
            ssize_t sent = send(ptr, remaining);
            
            if (sent < 0) {
                if (last_error_ == SocketError::WOULD_BLOCK) {
                    // Wait for socket to be writable
                    struct pollfd pfd{};
                    pfd.fd = fd_;
                    pfd.events = POLLOUT;
                    poll(&pfd, 1, 1000);
                    continue;
                }
                return false;
            }
            
            ptr += sent;
            remaining -= sent;
        }
        
        return true;
    }
    
    /**
     * Receive data
     * Returns bytes received, 0 on connection closed, -1 on error
     */
    ssize_t recv(void* buffer, size_t len) {
        if (!connected_) return -1;
        
        ssize_t received = ::recv(fd_, buffer, len, 0);
        
        if (received < 0) {
            last_error_ = translateErrno();
            return -1;
        }
        
        if (received == 0) {
            connected_ = false;
            last_error_ = SocketError::CLOSED;
            return 0;
        }
        
        return received;
    }
    
    /**
     * Receive exact amount of data (blocking until complete or error)
     */
    bool recvAll(void* buffer, size_t len, int timeout_ms = 30000) {
        uint8_t* ptr = static_cast<uint8_t*>(buffer);
        size_t remaining = len;
        
        auto start = std::chrono::steady_clock::now();
        
        while (remaining > 0) {
            ssize_t received = recv(ptr, remaining);
            
            if (received < 0) {
                if (last_error_ == SocketError::WOULD_BLOCK) {
                    // Check timeout
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                    if (elapsed >= timeout_ms) {
                        last_error_ = SocketError::TIMEOUT;
                        return false;
                    }
                    
                    // Wait for data
                    struct pollfd pfd{};
                    pfd.fd = fd_;
                    pfd.events = POLLIN;
                    poll(&pfd, 1, 100);
                    continue;
                }
                return false;
            }
            
            if (received == 0) {
                return false;  // Connection closed
            }
            
            ptr += received;
            remaining -= received;
        }
        
        return true;
    }
    
    /**
     * Check if data is available to read
     */
    bool hasData(int timeout_ms = 0) {
        if (fd_ < 0) return false;
        
        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;
        
        int result = poll(&pfd, 1, timeout_ms);
        return result > 0 && (pfd.revents & POLLIN);
    }
    
    /**
     * Close socket
     */
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        connected_ = false;
        listening_ = false;
    }
    
    // Accessors
    bool isValid() const { return fd_ >= 0; }
    bool isConnected() const { return connected_; }
    bool isListening() const { return listening_; }
    int getFd() const { return fd_; }
    SocketError getLastError() const { return last_error_; }
    
    /**
     * Get local address
     */
    bool getLocalAddr(uint32_t& ip, uint16_t& port) const {
        if (fd_ < 0) return false;
        
        struct sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        
        if (getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
            return false;
        }
        
        ip = ntohl(addr.sin_addr.s_addr);
        port = ntohs(addr.sin_port);
        return true;
    }
    
    /**
     * Get remote address
     */
    bool getRemoteAddr(uint32_t& ip, uint16_t& port) const {
        if (fd_ < 0) return false;
        
        struct sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        
        if (getpeername(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
            return false;
        }
        
        ip = ntohl(addr.sin_addr.s_addr);
        port = ntohs(addr.sin_port);
        return true;
    }
};

/**
 * IP address utilities
 */
class IpAddress {
public:
    static uint32_t fromString(const std::string& str) {
        struct in_addr addr;
        if (inet_pton(AF_INET, str.c_str(), &addr) != 1) {
            return 0;
        }
        return ntohl(addr.s_addr);
    }
    
    static std::string toString(uint32_t ip) {
        struct in_addr addr;
        addr.s_addr = htonl(ip);
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, buf, sizeof(buf));
        return std::string(buf);
    }
    
    static constexpr uint32_t LOCALHOST = 0x7F000001;  // 127.0.0.1
    static constexpr uint32_t ANY = 0;                  // 0.0.0.0
};

} // namespace network
