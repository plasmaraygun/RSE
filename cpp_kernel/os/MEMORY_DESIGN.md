# Memory Management Design

**Goal**: Give applications isolated virtual memory spaces with protection and efficient allocation.

---

## Overview

Memory management is critical for:
1. **Isolation** - Processes can't access each other's memory
2. **Protection** - Prevent crashes from corrupting the OS
3. **Efficiency** - Fast allocation/deallocation
4. **Simplicity** - Easy to understand and debug

---

## Key Concepts

### **Physical vs Virtual Memory**

**Physical Memory**: Actual RAM addresses (0x0000 to 0xFFFF...)
**Virtual Memory**: What processes see (each process thinks it has all memory)

```
Process A sees:        Process B sees:        Physical RAM:
0x0000 - 0xFFFF       0x0000 - 0xFFFF       0x0000 - 0xFFFF
     ↓                     ↓                      ↑
  Page Table A         Page Table B          Actual RAM
     ↓                     ↓                      ↑
     └──────────────────────┴──────────────────────┘
```

### **Pages**

Memory is divided into fixed-size **pages** (typically 4KB):
- Simplifies allocation (no fragmentation)
- Hardware MMU (Memory Management Unit) works with pages
- Page table maps virtual pages to physical pages

### **Page Table**

Maps virtual addresses to physical addresses:

```
Virtual Address:  [Page Number | Offset]
                       ↓
                  Page Table
                       ↓
Physical Address: [Frame Number | Offset]
```

Example (4KB pages):
- Virtual address 0x1234 = Page 1, Offset 0x234
- Page table[1] = Frame 5
- Physical address = 0x5234

---

## Braided OS Memory Architecture

### **Per-Torus Memory Management**

Each torus manages its own memory:

```
Torus A:
- Page tables for processes on Torus A
- Physical memory pool A
- Memory allocator A

Torus B:
- Page tables for processes on Torus B
- Physical memory pool B
- Memory allocator B

Torus C:
- Page tables for processes on Torus C
- Physical memory pool C
- Memory allocator C
```

**No global memory manager** → No bottleneck!

### **Memory Layout (Per Process)**

```
0x0000_0000_0000_0000  ┌─────────────────┐
                       │   Code (.text)  │  Executable code
0x0000_0000_0040_0000  ├─────────────────┤
                       │   Data (.data)  │  Initialized data
0x0000_0000_0080_0000  ├─────────────────┤
                       │   BSS (.bss)    │  Uninitialized data
0x0000_0000_00C0_0000  ├─────────────────┤
                       │   Heap          │  Dynamic allocation (grows ↑)
                       │       ↓         │
                       │       ...       │
                       │       ↑         │
                       │   Stack         │  Function calls (grows ↓)
0x0000_7FFF_FFFF_F000  ├─────────────────┤
                       │   Kernel        │  OS code/data
0xFFFF_FFFF_FFFF_FFFF  └─────────────────┘
```

---

## Page Table Structure

### **Simple Two-Level Page Table**

For simplicity, we'll use a **two-level page table**:

```
Virtual Address (64-bit):
┌──────────┬──────────┬────────────┐
│ L1 Index │ L2 Index │   Offset   │
│ (10 bits)│ (10 bits)│  (12 bits) │
└──────────┴──────────┴────────────┘
    ↓           ↓           ↓
  1024        1024       4096 bytes
  entries     entries    per page
```

**L1 Page Table** (1024 entries):
- Each entry points to an L2 page table
- Covers 4MB of virtual memory per entry

**L2 Page Table** (1024 entries):
- Each entry maps to a physical page (4KB)
- Contains flags (present, writable, executable)

**Total addressable**: 1024 × 1024 × 4KB = 4GB per process

### **Page Table Entry (PTE)**

```cpp
struct PageTableEntry {
    uint64_t present    : 1;   // Page is in memory
    uint64_t writable   : 1;   // Page is writable
    uint64_t user       : 1;   // User mode can access
    uint64_t accessed   : 1;   // Page was accessed
    uint64_t dirty      : 1;   // Page was written to
    uint64_t reserved   : 7;   // Reserved bits
    uint64_t frame      : 40;  // Physical frame number
    uint64_t available  : 12;  // Available for OS use
};
```

---

## Memory Allocation

### **Physical Memory Allocator**

Manages physical RAM frames (4KB each):

```cpp
class PhysicalAllocator {
    Bitmap free_frames_;  // 1 bit per frame
    uint64_t total_frames_;
    uint64_t free_count_;
    
public:
    uint64_t allocate_frame();     // Get free frame
    void free_frame(uint64_t frame); // Return frame
    uint64_t available();           // Free frames
};
```

Uses a **bitmap** to track free/used frames:
- 0 = free
- 1 = used

### **Virtual Memory Allocator**

Manages virtual address space per process:

```cpp
class VirtualAllocator {
    PageTable* page_table_;
    uint64_t heap_start_;
    uint64_t heap_end_;
    uint64_t heap_brk_;
    
public:
    void* allocate(size_t size);      // Allocate virtual memory
    void free(void* addr);             // Free virtual memory
    bool map(void* virt, uint64_t phys); // Map virtual to physical
    void unmap(void* virt);            // Unmap virtual address
};
```

---

## Memory Operations

### **1. Allocate Memory (brk/sbrk)**

```cpp
// Application calls:
void* ptr = sbrk(4096);  // Allocate 4KB

// OS does:
1. Check if heap can grow
2. Allocate physical frames
3. Map virtual pages to physical frames
4. Update page table
5. Return virtual address
```

### **2. Map Memory (mmap)**

```cpp
// Application calls:
void* ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, -1, 0);

// OS does:
1. Find free virtual address range
2. Allocate physical frames
3. Map virtual to physical
4. Set protection flags
5. Return virtual address
```

### **3. Unmap Memory (munmap)**

```cpp
// Application calls:
munmap(ptr, 4096);

// OS does:
1. Find virtual address in page table
2. Free physical frames
3. Remove page table entries
4. Flush TLB (Translation Lookaside Buffer)
```

### **4. Change Protection (mprotect)**

```cpp
// Application calls:
mprotect(ptr, 4096, PROT_READ);  // Make read-only

// OS does:
1. Find page table entries
2. Update protection flags
3. Flush TLB
```

---

## Page Fault Handling

When a process accesses unmapped memory:

1. **CPU generates page fault exception**
2. **OS page fault handler runs**:
   - Check if address is valid
   - If valid: allocate frame, map page, resume
   - If invalid: kill process (segmentation fault)

```cpp
void page_fault_handler(uint64_t fault_addr) {
    OSProcess* proc = get_current_process();
    
    // Check if address is in valid range
    if (fault_addr >= proc->memory.heap_start && 
        fault_addr < proc->memory.heap_end) {
        // Valid - allocate and map
        uint64_t frame = phys_alloc.allocate_frame();
        proc->page_table->map(fault_addr, frame);
        return;  // Resume process
    }
    
    // Invalid - segmentation fault
    std::cerr << "Segmentation fault at " << std::hex << fault_addr << std::endl;
    proc->setZombie(-1);
}
```

---

## Memory Protection

### **Protection Flags**

- **PROT_NONE**: No access
- **PROT_READ**: Read-only
- **PROT_WRITE**: Read/write
- **PROT_EXEC**: Executable

### **Enforcement**

Hardware MMU enforces protection:
- Write to read-only page → Page fault
- Execute non-executable page → Page fault
- Access kernel page from user mode → Page fault

---

## Integration with Braided System

### **Per-Torus Physical Memory**

Each torus has its own physical memory pool:

```
Total RAM: 16GB
Torus A: 5.3GB (1/3)
Torus B: 5.3GB (1/3)
Torus C: 5.3GB (1/3)
```

Processes on Torus A use Torus A's memory pool.

### **Process Migration**

When a process migrates from Torus A to Torus B:

1. **Copy page table** to Torus B
2. **Don't copy physical pages** (lazy migration)
3. **On page fault**: Copy page from Torus A to Torus B

This is **lazy migration** - only copy pages when accessed.

---

## Simplified Implementation (Phase 6.2)

For now, we'll implement a **simplified version**:

### **No Hardware MMU**

- Software-based address translation
- Slower but simpler
- Can add hardware MMU later

### **Fixed Page Size**

- 4KB pages only
- Simplifies implementation

### **No Demand Paging**

- Allocate physical frames immediately
- No page faults (yet)

### **No Swapping**

- All memory stays in RAM
- No disk I/O

---

## Implementation Plan

### **Phase 6.2.1: Page Table**
- Implement PageTableEntry structure
- Implement two-level page table
- Implement map/unmap operations

### **Phase 6.2.2: Physical Allocator**
- Implement bitmap-based frame allocator
- Implement allocate_frame/free_frame

### **Phase 6.2.3: Virtual Allocator**
- Implement virtual memory allocator
- Integrate with page table
- Implement brk/mmap/munmap syscalls

### **Phase 6.2.4: Testing**
- Test page table operations
- Test memory allocation
- Test syscalls (brk, mmap, munmap)

---

## Success Criteria

### **Functional**
- ✅ Page tables map virtual to physical
- ✅ Physical allocator manages frames
- ✅ Virtual allocator manages address space
- ✅ brk/mmap/munmap syscalls work
- ✅ Memory protection enforced

### **Performance**
- ✅ O(1) frame allocation
- ✅ O(1) address translation (with caching)
- ✅ Minimal overhead (<5%)

---

## The Vision

**Traditional OS**: Global memory manager → bottleneck

**Braided OS**: Per-torus memory managers → no bottleneck

Each torus manages its own memory independently. No locks, no contention, perfect scaling.

**This is how we make old hardware fast.** 🚀
