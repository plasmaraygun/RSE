#pragma once

#include "FixedStructures.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

// Process: Real computational unit with actual state
struct Process {
  int32_t pid;
  int32_t x, y, z;
  int64_t state;           // Actual computational state
  int64_t accumulator;     // Running computation result
  uint64_t events_received;
  uint64_t events_sent;
  uint64_t last_update_time;
  
  Process() : pid(0), x(0), y(0), z(0), state(0), accumulator(0),
              events_received(0), events_sent(0), last_update_time(0) {}
  
  Process(int32_t id, int32_t px, int32_t py, int32_t pz)
      : pid(id), x(px), y(py), z(pz), state(0), accumulator(0),
        events_received(0), events_sent(0), last_update_time(0) {}
  
  // Real computation: process incoming event and update state
  void processEvent(int64_t payload, uint64_t timestamp) {
    events_received++;
    last_update_time = timestamp;
    
    // Real state transition function
    // Uses payload to modify state deterministically
    int64_t old_state = state;
    state = (state * 31 + payload) ^ (old_state >> 3);
    accumulator += payload;
  }
  
  // Get state hash for boundary comparison
  uint32_t getStateHash() const {
    // Real hash of actual state
    uint64_t h = static_cast<uint64_t>(state);
    h ^= static_cast<uint64_t>(accumulator) << 17;
    h ^= events_received * 0x9E3779B97F4A7C15ULL;
    return static_cast<uint32_t>(h ^ (h >> 32));
  }
};

// Template-based toroidal lattice.
// Storage is bounded and deterministic:
// - compile-time indexed 1D array of cells
// - fixed-capacity process pointer list per voxel
template <int WIDTH, int HEIGHT, int DEPTH, std::size_t MAX_PROCESSES_PER_CELL = 8>
class ToroidalSpace {
public:
  static constexpr std::size_t kCellCount =
      static_cast<std::size_t>(WIDTH) * static_cast<std::size_t>(HEIGHT) *
      static_cast<std::size_t>(DEPTH);

  ToroidalSpace() {
    std::cout << "[Metal] ToroidalSpace <" << WIDTH << "x" << HEIGHT << "x"
              << DEPTH << "> Init." << std::endl;
  }

  // Wrap Coordinate (Topology Logic)
  static constexpr int wrap(int v, int max) {
    return ((v % max) + max) % max;
  }

  [[nodiscard]] constexpr std::size_t index(int x, int y, int z) const {
    const int wx = wrap(x, WIDTH);
    const int wy = wrap(y, HEIGHT);
    const int wz = wrap(z, DEPTH);

    return static_cast<std::size_t>(wx) +
           static_cast<std::size_t>(WIDTH) *
               (static_cast<std::size_t>(wy) +
                static_cast<std::size_t>(HEIGHT) *
                    static_cast<std::size_t>(wz));
  }

  bool addProcess(Process *p, int x, int y, int z) {
    Cell &cell = cells_[index(x, y, z)];
    const bool ok = cell.processes.push_back(p);
    if (ok) {
      ++total_processes_;
    }
    return ok;
  }

  bool removeProcess(Process *p, int x, int y, int z) {
    Cell &cell = cells_[index(x, y, z)];
    const bool removed = cell.processes.erase_first(p);
    if (removed) {
      assert(total_processes_ > 0);
      --total_processes_;
    }
    return removed;
  }

  [[nodiscard]] std::size_t getProcessCount() const { return total_processes_; }
  
  [[nodiscard]] std::size_t getProcessCount(int x, int y, int z) const {
    return cells_[index(x, y, z)].processes.size();
  }
  
  // Get real process state at coordinate
  [[nodiscard]] Process* getProcess(int x, int y, int z, std::size_t idx = 0) {
    Cell& cell = cells_[index(x, y, z)];
    if (idx >= cell.processes.size()) return nullptr;
    return cell.processes[idx];
  }
  
  [[nodiscard]] const Process* getProcess(int x, int y, int z, std::size_t idx = 0) const {
    const Cell& cell = cells_[index(x, y, z)];
    if (idx >= cell.processes.size()) return nullptr;
    return cell.processes[idx];
  }
  
  // Get aggregate state hash at coordinate (for boundary extraction)
  [[nodiscard]] uint32_t getStateHash(int x, int y, int z) const {
    const Cell& cell = cells_[index(x, y, z)];
    if (cell.processes.size() == 0) return 0;
    
    uint32_t hash = 0;
    for (std::size_t i = 0; i < cell.processes.size(); i++) {
      if (cell.processes[i]) {
        hash ^= cell.processes[i]->getStateHash();
        hash = (hash << 5) | (hash >> 27); // Rotate
      }
    }
    return hash;
  }
  
  // Get total accumulated state across all processes at coordinate
  [[nodiscard]] int64_t getAccumulatedState(int x, int y, int z) const {
    const Cell& cell = cells_[index(x, y, z)];
    int64_t total = 0;
    for (std::size_t i = 0; i < cell.processes.size(); i++) {
      if (cell.processes[i]) {
        total += cell.processes[i]->accumulator;
      }
    }
    return total;
  }
  
  // Clear all processes (for reset)
  void clear() {
    for (auto& cell : cells_) {
      cell.processes.clear();
    }
    total_processes_ = 0;
  }

private:
  struct Cell {
    FixedVector<Process *, MAX_PROCESSES_PER_CELL> processes;
  };

  std::array<Cell, kCellCount> cells_{};
  std::size_t total_processes_ = 0;
};
