#pragma once

#include "PageTable.h"
#include "PhysicalAllocator.h"
#include <iostream>

/**
 * Virtual Memory Allocator for Braided OS
 * 
 * Manages virtual address space for a process.
 * Integrates with page table and physical allocator.
 */

namespace os {

class VirtualAllocator {
private:
    PageTable* page_table_;
    PhysicalAllocator* phys_alloc_;
    
    // Virtual memory regions
    uint64_t heap_start_;
    uint64_t heap_end_;
    uint64_t heap_brk_;    // Current break (top of heap)
    
    uint64_t stack_start_;
    uint64_t stack_end_;
    
public:
    VirtualAllocator(PageTable* pt, PhysicalAllocator* pa)
        : page_table_(pt),
          phys_alloc_(pa),
          heap_start_(0x00000000'00400000ULL),   // 4MB
          heap_end_(0x00000000'40000000ULL),     // 1GB
          heap_brk_(0x00000000'00400000ULL),
          stack_start_(0x00007FFF'FFFF0000ULL),  // Near top of user space
          stack_end_(0x00007FFF'FFFFF000ULL) {
    }
    
    /**
     * Allocate virtual memory (like brk/sbrk).
     * 
     * @param size Number of bytes to allocate
     * @return Virtual address, or 0 if failed
     */
    uint64_t allocate(uint64_t size) {
        if (size == 0) return 0;
        
        // Align size to page boundary
        size = align_up(size);
        
        // Check if we have space
        if (heap_brk_ + size > heap_end_) {
            std::cerr << "[VirtualAllocator] Heap overflow!" << std::endl;
            return 0;
        }
        
        uint64_t virt_start = heap_brk_;
        uint64_t virt_end = heap_brk_ + size;
        
        // Allocate and map physical frames
        for (uint64_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
            // Allocate physical frame
            uint64_t phys = phys_alloc_->allocateFrame();
            if (phys == 0) {
                // Out of memory - unmap what we've allocated
                for (uint64_t v = virt_start; v < virt; v += PAGE_SIZE) {
                    uint64_t p = page_table_->translate(v);
                    if (p != 0) {
                        phys_alloc_->freeFrame(p);
                        page_table_->unmap(v);
                    }
                }
                return 0;
            }
            
            // Map virtual to physical
            if (!page_table_->map(virt, phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
                // Mapping failed - free frame
                phys_alloc_->freeFrame(phys);
                
                // Unmap what we've allocated
                for (uint64_t v = virt_start; v < virt; v += PAGE_SIZE) {
                    uint64_t p = page_table_->translate(v);
                    if (p != 0) {
                        phys_alloc_->freeFrame(p);
                        page_table_->unmap(v);
                    }
                }
                return 0;
            }
        }
        
        // Update break
        heap_brk_ = virt_end;
        
        return virt_start;
    }
    
    /**
     * Free virtual memory.
     * 
     * @param addr Virtual address
     * @param size Number of bytes to free
     */
    void free(uint64_t addr, uint64_t size) {
        if (size == 0) return;
        
        // Align to page boundaries
        uint64_t virt_start = align_down(addr);
        uint64_t virt_end = align_up(addr + size);
        
        // Unmap and free physical frames
        for (uint64_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
            uint64_t phys = page_table_->translate(virt);
            if (phys != 0) {
                phys_alloc_->freeFrame(phys);
                page_table_->unmap(virt);
            }
        }
    }
    
    /**
     * Set heap break (like brk syscall).
     * 
     * @param new_brk New break address (0 = query current)
     * @return Current or new break address, or 0 on error
     */
    uint64_t brk(uint64_t new_brk) {
        // Query current break
        if (new_brk == 0) {
            return heap_brk_;
        }
        
        // Check bounds
        if (new_brk < heap_start_ || new_brk > heap_end_) {
            return 0;  // Invalid
        }
        
        // Growing heap
        if (new_brk > heap_brk_) {
            uint64_t size = new_brk - heap_brk_;
            if (allocate(size) == 0) {
                return 0;  // Failed
            }
            return heap_brk_;
        }
        
        // Shrinking heap
        if (new_brk < heap_brk_) {
            uint64_t size = heap_brk_ - new_brk;
            free(new_brk, size);
            heap_brk_ = new_brk;
            return heap_brk_;
        }
        
        // No change
        return heap_brk_;
    }
    
    /**
     * Map memory (like mmap syscall).
     * 
     * @param addr Hint address (0 = let OS choose)
     * @param size Size in bytes
     * @param prot Protection flags
     * @return Virtual address, or 0 on error
     */
    uint64_t mmap(uint64_t addr, uint64_t size, uint64_t prot) {
        if (size == 0) return 0;
        
        // Align size
        size = align_up(size);
        
        // Choose address if not specified
        if (addr == 0) {
            addr = heap_brk_;
        }
        
        // Align address
        addr = align_down(addr);
        
        // Check bounds
        if (addr < heap_start_ || addr + size > heap_end_) {
            std::cerr << "[VirtualAllocator] mmap address out of range!" << std::endl;
            return 0;
        }
        
        // Convert protection flags to PTE flags
        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (prot & 0x02) {  // PROT_WRITE
            pte_flags |= PTE_WRITABLE;
        }
        
        // Allocate and map physical frames
        for (uint64_t virt = addr; virt < addr + size; virt += PAGE_SIZE) {
            // Allocate physical frame
            uint64_t phys = phys_alloc_->allocateFrame();
            if (phys == 0) {
                // Out of memory - unmap what we've allocated
                for (uint64_t v = addr; v < virt; v += PAGE_SIZE) {
                    uint64_t p = page_table_->translate(v);
                    if (p != 0) {
                        phys_alloc_->freeFrame(p);
                        page_table_->unmap(v);
                    }
                }
                return 0;
            }
            
            // Map virtual to physical
            if (!page_table_->map(virt, phys, pte_flags)) {
                phys_alloc_->freeFrame(phys);
                
                // Unmap what we've allocated
                for (uint64_t v = addr; v < virt; v += PAGE_SIZE) {
                    uint64_t p = page_table_->translate(v);
                    if (p != 0) {
                        phys_alloc_->freeFrame(p);
                        page_table_->unmap(v);
                    }
                }
                return 0;
            }
        }
        
        return addr;
    }
    
    /**
     * Unmap memory (like munmap syscall).
     * 
     * @param addr Virtual address
     * @param size Size in bytes
     */
    void munmap(uint64_t addr, uint64_t size) {
        free(addr, size);
    }
    
    /**
     * Change memory protection (like mprotect syscall).
     * 
     * @param addr Virtual address
     * @param size Size in bytes
     * @param prot New protection flags
     * @return true on success
     */
    bool mprotect(uint64_t addr, uint64_t size, uint64_t prot) {
        if (size == 0) return true;
        
        // Align to page boundaries
        uint64_t virt_start = align_down(addr);
        uint64_t virt_end = align_up(addr + size);
        
        // Convert protection flags to PTE flags
        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (prot & 0x02) {  // PROT_WRITE
            pte_flags |= PTE_WRITABLE;
        }
        
        // Update protection for each page
        for (uint64_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
            if (!page_table_->protect(virt, pte_flags)) {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Get heap bounds.
     */
    uint64_t getHeapStart() const { return heap_start_; }
    uint64_t getHeapEnd() const { return heap_end_; }
    uint64_t getHeapBrk() const { return heap_brk_; }
    
    /**
     * Print memory statistics.
     */
    void printStats() const {
        uint64_t heap_used = heap_brk_ - heap_start_;
        
        std::cout << "[VirtualAllocator] "
                  << "Heap: " << (heap_used / 1024) << " KB used, "
                  << ((heap_end_ - heap_brk_) / 1024 / 1024) << " MB available"
                  << std::endl;
        
        page_table_->printStats();
    }
};

} // namespace os
