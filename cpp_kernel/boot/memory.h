#pragma once
/**
 * Freestanding Memory Management for ARQON Kernel
 * Phase 6.2: Physical allocator, page tables, virtual memory
 */

#include <cstdint>

namespace mem {

constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t PAGE_SHIFT = 12;

// Page table entry flags (x86-64)
constexpr uint64_t PTE_PRESENT  = 1ULL << 0;
constexpr uint64_t PTE_WRITABLE = 1ULL << 1;
constexpr uint64_t PTE_USER     = 1ULL << 2;
constexpr uint64_t PTE_HUGE     = 1ULL << 7;

inline uint64_t align_up(uint64_t addr, uint64_t align) {
    return (addr + align - 1) & ~(align - 1);
}

inline uint64_t align_down(uint64_t addr, uint64_t align) {
    return addr & ~(align - 1);
}

/**
 * Simple bitmap-based physical frame allocator
 * Manages physical memory in 4KB frames
 */
class PhysicalAllocator {
private:
    static constexpr uint64_t MAX_FRAMES = 32768;  // 128MB max
    uint64_t bitmap_[MAX_FRAMES / 64];
    uint64_t total_frames_;
    uint64_t free_frames_;
    uint64_t base_addr_;
    
    void set_bit(uint64_t frame) {
        bitmap_[frame / 64] |= (1ULL << (frame % 64));
    }
    
    void clear_bit(uint64_t frame) {
        bitmap_[frame / 64] &= ~(1ULL << (frame % 64));
    }
    
    bool get_bit(uint64_t frame) const {
        return (bitmap_[frame / 64] >> (frame % 64)) & 1;
    }

public:
    void init(uint64_t base, uint64_t size) {
        base_addr_ = base;
        total_frames_ = size / PAGE_SIZE;
        if (total_frames_ > MAX_FRAMES) total_frames_ = MAX_FRAMES;
        free_frames_ = total_frames_;
        
        // Clear bitmap (all frames free)
        for (uint64_t i = 0; i < MAX_FRAMES / 64; i++) {
            bitmap_[i] = 0;
        }
    }
    
    // Reserve frames (mark as used) - for kernel/reserved regions
    void reserve(uint64_t phys_addr, uint64_t size) {
        uint64_t start_frame = (phys_addr - base_addr_) / PAGE_SIZE;
        uint64_t num_frames = align_up(size, PAGE_SIZE) / PAGE_SIZE;
        
        for (uint64_t i = 0; i < num_frames && (start_frame + i) < total_frames_; i++) {
            if (!get_bit(start_frame + i)) {
                set_bit(start_frame + i);
                free_frames_--;
            }
        }
    }
    
    // Allocate a single frame
    uint64_t alloc_frame() {
        if (free_frames_ == 0) return 0;
        
        for (uint64_t i = 0; i < total_frames_ / 64; i++) {
            if (bitmap_[i] != UINT64_MAX) {
                for (uint64_t b = 0; b < 64; b++) {
                    uint64_t frame = i * 64 + b;
                    if (frame >= total_frames_) return 0;
                    if (!get_bit(frame)) {
                        set_bit(frame);
                        free_frames_--;
                        return base_addr_ + frame * PAGE_SIZE;
                    }
                }
            }
        }
        return 0;
    }
    
    // Free a frame
    void free_frame(uint64_t phys_addr) {
        if (phys_addr < base_addr_) return;
        uint64_t frame = (phys_addr - base_addr_) / PAGE_SIZE;
        if (frame >= total_frames_) return;
        if (get_bit(frame)) {
            clear_bit(frame);
            free_frames_++;
        }
    }
    
    uint64_t available() const { return free_frames_; }
    uint64_t total() const { return total_frames_; }
};

/**
 * Simple heap allocator using a free list
 * For kernel dynamic allocation (kmalloc/kfree)
 */
class HeapAllocator {
private:
    struct Block {
        uint64_t size;
        Block* next;
        bool free;
    };
    
    uint8_t* heap_start_;
    uint8_t* heap_end_;
    Block* free_list_;
    
public:
    void init(uint64_t start, uint64_t size) {
        heap_start_ = reinterpret_cast<uint8_t*>(start);
        heap_end_ = heap_start_ + size;
        
        // Initialize with one big free block
        free_list_ = reinterpret_cast<Block*>(heap_start_);
        free_list_->size = size - sizeof(Block);
        free_list_->next = nullptr;
        free_list_->free = true;
    }
    
    void* alloc(uint64_t size) {
        // Align size to 16 bytes
        size = align_up(size, 16);
        
        Block* prev = nullptr;
        Block* curr = free_list_;
        
        while (curr) {
            if (curr->free && curr->size >= size) {
                // Split block if large enough
                if (curr->size >= size + sizeof(Block) + 16) {
                    Block* new_block = reinterpret_cast<Block*>(
                        reinterpret_cast<uint8_t*>(curr) + sizeof(Block) + size
                    );
                    new_block->size = curr->size - size - sizeof(Block);
                    new_block->next = curr->next;
                    new_block->free = true;
                    
                    curr->size = size;
                    curr->next = new_block;
                }
                
                curr->free = false;
                return reinterpret_cast<uint8_t*>(curr) + sizeof(Block);
            }
            
            prev = curr;
            curr = curr->next;
        }
        
        return nullptr;  // Out of memory
    }
    
    void free(void* ptr) {
        if (!ptr) return;
        
        Block* block = reinterpret_cast<Block*>(
            reinterpret_cast<uint8_t*>(ptr) - sizeof(Block)
        );
        block->free = true;
        
        // Coalesce adjacent free blocks
        Block* curr = free_list_;
        while (curr && curr->next) {
            if (curr->free && curr->next->free) {
                curr->size += sizeof(Block) + curr->next->size;
                curr->next = curr->next->next;
            } else {
                curr = curr->next;
            }
        }
    }
};

// Global allocators
extern PhysicalAllocator g_phys_alloc;
extern HeapAllocator g_heap;

inline void* kmalloc(uint64_t size) {
    return g_heap.alloc(size);
}

inline void kfree(void* ptr) {
    g_heap.free(ptr);
}

} // namespace mem
