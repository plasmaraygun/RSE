#pragma once

#include <cstdint>

namespace rse {

/**
 * ErrorCode: Standardized error codes for RSE operations.
 * 
 * Design principles:
 * - Zero means success
 * - Negative values are errors
 * - Positive values are warnings or info
 */
enum class ErrorCode : int32_t {
    // Success
    SUCCESS = 0,
    
    // Warnings (positive)
    WARN_CAPACITY_LOW = 1,
    WARN_TIME_DRIFT = 2,
    WARN_DEGRADED_MODE = 3,
    
    // Resource errors (-1xx)
    ERR_OUT_OF_MEMORY = -100,
    ERR_CAPACITY_EXCEEDED = -101,
    ERR_POOL_EXHAUSTED = -102,
    ERR_QUEUE_FULL = -103,
    
    // Validation errors (-2xx)
    ERR_INVALID_ARGUMENT = -200,
    ERR_INVALID_COORDINATE = -201,
    ERR_INVALID_POINTER = -202,
    ERR_HASH_MISMATCH = -203,
    ERR_PROJECTION_INVALID = -204,
    
    // State errors (-3xx)
    ERR_NOT_INITIALIZED = -300,
    ERR_ALREADY_INITIALIZED = -301,
    ERR_TORUS_FAILED = -302,
    ERR_RECONSTRUCTION_FAILED = -303,
    
    // Consistency errors (-4xx)
    ERR_CONSISTENCY_VIOLATION = -400,
    ERR_TIME_DIVERGENCE = -401,
    ERR_BOUNDARY_MISMATCH = -402,
    ERR_CONSTRAINT_VIOLATION = -403,
    
    // System errors (-5xx)
    ERR_THREAD_ERROR = -500,
    ERR_LOCK_FAILED = -501,
    ERR_TIMEOUT = -502,
};

inline const char* errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "SUCCESS";
        case ErrorCode::WARN_CAPACITY_LOW: return "WARN_CAPACITY_LOW";
        case ErrorCode::WARN_TIME_DRIFT: return "WARN_TIME_DRIFT";
        case ErrorCode::WARN_DEGRADED_MODE: return "WARN_DEGRADED_MODE";
        case ErrorCode::ERR_OUT_OF_MEMORY: return "ERR_OUT_OF_MEMORY";
        case ErrorCode::ERR_CAPACITY_EXCEEDED: return "ERR_CAPACITY_EXCEEDED";
        case ErrorCode::ERR_POOL_EXHAUSTED: return "ERR_POOL_EXHAUSTED";
        case ErrorCode::ERR_QUEUE_FULL: return "ERR_QUEUE_FULL";
        case ErrorCode::ERR_INVALID_ARGUMENT: return "ERR_INVALID_ARGUMENT";
        case ErrorCode::ERR_INVALID_COORDINATE: return "ERR_INVALID_COORDINATE";
        case ErrorCode::ERR_INVALID_POINTER: return "ERR_INVALID_POINTER";
        case ErrorCode::ERR_HASH_MISMATCH: return "ERR_HASH_MISMATCH";
        case ErrorCode::ERR_PROJECTION_INVALID: return "ERR_PROJECTION_INVALID";
        case ErrorCode::ERR_NOT_INITIALIZED: return "ERR_NOT_INITIALIZED";
        case ErrorCode::ERR_ALREADY_INITIALIZED: return "ERR_ALREADY_INITIALIZED";
        case ErrorCode::ERR_TORUS_FAILED: return "ERR_TORUS_FAILED";
        case ErrorCode::ERR_RECONSTRUCTION_FAILED: return "ERR_RECONSTRUCTION_FAILED";
        case ErrorCode::ERR_CONSISTENCY_VIOLATION: return "ERR_CONSISTENCY_VIOLATION";
        case ErrorCode::ERR_TIME_DIVERGENCE: return "ERR_TIME_DIVERGENCE";
        case ErrorCode::ERR_BOUNDARY_MISMATCH: return "ERR_BOUNDARY_MISMATCH";
        case ErrorCode::ERR_CONSTRAINT_VIOLATION: return "ERR_CONSTRAINT_VIOLATION";
        case ErrorCode::ERR_THREAD_ERROR: return "ERR_THREAD_ERROR";
        case ErrorCode::ERR_LOCK_FAILED: return "ERR_LOCK_FAILED";
        case ErrorCode::ERR_TIMEOUT: return "ERR_TIMEOUT";
        default: return "UNKNOWN_ERROR";
    }
}

inline bool isSuccess(ErrorCode code) { return code == ErrorCode::SUCCESS; }
inline bool isWarning(ErrorCode code) { return static_cast<int32_t>(code) > 0; }
inline bool isError(ErrorCode code) { return static_cast<int32_t>(code) < 0; }

} // namespace rse
