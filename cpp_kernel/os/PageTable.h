#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>

/**
 * Page Table Implementation for Braided OS
 * 
 * Two-level page table with 4KB pages.
 * Virtual address space: 4GB per process
 */

namespace os {

// Page size: 4KB
constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t PAGE_SHIFT = 12;  // log2(4096)

// Page table dimensions
constexpr uint32_t L1_ENTRIES = 1024;  // 10 bits
constexpr uint32_t L2_ENTRIES = 1024;  // 10 bits

// Page table entry flags
constexpr uint64_t PTE_PRESENT   = (1ULL << 0);  // Page is in memory
constexpr uint64_t PTE_WRITABLE  = (1ULL << 1);  // Page is writable
constexpr uint64_t PTE_USER      = (1ULL << 2);  // User mode can access
constexpr uint64_t PTE_ACCESSED  = (1ULL << 3);  // Page was accessed
constexpr uint64_t PTE_DIRTY     = (1ULL << 4);  // Page was written to

// Extract page table indices from virtual address
inline uint32_t get_l1_index(uint64_t virt_addr) {
    return (virt_addr >> 22) & 0x3FF;  // Bits 22-31
}

inline uint32_t get_l2_index(uint64_t virt_addr) {
    return (virt_addr >> 12) & 0x3FF;  // Bits 12-21
}

inline uint32_t get_page_offset(uint64_t virt_addr) {
    return virt_addr & 0xFFF;  // Bits 0-11
}

// Align address to page boundary
inline uint64_t align_down(uint64_t addr) {
    return addr & ~(PAGE_SIZE - 1);
}

inline uint64_t align_up(uint64_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/**
 * Page Table Entry (PTE)
 * 
 * 64-bit entry with flags and physical frame number.
 */
struct PageTableEntry {
    uint64_t value;
    
    PageTableEntry() : value(0) {}
    
    explicit PageTableEntry(uint64_t v) : value(v) {}
    
    // Flag accessors
    bool isPresent() const { return value & PTE_PRESENT; }
    bool isWritable() const { return value & PTE_WRITABLE; }
    bool isUser() const { return value & PTE_USER; }
    bool isAccessed() const { return value & PTE_ACCESSED; }
    bool isDirty() const { return value & PTE_DIRTY; }
    
    // Flag setters
    void setPresent(bool p) {
        if (p) value |= PTE_PRESENT;
        else value &= ~PTE_PRESENT;
    }
    
    void setWritable(bool w) {
        if (w) value |= PTE_WRITABLE;
        else value &= ~PTE_WRITABLE;
    }
    
    void setUser(bool u) {
        if (u) value |= PTE_USER;
        else value &= ~PTE_USER;
    }
    
    void setAccessed(bool a) {
        if (a) value |= PTE_ACCESSED;
        else value &= ~PTE_ACCESSED;
    }
    
    void setDirty(bool d) {
        if (d) value |= PTE_DIRTY;
        else value &= ~PTE_DIRTY;
    }
    
    // Physical frame number (bits 12-51)
    uint64_t getFrame() const {
        return (value >> 12) & 0xFFFFFFFFFULL;
    }
    
    void setFrame(uint64_t frame) {
        value = (value & 0xFFF) | (frame << 12);
    }
    
    // Physical address
    uint64_t getPhysAddr() const {
        return getFrame() << PAGE_SHIFT;
    }
    
    void setPhysAddr(uint64_t phys_addr) {
        setFrame(phys_addr >> PAGE_SHIFT);
    }
};

/**
 * Level 2 Page Table
 * 
 * 1024 entries, each mapping a 4KB page.
 * Covers 4MB of virtual address space.
 */
struct L2PageTable {
    PageTableEntry entries[L2_ENTRIES];
    
    L2PageTable() {
        std::memset(entries, 0, sizeof(entries));
    }
    
    PageTableEntry& operator[](uint32_t index) {
        return entries[index];
    }
    
    const PageTableEntry& operator[](uint32_t index) const {
        return entries[index];
    }
};

/**
 * Level 1 Page Table (Page Directory)
 * 
 * 1024 entries, each pointing to an L2 page table.
 * Covers 4GB of virtual address space.
 */
class PageTable {
private:
    L2PageTable* l2_tables_[L1_ENTRIES];
    
public:
    PageTable() {
        std::memset(l2_tables_, 0, sizeof(l2_tables_));
    }
    
    ~PageTable() {
        // Free all L2 tables
        for (uint32_t i = 0; i < L1_ENTRIES; i++) {
            if (l2_tables_[i]) {
                delete l2_tables_[i];
            }
        }
    }
    
    /**
     * Map a virtual address to a physical address.
     */
    bool map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags = PTE_PRESENT | PTE_WRITABLE | PTE_USER) {
        uint32_t l1_idx = get_l1_index(virt_addr);
        uint32_t l2_idx = get_l2_index(virt_addr);
        
        // Allocate L2 table if needed
        if (!l2_tables_[l1_idx]) {
            l2_tables_[l1_idx] = new L2PageTable();
            if (!l2_tables_[l1_idx]) {
                return false;  // Out of memory
            }
        }
        
        // Set page table entry
        PageTableEntry& pte = (*l2_tables_[l1_idx])[l2_idx];
        pte.setPhysAddr(phys_addr);
        pte.value |= flags;
        
        return true;
    }
    
    /**
     * Unmap a virtual address.
     */
    void unmap(uint64_t virt_addr) {
        uint32_t l1_idx = get_l1_index(virt_addr);
        uint32_t l2_idx = get_l2_index(virt_addr);
        
        if (!l2_tables_[l1_idx]) {
            return;  // Not mapped
        }
        
        // Clear page table entry
        (*l2_tables_[l1_idx])[l2_idx].value = 0;
    }
    
    /**
     * Translate virtual address to physical address.
     * Returns 0 if not mapped.
     */
    uint64_t translate(uint64_t virt_addr) const {
        uint32_t l1_idx = get_l1_index(virt_addr);
        uint32_t l2_idx = get_l2_index(virt_addr);
        uint32_t offset = get_page_offset(virt_addr);
        
        if (!l2_tables_[l1_idx]) {
            return 0;  // Not mapped
        }
        
        const PageTableEntry& pte = (*l2_tables_[l1_idx])[l2_idx];
        if (!pte.isPresent()) {
            return 0;  // Not present
        }
        
        return pte.getPhysAddr() + offset;
    }
    
    /**
     * Get page table entry for a virtual address.
     * Returns nullptr if not mapped.
     */
    PageTableEntry* getPTE(uint64_t virt_addr) {
        uint32_t l1_idx = get_l1_index(virt_addr);
        uint32_t l2_idx = get_l2_index(virt_addr);
        
        if (!l2_tables_[l1_idx]) {
            return nullptr;
        }
        
        return &(*l2_tables_[l1_idx])[l2_idx];
    }
    
    /**
     * Check if a virtual address is mapped.
     */
    bool isMapped(uint64_t virt_addr) const {
        uint32_t l1_idx = get_l1_index(virt_addr);
        uint32_t l2_idx = get_l2_index(virt_addr);
        
        if (!l2_tables_[l1_idx]) {
            return false;
        }
        
        return (*l2_tables_[l1_idx])[l2_idx].isPresent();
    }
    
    /**
     * Change protection flags for a virtual address.
     */
    bool protect(uint64_t virt_addr, uint64_t flags) {
        PageTableEntry* pte = getPTE(virt_addr);
        if (!pte || !pte->isPresent()) {
            return false;
        }
        
        // Preserve frame, update flags
        uint64_t frame = pte->getFrame();
        pte->value = (frame << 12) | flags | PTE_PRESENT;
        
        return true;
    }
    
    /**
     * Copy page table (for fork).
     */
    PageTable* clone() const {
        PageTable* new_pt = new PageTable();
        if (!new_pt) {
            return nullptr;
        }
        
        // Copy all L2 tables
        for (uint32_t i = 0; i < L1_ENTRIES; i++) {
            if (l2_tables_[i]) {
                new_pt->l2_tables_[i] = new L2PageTable();
                if (!new_pt->l2_tables_[i]) {
                    delete new_pt;
                    return nullptr;
                }
                
                // Copy entries
                std::memcpy(new_pt->l2_tables_[i], l2_tables_[i], sizeof(L2PageTable));
            }
        }
        
        return new_pt;
    }
    
    /**
     * Print page table statistics.
     */
    void printStats() const {
        uint32_t l2_count = 0;
        uint32_t mapped_pages = 0;
        
        for (uint32_t i = 0; i < L1_ENTRIES; i++) {
            if (l2_tables_[i]) {
                l2_count++;
                
                for (uint32_t j = 0; j < L2_ENTRIES; j++) {
                    if ((*l2_tables_[i])[j].isPresent()) {
                        mapped_pages++;
                    }
                }
            }
        }
        
        std::cout << "[PageTable] L2 tables: " << l2_count 
                  << ", Mapped pages: " << mapped_pages
                  << ", Memory used: " << (mapped_pages * PAGE_SIZE / 1024) << " KB"
                  << std::endl;
    }
};

} // namespace os
