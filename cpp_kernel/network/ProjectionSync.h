#pragma once

#include "P2PNode.h"
#include "../braided/Projection.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>

/**
 * Projection Synchronization for Distributed Braiding
 * 
 * Synchronizes projections between tori over the P2P network.
 */

namespace network {

using namespace braided;

// ============================================================================
// Projection Message
// ============================================================================

struct ProjectionMessage {
    uint32_t torus_id;
    uint64_t timestamp;
    uint64_t current_time;
    uint64_t events_processed;
    uint32_t active_processes;
    
    // Boundary state (simplified - first 32 values)
    std::array<int, 32> boundary_sample;
    
    // Signature (for authenticity)
    crypto::Signature signature;
    
    ProjectionMessage() : torus_id(0), timestamp(0), current_time(0),
                          events_processed(0), active_processes(0) {
        boundary_sample.fill(0);
        signature.fill(0);
    }
    
    // Serialize to bytes
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.resize(sizeof(ProjectionMessage));
        
        size_t pos = 0;
        std::memcpy(data.data() + pos, &torus_id, 4); pos += 4;
        std::memcpy(data.data() + pos, &timestamp, 8); pos += 8;
        std::memcpy(data.data() + pos, &current_time, 8); pos += 8;
        std::memcpy(data.data() + pos, &events_processed, 8); pos += 8;
        std::memcpy(data.data() + pos, &active_processes, 4); pos += 4;
        std::memcpy(data.data() + pos, boundary_sample.data(), 32 * 4); pos += 32 * 4;
        std::memcpy(data.data() + pos, signature.data(), crypto::SIGNATURE_SIZE); pos += crypto::SIGNATURE_SIZE;
        
        return data;
    }
    
    // Deserialize from bytes
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(ProjectionMessage)) {
            return false;
        }
        
        size_t pos = 0;
        std::memcpy(&torus_id, data.data() + pos, 4); pos += 4;
        std::memcpy(&timestamp, data.data() + pos, 8); pos += 8;
        std::memcpy(&current_time, data.data() + pos, 8); pos += 8;
        std::memcpy(&events_processed, data.data() + pos, 8); pos += 8;
        std::memcpy(&active_processes, data.data() + pos, 4); pos += 4;
        std::memcpy(boundary_sample.data(), data.data() + pos, 32 * 4); pos += 32 * 4;
        std::memcpy(signature.data(), data.data() + pos, crypto::SIGNATURE_SIZE); pos += crypto::SIGNATURE_SIZE;
        
        return true;
    }
    
    // Convert from Projection
    static ProjectionMessage fromProjection(const Projection& proj, uint32_t torus_id) {
        ProjectionMessage msg;
        msg.torus_id = torus_id;
        msg.timestamp = proj.timestamp;
        msg.current_time = proj.current_time;
        msg.events_processed = proj.total_events_processed;
        msg.active_processes = proj.active_processes;
        
        // Sample boundary state (first 32 values)
        for (size_t i = 0; i < 32; i++) {
            msg.boundary_sample[i] = static_cast<int>(proj.boundary_states[i]);
        }
        
        return msg;
    }
    
    // Convert to Projection
    Projection toProjection() const {
        Projection proj;
        proj.torus_id = torus_id;
        proj.timestamp = timestamp;
        proj.current_time = current_time;
        proj.total_events_processed = events_processed;
        proj.active_processes = active_processes;
        
        // Restore boundary state (first 32 values)
        for (size_t i = 0; i < 32; i++) {
            proj.boundary_states[i] = static_cast<uint32_t>(boundary_sample[i]);
        }
        // Zero out the rest
        for (size_t i = 32; i < proj.boundary_states.size(); i++) {
            proj.boundary_states[i] = 0;
        }
        
        return proj;
    }
};

// ============================================================================
// Projection Synchronizer
// ============================================================================

class ProjectionSync {
private:
    P2PNode& node_;
    uint32_t local_torus_id_;
    
    // Received projections from other tori
    std::unordered_map<uint32_t, ProjectionMessage> remote_projections_;
    mutable std::mutex projections_mutex_;
    
    // Last broadcast time
    uint64_t last_broadcast_;
    uint64_t broadcast_interval_;  // milliseconds
    
public:
    ProjectionSync(P2PNode& node, uint32_t torus_id, uint64_t interval_ms = 1000)
        : node_(node), local_torus_id_(torus_id), 
          last_broadcast_(0), broadcast_interval_(interval_ms) {}
    
    // Broadcast local projection to network
    void broadcastProjection(const Projection& proj) {
        uint64_t now = std::chrono::system_clock::now().time_since_epoch().count();
        
        // Rate limiting
        if (now - last_broadcast_ < broadcast_interval_ * 1000000) {
            return;
        }
        
        ProjectionMessage msg = ProjectionMessage::fromProjection(proj, local_torus_id_);
        
        // Serialize and broadcast
        std::vector<uint8_t> data = msg.serialize();
        Message network_msg(MessageType::PROJECTION, data);
        
        node_.broadcast(network_msg);
        
        last_broadcast_ = now;
    }
    
    // Request projection from specific torus
    void requestProjection(uint32_t torus_id) {
        std::vector<uint8_t> data(4);
        std::memcpy(data.data(), &torus_id, 4);
        
        Message msg(MessageType::GETPROJECTION, data);
        node_.broadcast(msg);
    }
    
    // Process incoming projection message
    void processProjection(const Message& msg) {
        ProjectionMessage proj_msg;
        
        if (!proj_msg.deserialize(msg.payload)) {
            std::cerr << "[ProjectionSync] Failed to deserialize projection" << std::endl;
            return;
        }
        
        // Don't store our own projection
        if (proj_msg.torus_id == local_torus_id_) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(projections_mutex_);
        
        // Update stored projection
        remote_projections_[proj_msg.torus_id] = proj_msg;
        
        std::cout << "[ProjectionSync] Received projection from Torus " 
                  << proj_msg.torus_id << " (time=" << proj_msg.current_time << ")" << std::endl;
    }
    
    // Get projection from specific torus
    bool getProjection(uint32_t torus_id, Projection& proj) const {
        std::lock_guard<std::mutex> lock(projections_mutex_);
        
        auto it = remote_projections_.find(torus_id);
        if (it == remote_projections_.end()) {
            return false;
        }
        
        proj = it->second.toProjection();
        return true;
    }
    
    // Get all remote projections
    std::vector<Projection> getAllProjections() const {
        std::lock_guard<std::mutex> lock(projections_mutex_);
        
        std::vector<Projection> result;
        for (const auto& [id, msg] : remote_projections_) {
            result.push_back(msg.toProjection());
        }
        
        return result;
    }
    
    // Check if we have projections from all expected tori
    bool hasAllProjections(const std::vector<uint32_t>& expected_tori) const {
        std::lock_guard<std::mutex> lock(projections_mutex_);
        
        for (uint32_t id : expected_tori) {
            if (id == local_torus_id_) continue;  // Skip self
            
            if (remote_projections_.find(id) == remote_projections_.end()) {
                return false;
            }
        }
        
        return true;
    }
    
    // Clear old projections
    void clearOldProjections(uint64_t max_age_ms) {
        std::lock_guard<std::mutex> lock(projections_mutex_);
        
        uint64_t now = std::chrono::system_clock::now().time_since_epoch().count();
        uint64_t cutoff = now - (max_age_ms * 1000000);
        
        auto it = remote_projections_.begin();
        while (it != remote_projections_.end()) {
            if (it->second.timestamp < cutoff) {
                std::cout << "[ProjectionSync] Removing stale projection from Torus " 
                          << it->first << std::endl;
                it = remote_projections_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    size_t getProjectionCount() const {
        std::lock_guard<std::mutex> lock(projections_mutex_);
        return remote_projections_.size();
    }
};

} // namespace network
