# RSE Core Infrastructure

This directory contains the **shared infrastructure** used by all RSE execution modes (single-torus, braided-torus, and future OS components).

## Components

### Allocator.h
**Bounded Arena Allocator** with O(1) memory guarantee.

- Fixed-size pools for processes, events, and edges
- No dynamic allocation during execution
- Predictable memory usage
- Thread-safe allocation

**Key Features**:
- Process pool: 327,680 processes × 64 bytes = 20.9 MB
- Event pool: 1,638,400 events × 32 bytes = 52.4 MB
- Edge pool: 163,840 edges × 64 bytes = 10.5 MB
- Generic pool: 67.1 MB

**Total**: ~150 MB fixed memory footprint

### FixedStructures.h
**Fixed-size data structures** for deterministic performance.

- `FixedMinHeap<T, N>`: Priority queue with O(log N) operations
- `FixedVector<T, N>`: Dynamic array with fixed capacity
- `FixedObjectPool<T, N>`: Object pool with O(1) allocation

All structures have **compile-time size guarantees** and no dynamic allocation.

### ToroidalSpace.h
**3D Toroidal Lattice** for spatial process organization.

- 32×32×32 lattice with wraparound topology
- O(1) neighbor lookup
- Deterministic spatial routing
- Process-to-cell mapping

**Topology**:
```
   (x, y, z) → (x+1 mod 32, y, z)  [wraparound in all dimensions]
```

This creates a **closed universe** with no boundaries—every cell has exactly 6 neighbors.

---

## Design Philosophy

The core infrastructure is designed for:

1. **Predictability**: No dynamic allocation, no unbounded growth
2. **Performance**: O(1) or O(log N) operations everywhere
3. **Determinism**: Same input → same output, always
4. **Simplicity**: Minimal dependencies, easy to understand

These properties make RSE suitable for:
- Real-time systems
- Embedded systems
- Distributed systems
- Operating system kernels

---

## Usage

All RSE execution modes (single-torus, braided-torus, OS) build on top of these core components:

```cpp
#include "core/Allocator.h"
#include "core/FixedStructures.h"
#include "core/ToroidalSpace.h"
```

The core is **mode-agnostic**—it doesn't know or care whether it's being used in a single-torus system, a braided system, or an OS kernel.
