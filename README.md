# RSE - Recursive Spatial Engine

A high-performance blockchain operating system built on a braided torus architecture.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│              Userspace (Shell, Applications)            │
├─────────────────────────────────────────────────────────┤
│  System Calls (43 defined, Linux-compatible subset)     │
├─────────────────────────────────────────────────────────┤
│  Scheduler │ Memory Mgmt │ File System │ I/O System    │
├─────────────────────────────────────────────────────────┤
│  Braided Torus Runtime (3-torus with fault tolerance)   │
│  - Lock-free parallel execution                         │
│  - Automatic 2-of-3 Byzantine fault tolerance           │
├─────────────────────────────────────────────────────────┤
│  Core Kernel (Multiboot2, 64-bit long mode)             │
└─────────────────────────────────────────────────────────┘
```

## Features

### Kernel
- **Braided Torus**: 3 independent tori with cyclic constraints
- **Memory**: 128MB physical memory, kernel heap, kmalloc/kfree
- **File System**: RAM-based VFS with file descriptors
- **Syscalls**: Linux-compatible subset (read, write, open, close, etc.)
- **Shell**: Interactive prompt with built-in commands

### Blockchain
- **Crypto**: Ed25519 signatures, BLAKE2b hashing (libsodium)
- **P2P**: Real TCP/UDP networking with STUN NAT traversal
- **Persistence**: Immutable content-addressed snapshots
- **Economics**: Gas metering, staking, rewards
- **Merkle Trees**: Sparse Merkle trees for O(log n) state proofs

### Inference
- **Petals Integration**: Distributed LLM inference
- **Ollama Support**: Local model execution
- **Bridge**: TCP socket communication between kernel and inference

## Performance

| Metric | Result |
|--------|--------|
| Single-torus throughput | 16.8M events/sec |
| Parallel scaling (16 kernels) | 285.7M events/sec |
| Memory stability | O(1) - constant |
| Syscall performance | 100× faster than traditional |

## Directory Structure

```
├── cpp_kernel/          # C++ kernel implementation
│   ├── boot/           # Multiboot2 boot, kernel entry
│   ├── core/           # Crypto, Economics, Persistence
│   ├── braided/        # Braided torus runtime
│   ├── network/        # P2P, STUN, TCP sockets
│   ├── inference/      # Petals client, inference node
│   ├── os/             # Syscalls, processes, scheduler
│   ├── drivers/        # Framebuffer, keyboard, serial
│   └── tests/          # Test suite
├── inference_bridge/    # Python inference bridge (Petals/Ollama)
├── web_dashboard/       # React dashboard with wallet/explorer
└── dashboard/           # Static dashboard
```

## Building

```bash
# Build kernel
cd cpp_kernel
make

# Run tests
g++ -std=c++20 -O2 -pthread -I. tests/persistence_test.cpp -o test -lsodium
./test

# Run performance benchmark
g++ -std=c++20 -O2 -pthread -I. tests/performance_harness.cpp -o bench -lsodium -latomic
./bench --nodes 1000 --txs 10000
```

## Dependencies

- C++20 compiler (GCC 11+ or Clang 14+)
- libsodium (cryptography)
- Python 3.8+ (inference bridge)
- Node.js 18+ (web dashboard)

## License

MIT
