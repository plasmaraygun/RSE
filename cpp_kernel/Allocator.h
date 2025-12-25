#pragma once

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unistd.h>
#if defined(__linux__)
  #include <sys/resource.h>
  #include <stdio.h>
#elif defined(__APPLE__)
  #include <mach/mach.h>
#elif defined(_WIN32)
  #include <windows.h>
  #include <psapi.h>
#endif

// Bounded Arena Allocator for BettiOS
// Pre-reserves exact memory for 32³ lattice, processes, events, and edges
// Thread-safe with lock-free freelists per slab and global fallback

// ============================================================================
// Configuration
// ============================================================================

// 32³ lattice = 32768 cells
constexpr size_t LATTICE_SIZE = 32 * 32 * 32;

// Typical allocations per cell (tunable based on workload)
constexpr size_t PROCESSES_PER_CELL = 10;
constexpr size_t EVENTS_PER_CELL = 50;
constexpr size_t EDGES_PER_CELL = 5;

// Pre-allocated pool capacities (in blocks)
constexpr size_t PROCESS_POOL_CAPACITY = LATTICE_SIZE * PROCESSES_PER_CELL; // ~327k
constexpr size_t EVENT_POOL_CAPACITY = LATTICE_SIZE * EVENTS_PER_CELL;     // ~1.6M
constexpr size_t EDGE_POOL_CAPACITY = LATTICE_SIZE * EDGES_PER_CELL;       // ~163k

// Fixed block sizes per pool
// (objects are allocated as blocks and constructed via placement-new)
constexpr size_t PROCESS_BLOCK_SIZE = 64;
constexpr size_t EVENT_BLOCK_SIZE = 32;
constexpr size_t EDGE_BLOCK_SIZE = 64;

// Generic allocation pool for miscellaneous allocations
constexpr size_t GENERIC_POOL_CAPACITY = 64ULL * 1024ULL * 1024ULL; // 64MB

// Slab size for lock-free freelists (good balance between contention and cache)
constexpr size_t FREELIST_SLAB_SIZE = 64;

// ============================================================================
// Forward Declarations
// ============================================================================

class BoundedArenaAllocator;

// ============================================================================
// Statistics and Instrumentation
// ============================================================================

class AllocationStats {
public:
    std::atomic<size_t> current_usage{0};
    std::atomic<size_t> peak_usage{0};
    std::atomic<size_t> allocation_count{0};
    std::atomic<size_t> deallocation_count{0};
    std::atomic<size_t> allocation_failures{0};

    void recordAllocation(size_t size) {
        allocation_count.fetch_add(1, std::memory_order_relaxed);
        size_t new_usage = current_usage.fetch_add(size, std::memory_order_relaxed) + size;
        
        size_t peak = peak_usage.load(std::memory_order_relaxed);
        while (new_usage > peak && 
               !peak_usage.compare_exchange_weak(peak, new_usage, std::memory_order_relaxed)) {
            peak = peak_usage.load(std::memory_order_relaxed);
        }
    }

    void recordDeallocation(size_t size) {
        deallocation_count.fetch_add(1, std::memory_order_relaxed);
        current_usage.fetch_sub(size, std::memory_order_relaxed);
    }

    void recordFailure() {
        allocation_failures.fetch_add(1, std::memory_order_relaxed);
    }

    void print(const char* pool_name) const {
        std::cout << "[BoundedAllocator] " << pool_name << " Stats:" << std::endl;
        std::cout << "    Current: " << current_usage.load() << " bytes" << std::endl;
        std::cout << "    Peak: " << peak_usage.load() << " bytes" << std::endl;
        std::cout << "    Allocations: " << allocation_count.load() << std::endl;
        std::cout << "    Deallocations: " << deallocation_count.load() << std::endl;
        std::cout << "    Failures: " << allocation_failures.load() << std::endl;
    }
};

// ============================================================================
// Lock-Free Freelist Node
// ============================================================================

struct FreelistNode {
    FreelistNode* next;
    size_t size;
};

// ============================================================================
// Slab-Based Fixed Block Pool (Thread-Safe, ABA-resistant freelists)
// ============================================================================

struct TaggedFreelistHead {
    FreelistNode* ptr;
    std::uint64_t tag;
};

static_assert(std::is_trivially_copyable_v<TaggedFreelistHead>);

template <size_t BLOCK_SIZE, size_t CAPACITY>
class FixedBlockPool {
private:
    static constexpr size_t ALIGNMENT = alignof(std::max_align_t);
    static constexpr size_t ELEMENT_SIZE =
        ((BLOCK_SIZE + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
    static constexpr size_t TOTAL_SIZE = CAPACITY * ELEMENT_SIZE;

    uint8_t* arena;
    std::atomic<TaggedFreelistHead> freelists[FREELIST_SLAB_SIZE];
    std::mutex arena_mutex;
    AllocationStats stats;
    size_t allocated_count;

    size_t hash_ptr(void* ptr) const {
        return (reinterpret_cast<std::uintptr_t>(ptr) / ELEMENT_SIZE) % FREELIST_SLAB_SIZE;
    }

    void push_to_freelist(FreelistNode* node) {
        const size_t idx = hash_ptr(node);
        TaggedFreelistHead head = freelists[idx].load(std::memory_order_acquire);
        TaggedFreelistHead desired{};

        do {
            node->next = head.ptr;
            desired.ptr = node;
            desired.tag = head.tag + 1;
        } while (!freelists[idx].compare_exchange_weak(head, desired,
                                                        std::memory_order_release,
                                                        std::memory_order_acquire));
    }

    FreelistNode* pop_from_freelist(size_t idx) {
        TaggedFreelistHead head = freelists[idx].load(std::memory_order_acquire);

        while (head.ptr != nullptr) {
            FreelistNode* next = head.ptr->next;
            TaggedFreelistHead desired{next, head.tag + 1};

            if (freelists[idx].compare_exchange_weak(head, desired,
                                                     std::memory_order_release,
                                                     std::memory_order_acquire)) {
                return head.ptr;
            }
        }

        return nullptr;
    }

public:
    FixedBlockPool(const char* name) : allocated_count(0) {
        arena = static_cast<uint8_t*>(std::malloc(TOTAL_SIZE));
        if (!arena) {
            throw std::bad_alloc();
        }
        std::memset(arena, 0, TOTAL_SIZE);

        for (size_t i = 0; i < FREELIST_SLAB_SIZE; ++i) {
            freelists[i].store(TaggedFreelistHead{nullptr, 0}, std::memory_order_relaxed);
        }

        std::cout << "[BoundedAllocator] Initialized " << name
                  << " pool: " << CAPACITY << " x " << ELEMENT_SIZE
                  << " = " << TOTAL_SIZE << " bytes" << std::endl;
    }

    ~FixedBlockPool() {
        if (arena) {
            std::free(arena);
            arena = nullptr;
        }
    }

    void* allocate() {
        FreelistNode* node = nullptr;

        // Try all slabs (bounded)
        for (size_t i = 0; i < FREELIST_SLAB_SIZE; ++i) {
            node = pop_from_freelist(i);
            if (node) break;
        }

        void* ptr = nullptr;
        if (node) {
            ptr = node;
        } else {
            // Allocate from arena
            std::lock_guard<std::mutex> lock(arena_mutex);
            if (allocated_count < CAPACITY) {
                ptr = arena + (allocated_count * ELEMENT_SIZE);
                allocated_count++;
            } else {
                stats.recordFailure();
                return nullptr;
            }
        }

        stats.recordAllocation(ELEMENT_SIZE);
        return ptr;
    }

    void deallocate(void* ptr) {
        if (!ptr) return;

        uintptr_t ptr_val = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t arena_start = reinterpret_cast<uintptr_t>(arena);
        uintptr_t arena_end = arena_start + TOTAL_SIZE;

        if (ptr_val < arena_start || ptr_val >= arena_end) {
            std::cerr << "[BoundedAllocator] ERROR: Pointer not in pool!" << std::endl;
            return;
        }

        FreelistNode* node = reinterpret_cast<FreelistNode*>(ptr);
        node->size = ELEMENT_SIZE;
        push_to_freelist(node);

        stats.recordDeallocation(ELEMENT_SIZE);
    }

    size_t getCapacity() const { return CAPACITY; }
    size_t getBlockSize() const { return ELEMENT_SIZE; }

    size_t getAllocatedCount() const {
        std::lock_guard<std::mutex> lock(arena_mutex);
        return allocated_count;
    }

    void printStats(const char* name) const { stats.print(name); }

    size_t getCurrentUsage() const {
        return stats.current_usage.load(std::memory_order_relaxed);
    }
};

// ============================================================================
// Generic Slab-Based Memory Pool (for miscellaneous allocations)
// ============================================================================

class GenericPool {
private:
    uint8_t* arena;
    std::atomic<size_t> allocated_offset{0};
    AllocationStats stats;
    std::mutex arena_mutex;

public:
    GenericPool() {
        arena = static_cast<uint8_t*>(std::malloc(GENERIC_POOL_CAPACITY));
        if (!arena) {
            throw std::bad_alloc();
        }
        std::memset(arena, 0, GENERIC_POOL_CAPACITY);
        
        std::cout << "[BoundedAllocator] Initialized generic pool: " 
                  << GENERIC_POOL_CAPACITY << " bytes" << std::endl;
    }

    ~GenericPool() {
        if (arena) {
            std::free(arena);
            arena = nullptr;
        }
    }

    void* allocate(size_t size) {
        if (size == 0) size = 1;

        std::lock_guard<std::mutex> lock(arena_mutex);
        size_t current_offset = allocated_offset.load(std::memory_order_relaxed);
        
        if (current_offset + size > GENERIC_POOL_CAPACITY) {
            stats.recordFailure();
            return nullptr;
        }

        void* ptr = arena + current_offset;
        allocated_offset.fetch_add(size, std::memory_order_release);
        stats.recordAllocation(size);
        
        return ptr;
    }

    void deallocate(void*) {
        // Generic pool is not reused (arena-style)
    }

    void printStats() const { stats.print("Generic Pool"); }
    
    size_t getCurrentUsage() const {
        return stats.current_usage.load(std::memory_order_relaxed);
    }
};

// ============================================================================
// Global Allocator Manager (Singleton)
// ============================================================================

// Forward declare the guard variable
static thread_local bool g_in_allocator_init;

class BoundedArenaAllocator {
private:
    static bool initializing;
    
    FixedBlockPool<PROCESS_BLOCK_SIZE, PROCESS_POOL_CAPACITY>* process_pool;
    FixedBlockPool<EVENT_BLOCK_SIZE, EVENT_POOL_CAPACITY>* event_pool;
    FixedBlockPool<EDGE_BLOCK_SIZE, EDGE_POOL_CAPACITY>* edge_pool;
    GenericPool* generic_pool;

    BoundedArenaAllocator()
        : process_pool(nullptr), event_pool(nullptr), edge_pool(nullptr), generic_pool(nullptr)
    {
        std::cout << "\n[BoundedAllocator] ========== INITIALIZATION ==========" << std::endl;
        
        g_in_allocator_init = true;
        
        try {
            process_pool = new FixedBlockPool<PROCESS_BLOCK_SIZE, PROCESS_POOL_CAPACITY>("Process");
            event_pool = new FixedBlockPool<EVENT_BLOCK_SIZE, EVENT_POOL_CAPACITY>("Event");
            edge_pool = new FixedBlockPool<EDGE_BLOCK_SIZE, EDGE_POOL_CAPACITY>("Edge");
            generic_pool = new GenericPool();
            
            std::cout << "[BoundedAllocator] ========== READY ==========" << std::endl;
        } catch (const std::bad_alloc& e) {
            std::cerr << "[BoundedAllocator] FATAL: Failed to initialize pools!" << std::endl;
            throw;
        }
        
        g_in_allocator_init = false;
    }

public:
    ~BoundedArenaAllocator() {
        delete process_pool;
        delete event_pool;
        delete edge_pool;
        delete generic_pool;
    }

    static BoundedArenaAllocator& getInstance() {
        static BoundedArenaAllocator instance;
        return instance;
    }

    void* allocateProcess(size_t size) {
        if (size > PROCESS_BLOCK_SIZE) {
            std::cerr << "[BoundedAllocator] Process allocation too large: " << size << std::endl;
            return nullptr;
        }
        return process_pool->allocate();
    }

    void deallocateProcess(void* ptr) {
        process_pool->deallocate(ptr);
    }

    void* allocateEvent(size_t size) {
        if (size > EVENT_BLOCK_SIZE) {
            std::cerr << "[BoundedAllocator] Event allocation too large: " << size << std::endl;
            return nullptr;
        }
        return event_pool->allocate();
    }

    void deallocateEvent(void* ptr) {
        event_pool->deallocate(ptr);
    }

    void* allocateEdge(size_t size) {
        if (size > EDGE_BLOCK_SIZE) {
            std::cerr << "[BoundedAllocator] Edge allocation too large: " << size << std::endl;
            return nullptr;
        }
        return edge_pool->allocate();
    }

    void deallocateEdge(void* ptr) {
        edge_pool->deallocate(ptr);
    }

    void* allocateGeneric(size_t size) {
        return generic_pool->allocate(size);
    }

    void deallocateGeneric(void* ptr) {
        generic_pool->deallocate(ptr);
    }

    void printAllStats() const {
        std::cout << "\n[BoundedAllocator] ========== FINAL STATISTICS ==========" << std::endl;
        process_pool->printStats("Process Pool");
        event_pool->printStats("Event Pool");
        edge_pool->printStats("Edge Pool");
        generic_pool->printStats();
        std::cout << "[BoundedAllocator] ===================================" << std::endl << std::endl;
    }

    // Capacity queries
    size_t getProcessPoolCapacity() const { return process_pool->getCapacity(); }
    size_t getEventPoolCapacity() const { return event_pool->getCapacity(); }
    size_t getEdgePoolCapacity() const { return edge_pool->getCapacity(); }
    
    size_t getProcessPoolUsage() const {
        return process_pool->getCurrentUsage();
    }
    size_t getEventPoolUsage() const {
        return event_pool->getCurrentUsage();
    }
    size_t getEdgePoolUsage() const {
        return edge_pool->getCurrentUsage();
    }

    size_t getGenericPoolUsage() const {
        return generic_pool->getCurrentUsage();
    }
};

// Initialize static member
bool BoundedArenaAllocator::initializing = false;

// ============================================================================
// Global new/delete operators (routes through allocator)
// ============================================================================

void* operator new(size_t size) {
    if (g_in_allocator_init) {
        return std::malloc(size);
    }
    
    try {
        void* ptr = BoundedArenaAllocator::getInstance().allocateGeneric(size);
        if (!ptr) throw std::bad_alloc();
        return ptr;
    } catch (...) {
        throw std::bad_alloc();
    }
}

void operator delete(void* p) noexcept {
    if (p) {
        try {
            BoundedArenaAllocator::getInstance().deallocateGeneric(p);
        } catch (...) {
            // Silently ignore errors during delete
        }
    }
}

void operator delete(void* p, size_t) noexcept {
    if (p) {
        try {
            BoundedArenaAllocator::getInstance().deallocateGeneric(p);
        } catch (...) {
            // Silently ignore errors during delete
        }
    }
}

// ============================================================================
// Legacy MemoryManager (for compatibility)
// ============================================================================

class MemoryManager {
private:
    static std::atomic<size_t> manual_peak_rss;

    static void updatePeak(size_t current_rss) {
        size_t peak = manual_peak_rss.load(std::memory_order_relaxed);
        while (current_rss > peak && 
               !manual_peak_rss.compare_exchange_weak(peak, current_rss, std::memory_order_relaxed)) {
            peak = manual_peak_rss.load(std::memory_order_relaxed);
        }
    }

public:
    // Kernel pools only (process/event/edge).
    static size_t getUsedMemory() {
        auto& allocator = BoundedArenaAllocator::getInstance();
        return allocator.getProcessPoolUsage() +
               allocator.getEventPoolUsage() +
               allocator.getEdgePoolUsage();
    }

    // Total arena usage including the generic pool (used by global new).
    static size_t getTotalUsedMemory() {
        auto& allocator = BoundedArenaAllocator::getInstance();
        return getUsedMemory() + allocator.getGenericPoolUsage();
    }

    static size_t getGenericUsedMemory() {
        return BoundedArenaAllocator::getInstance().getGenericPoolUsage();
    }

    static size_t getSystemRSS() {
#if defined(__linux__)
        long rss = 0L;
        FILE* fp = NULL;
        if ((fp = fopen("/proc/self/statm", "r")) == NULL)
            return (size_t)0L;
        if (fscanf(fp, "%*s%ld", &rss) != 1) {
            fclose(fp);
            return (size_t)0L;
        }
        fclose(fp);
        size_t current_rss = (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
        updatePeak(current_rss);
        return current_rss;
#elif defined(__APPLE__)
        struct mach_task_basic_info info;
        mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                    (task_info_t)&info, &infoCount) != KERN_SUCCESS)
            return (size_t)0L;
        size_t current_rss = (size_t)info.resident_size;
        updatePeak(current_rss);
        return current_rss;
#elif defined(_WIN32)
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            size_t current_rss = (size_t)pmc.WorkingSetSize;
            updatePeak(current_rss);
            return current_rss;
        }
        return (size_t)0L;
#else
        return (size_t)0L;
#endif
    }

    static size_t getSystemPeakRSS() {
        return manual_peak_rss.load(std::memory_order_relaxed);
    }

    static void resetSystemPeak() {
        manual_peak_rss.store(getSystemRSS(), std::memory_order_relaxed);
    }

    static void fold() {
        std::cout << "[Metal] Memory Manager: Folding Entropy..." << std::endl;
        // In a real kernel, this would defragment memory
        // Here we just print stats
        BoundedArenaAllocator::getInstance().printAllStats();
    }

    static BoundedArenaAllocator& getAllocator() {
        return BoundedArenaAllocator::getInstance();
    }
};

std::atomic<size_t> MemoryManager::manual_peak_rss{0};
