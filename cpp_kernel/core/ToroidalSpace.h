#pragma once

#include "FixedStructures.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>

// Forward declaration
struct Process;

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
  
  void clear() {
    for (auto& cell : cells_) { cell.processes.clear(); }
    total_processes_ = 0;
  }

private:
  struct Cell {
    FixedVector<Process *, MAX_PROCESSES_PER_CELL> processes;
  };

  std::array<Cell, kCellCount> cells_{};
  std::size_t total_processes_ = 0;
};
