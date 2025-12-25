#pragma once

#include "../Allocator.h"
#include "../FixedStructures.h"
#include "../ToroidalSpace.h"
#include "../core/Crypto.h"
#include "../core/Economics.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>

// Betti-RDL Integration
// Combines toroidal space (Betti) with time-native events (RDL)

// RDL Event: timestamped message between processes
// Now includes cryptographic signature and gas fields
struct RDLEvent {
  unsigned long long timestamp;
  int dst_node;
  int src_node;
  int payload;
  
  // Blockchain fields
  crypto::Address from;           // Sender address
  crypto::Address to;             // Recipient address (for transfers)
  uint64_t gas_price;             // Gas price
  uint64_t gas_limit;             // Gas limit
  uint64_t nonce;                 // Transaction nonce
  crypto::Signature signature;    // Cryptographic signature
  bool verified;                  // Signature verified flag

  RDLEvent() : timestamp(0), dst_node(0), src_node(0), payload(0),
               gas_price(0), gas_limit(0), nonce(0), verified(false) {
    from.fill(0);
    to.fill(0);
    signature.fill(0);
  }

  // Canonical ordering for determinism
  bool operator<(const RDLEvent &other) const {
    if (timestamp != other.timestamp)
      return timestamp < other.timestamp;
    if (dst_node != other.dst_node)
      return dst_node < other.dst_node;
    if (src_node != other.src_node)
      return src_node < other.src_node;
    return payload < other.payload;
  }
};

// BettiRDLProcess: Alias to real Process struct from ToroidalSpace.h
// Process now has real state, accumulator, event counts, etc.
using BettiRDLProcess = Process;

// Edge with adaptive delay (RDL concept)
struct AdaptiveEdge {
  int from_x, from_y, from_z;
  int to_x, to_y, to_z;
  unsigned long long delay; // Time delay (RDL memory)

  void updateDelay(int payload, unsigned long long /*current_time*/) {
    if (payload > 0) {
      delay = std::max(1ULL, delay - 1);
    } else {
      delay = delay + 1;
    }
  }
};

class BettiRDLKernel {
private:
  static constexpr int kDim = 32;
  static constexpr std::uint32_t kInvalidEdge = 0xFFFFFFFFu;

  static constexpr std::uint32_t nodeId(int x, int y, int z) {
    const int wx = ToroidalSpace<kDim, kDim, kDim>::wrap(x, kDim);
    const int wy = ToroidalSpace<kDim, kDim, kDim>::wrap(y, kDim);
    const int wz = ToroidalSpace<kDim, kDim, kDim>::wrap(z, kDim);
    return static_cast<std::uint32_t>(wx * 1024 + wy * 32 + wz);
  }

  static constexpr void decodeNode(std::uint32_t node, int &x, int &y, int &z) {
    x = static_cast<int>(node / 1024u);
    y = static_cast<int>((node % 1024u) / 32u);
    z = static_cast<int>(node % 32u);
  }

  static constexpr std::size_t kMaxPendingEvents = 2048;
  static constexpr std::size_t kMaxEdges = 2048;
  static constexpr std::size_t kMaxProcesses = 1024;

  ToroidalSpace<kDim, kDim, kDim> space;
  FixedMinHeap<RDLEvent, kMaxPendingEvents> event_queue;
  FixedObjectPool<BettiRDLProcess, kMaxProcesses> process_pool;

  struct EdgeEntry {
    std::uint32_t from_node;
    std::uint32_t to_node;
    AdaptiveEdge edge;
    std::uint32_t next_out = kInvalidEdge;
  };

  std::array<std::uint32_t, LATTICE_SIZE> out_head_{};
  std::array<std::uint32_t, LATTICE_SIZE> out_tail_{};
  std::array<EdgeEntry, kMaxEdges> edges_{};
  std::size_t edge_count_ = 0;

  unsigned long long current_time = 0;
  unsigned long long events_processed = 0;  // Lifetime total
  int process_counter = 0;

  // Thread-safety for concurrent injectEvent
  std::mutex event_injection_lock;
  FixedVector<RDLEvent, 4096> pending_events;

  [[nodiscard]] bool insertOrUpdateEdge(const AdaptiveEdge &edge) {
    const std::uint32_t from = nodeId(edge.from_x, edge.from_y, edge.from_z);
    const std::uint32_t to = nodeId(edge.to_x, edge.to_y, edge.to_z);

    // Check existing edges for this source (bounded, deterministic scan)
    for (std::uint32_t idx = out_head_[from]; idx != kInvalidEdge;
         idx = edges_[idx].next_out) {
      if (edges_[idx].to_node == to) {
        edges_[idx].edge = edge;
        return true;
      }
    }

    if (edge_count_ >= kMaxEdges) {
      assert(false && "AdaptiveEdge capacity exceeded");
      return false;
    }

    const std::uint32_t new_idx = static_cast<std::uint32_t>(edge_count_++);
    edges_[new_idx] = EdgeEntry{from, to, edge, kInvalidEdge};

    if (out_head_[from] == kInvalidEdge) {
      out_head_[from] = new_idx;
      out_tail_[from] = new_idx;
    } else {
      edges_[out_tail_[from]].next_out = new_idx;
      out_tail_[from] = new_idx;
    }

    return true;
  }

public:
  BettiRDLKernel() {
    std::cout << "[BETTI-RDL] Initializing space-time kernel..." << std::endl;
    std::cout << "    > Spatial: ToroidalSpace<32,32,32>" << std::endl;
    std::cout << "    > Temporal: Event-driven with adaptive delays" << std::endl;

    out_head_.fill(kInvalidEdge);
    out_tail_.fill(kInvalidEdge);
  }

  bool spawnProcess(int x, int y, int z) {
    // Bounds validation - allow wrapping but reject extreme values
    constexpr int limit = kDim * 4;  // Allow reasonable wrapping
    if (x < -limit || x >= limit || y < -limit || y >= limit || z < -limit || z >= limit) {
      return false;
    }
    Process *p = process_pool.create(++process_counter, x, y, z);
    if (!p) {
      return false;
    }
    return space.addProcess(p, x, y, z);
  }
  
  // REAL: Spawn process with initial state
  bool spawnProcessWithState(int x, int y, int z, int64_t initial_state, int64_t initial_accum) {
    // Bounds validation - allow wrapping but reject extreme values
    constexpr int limit = kDim * 4;
    if (x < -limit || x >= limit || y < -limit || y >= limit || z < -limit || z >= limit) {
      return false;
    }
    Process *p = process_pool.create(++process_counter, x, y, z);
    if (!p) {
      return false;
    }
    p->state = initial_state;
    p->accumulator = initial_accum;
    return space.addProcess(p, x, y, z);
  }
  
  // REAL: Get total accumulated computation across all processes
  int64_t getTotalAccumulator() const {
    int64_t total = 0;
    process_pool.forEach([&total](const Process& p) {
      total += p.accumulator;
    });
    return total;
  }
  
  // REAL: Get total events received across all processes
  uint64_t getTotalEventsReceived() const {
    uint64_t total = 0;
    process_pool.forEach([&total](const Process& p) {
      total += p.events_received;
    });
    return total;
  }

  bool createEdge(int x1, int y1, int z1, int x2, int y2, int z2,
                  unsigned long long initial_delay) {
    // Bounds validation - allow wrapping but reject extreme values
    constexpr int limit = kDim * 4;
    if (x1 < -limit || x1 >= limit || y1 < -limit || y1 >= limit || z1 < -limit || z1 >= limit ||
        x2 < -limit || x2 >= limit || y2 < -limit || y2 >= limit || z2 < -limit || z2 >= limit) {
      return false;
    }
    
    AdaptiveEdge edge{};
    edge.from_x = x1;
    edge.from_y = y1;
    edge.from_z = z1;
    edge.to_x = x2;
    edge.to_y = y2;
    edge.to_z = z2;
    edge.delay = initial_delay;

    return insertOrUpdateEdge(edge);
  }

  bool injectEvent(int dst_x, int dst_y, int dst_z, int src_x, int src_y,
                   int src_z, int payload) {
    // Bounds validation - allow wrapping but reject extreme values
    constexpr int limit = kDim * 4;
    if (dst_x < -limit || dst_x >= limit || dst_y < -limit || dst_y >= limit || dst_z < -limit || dst_z >= limit ||
        src_x < -limit || src_x >= limit || src_y < -limit || src_y >= limit || src_z < -limit || src_z >= limit) {
      return false;
    }
    
    RDLEvent evt{};
    evt.timestamp = current_time;
    evt.dst_node = static_cast<int>(nodeId(dst_x, dst_y, dst_z));
    evt.src_node = static_cast<int>(nodeId(src_x, src_y, src_z));
    evt.payload = payload;

    // Thread-safe injection: add to pending queue
    {
      std::lock_guard<std::mutex> lock(event_injection_lock);
      if (!pending_events.push_back(evt)) {
        return false;
      }
    }
    return true;
  }

  // Transfer pending events to the main event queue (single-threaded from scheduler)
  private:
  void flushPendingEvents() {
    std::lock_guard<std::mutex> lock(event_injection_lock);
    for (std::size_t i = 0; i < pending_events.size(); ++i) {
      (void)event_queue.push(pending_events[i]);
    }
    pending_events.clear();
  }

  public:

  void tick() {
    flushPendingEvents();
    if (event_queue.empty()) {
      return;
    }

    RDLEvent evt = event_queue.top();
    (void)event_queue.pop();

    current_time = evt.timestamp;
    events_processed++;

    // REAL: Decode destination and deliver event to actual process
    const std::uint32_t dst_node = static_cast<std::uint32_t>(evt.dst_node);
    int dx, dy, dz;
    decodeNode(dst_node, dx, dy, dz);
    
    // REAL: Find process at destination and execute real computation
    Process* dst_process = space.getProcess(dx, dy, dz);
    if (dst_process) {
      // REAL: Process executes actual state transition
      dst_process->processEvent(static_cast<int64_t>(evt.payload), current_time);
    }

    // Propagate along outgoing edges
    for (std::uint32_t idx = out_head_[dst_node]; idx != kInvalidEdge;
         idx = edges_[idx].next_out) {
      EdgeEntry &entry = edges_[idx];

      entry.edge.updateDelay(evt.payload, current_time);

      // REAL: New payload is derived from actual process state
      int new_payload = evt.payload + 1;
      if (dst_process) {
        // Use actual accumulated state to influence propagation
        new_payload = static_cast<int>((dst_process->state & 0xFF) + 1);
        dst_process->events_sent++;
      }

      RDLEvent new_evt{};
      new_evt.timestamp = current_time + entry.edge.delay;
      new_evt.dst_node = static_cast<int>(entry.to_node);
      new_evt.src_node = evt.dst_node;
      new_evt.payload = new_payload;

      (void)event_queue.push(new_evt);
    }
  }

  // Process at most max_events NEW events, returning the count processed
  // Does not depend on lifetime events_processed total
  int run(int max_events) {
    std::cout << "\n[BETTI-RDL] Starting execution..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    size_t mem_before = MemoryManager::getUsedMemory();

    // Flush any pending events from concurrent injections
    flushPendingEvents();

    int events_in_run = 0;
    while (events_in_run < max_events && !event_queue.empty()) {
      tick();
      events_in_run++;

      if (events_processed % 100000 == 0) {
        std::cout << "    > Events (lifetime): " << events_processed
                  << ", Events (this run): " << events_in_run
                  << ", Time: " << current_time
                  << ", Queue: " << event_queue.size() << std::endl;
      }
    }

    auto end = std::chrono::high_resolution_clock::now();
    size_t mem_after = MemoryManager::getUsedMemory();

    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\n[BETTI-RDL] ✓ EXECUTION COMPLETE" << std::endl;
    std::cout << "    > Events Processed (this run): " << events_in_run << std::endl;
    std::cout << "    > Events Processed (lifetime): " << events_processed << std::endl;
    std::cout << "    > Final Time: " << current_time << std::endl;
    std::cout << "    > Processes: " << space.getProcessCount() << std::endl;
    std::cout << "    > Edges: " << edge_count_ << std::endl;
    std::cout << "    > Duration: " << duration.count() << "ms" << std::endl;
    std::cout << "    > Memory Before: " << mem_before << " bytes" << std::endl;
    std::cout << "    > Memory After: " << mem_after << " bytes" << std::endl;
    std::cout << "    > Memory Delta: " << (mem_after - mem_before) << " bytes"
              << std::endl;
    if (duration.count() > 0) {
      std::cout << "    > Events/sec: "
                << (events_in_run * 1000.0 / duration.count()) << std::endl;
    }

    return events_in_run;
  }

  unsigned long long getCurrentTime() const { return current_time; }
  unsigned long long getEventsProcessed() const { return events_processed; }
  
  // Phase 2: Expose internal state for braiding
  std::size_t getActiveProcessCount() const { return process_pool.size(); }
  std::size_t getPendingEventCount() const { return event_queue.size(); }
  std::size_t getEdgeCount() const { return edge_count_; }
  
  // REAL: Returns actual process state hash at coordinate
  int getProcessStateAt(int x, int y, int z) const {
    int wx = ToroidalSpace<kDim, kDim, kDim>::wrap(x, kDim);
    int wy = ToroidalSpace<kDim, kDim, kDim>::wrap(y, kDim);
    int wz = ToroidalSpace<kDim, kDim, kDim>::wrap(z, kDim);
    
    // Return REAL state hash from actual processes
    return static_cast<int>(space.getStateHash(wx, wy, wz));
  }
  
  // REAL: Get accumulated computation result at coordinate
  int64_t getAccumulatedStateAt(int x, int y, int z) const {
    int wx = ToroidalSpace<kDim, kDim, kDim>::wrap(x, kDim);
    int wy = ToroidalSpace<kDim, kDim, kDim>::wrap(y, kDim);
    int wz = ToroidalSpace<kDim, kDim, kDim>::wrap(z, kDim);
    return space.getAccumulatedState(wx, wy, wz);
  }
  
  // REAL: Get process pointer for direct state access
  Process* getProcessAt(int x, int y, int z, std::size_t idx = 0) {
    int wx = ToroidalSpace<kDim, kDim, kDim>::wrap(x, kDim);
    int wy = ToroidalSpace<kDim, kDim, kDim>::wrap(y, kDim);
    int wz = ToroidalSpace<kDim, kDim, kDim>::wrap(z, kDim);
    return space.getProcess(wx, wy, wz, idx);
  }
  
  const Process* getProcessAt(int x, int y, int z, std::size_t idx = 0) const {
    int wx = ToroidalSpace<kDim, kDim, kDim>::wrap(x, kDim);
    int wy = ToroidalSpace<kDim, kDim, kDim>::wrap(y, kDim);
    int wz = ToroidalSpace<kDim, kDim, kDim>::wrap(z, kDim);
    return space.getProcess(wx, wy, wz, idx);
  }
  
  void getBoundaryState(int face, std::array<uint32_t, 1024>& out) const {
    for (int y = 0; y < 32; y++) {
      for (int z = 0; z < 32; z++) {
        int x = (face == 0) ? 0 : 31;
        out[y * 32 + z] = static_cast<uint32_t>(getProcessStateAt(x, y, z));
      }
    }
  }
  
  bool injectCorrectiveEvent(int x, int y, int z, int32_t correction) {
    return injectEvent(x, y, z, x, y, z, correction);
  }
  
  void reset() {
    while (!event_queue.empty()) { (void)event_queue.pop(); }
    { std::lock_guard<std::mutex> lock(event_injection_lock); pending_events.clear(); }
    process_pool.clear();
    edge_count_ = 0;
    out_head_.fill(kInvalidEdge);
    out_tail_.fill(kInvalidEdge);
    space.clear();
    current_time = 0;
    events_processed = 0;
    process_counter = 0;
  }
};
