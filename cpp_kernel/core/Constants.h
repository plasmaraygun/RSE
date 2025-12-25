#pragma once
/**
 * System-wide constants for ARQON
 * Eliminates magic numbers throughout the codebase
 */

#include <cstdint>
#include <cstddef>

namespace arqon {

// ============================================================================
// Buffer Sizes
// ============================================================================
constexpr size_t RECV_BUFFER_SIZE = 65536;
constexpr size_t SMALL_BUFFER_SIZE = 8192;
constexpr size_t FORMAT_BUFFER_SIZE = 1024;
constexpr size_t PATH_BUFFER_SIZE = 256;

// ============================================================================
// Network Constants
// ============================================================================
constexpr uint16_t DEFAULT_P2P_PORT = 31330;
constexpr uint16_t DEFAULT_API_PORT = 8080;
constexpr uint16_t DEFAULT_PETALS_PORT = 8765;
constexpr uint16_t DEFAULT_BOOTSTRAP_PORT = 8334;

constexpr int SOCKET_BACKLOG = 128;
constexpr int CONNECT_TIMEOUT_MS = 5000;
constexpr int RECV_TIMEOUT_MS = 10000;

// ============================================================================
// Time Constants
// ============================================================================
constexpr uint64_t MS_PER_SECOND = 1000ULL;
constexpr uint64_t US_PER_MS = 1000ULL;
constexpr uint64_t NS_PER_US = 1000ULL;
constexpr uint64_t NS_PER_MS = 1000000ULL;
constexpr uint64_t NS_PER_SECOND = 1000000000ULL;

constexpr uint64_t SECONDS_PER_MINUTE = 60ULL;
constexpr uint64_t SECONDS_PER_HOUR = 3600ULL;
constexpr uint64_t SECONDS_PER_DAY = 86400ULL;

// ============================================================================
// Memory Constants
// ============================================================================
constexpr size_t PAGE_SIZE = 4096;
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t KB = 1024;
constexpr size_t MB = 1024 * KB;
constexpr size_t GB = 1024 * MB;

// ============================================================================
// Retry/Polling Constants
// ============================================================================
constexpr int DEFAULT_MAX_RETRIES = 3;
constexpr int POLL_INTERVAL_MS = 10;
constexpr int BUSY_WAIT_ITERATIONS = 1000;

// ============================================================================
// Torus Constants
// ============================================================================
constexpr uint32_t TORUS_DIMENSION = 32;
constexpr uint32_t TORUS_SIZE = TORUS_DIMENSION * TORUS_DIMENSION * TORUS_DIMENSION;
constexpr uint32_t TORUS_COORD_MULTIPLIER_X = 1024;
constexpr uint32_t TORUS_COORD_MULTIPLIER_Y = 32;

// ============================================================================
// Economic Constants (already in Economics.h but duplicated for convenience)
// ============================================================================
constexpr uint64_t QUANTA_PER_ARQON = 1000000000ULL;  // 10^9

} // namespace arqon
