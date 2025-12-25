#pragma once

#include "PageTable.h"
#include <cstdint>
#include <cstring>
#include <iostream>

/**
 * Physical Memory Allocator for Braided OS
 * 
 * Manages physical RAM frames (4KB each) using a bitmap.
 * O(1) allocation and deallocation.
 */

namespace os {

/**
 * Simple bitmap for tracking free/used frames.
 */
class Bitmap {
private:
    uint64_t* bits_;
    uint64_t size_;  // Number of bits
    
public:
    Bitmap(uint64_t size) : size_(size) {
        uint64_t num_words = (size + 63) / 64;
        bits_ = new uint64_t[num_words];
        std::memset(bits_, 0, num_words * sizeof(uint64_t));
    }
    
    ~Bitmap() {
        delete[] bits_;
    }
    
    bool get(uint64_t index) const {
        if (index >= size_) return false;
        uint64_t word = index / 64;
        uint64_t bit = index % 64;
        return (bits_[word] >> bit) & 1;
    }
    
    void set(uint64_t index) {
        if (index >= size_) return;
        uint64_t word = index / 64;
        uint64_t bit = index % 64;
        bits_[word] |= (1ULL << bit);
    }
    
    void clear(uint64_t index) {
        if (index >= size_) return;
        uint64_t word = index / 64;
        uint64_t bit = index % 64;
        bits_[word] &= ~(1ULL << bit);
    }
    
    // Find first zero bit (free frame)
    uint64_t findFirstZero() const {
        uint64_t num_words = (size_ + 63) / 64;
        
        for (uint64_t w = 0; w < num_words; w++) {
            if (bits_[w] != UINT64_MAX) {
                // This word has at least one zero bit
                for (uint64_t b = 0; b < 64; b++) {
                    uint64_t index = w * 64 + b;
                    if (index >= size_) return size_;
                    if (!get(index)) {
                        return index;
                    }
                }
            }
        }
        
        return size_;  // No free bit found
    }
};

/**
 * Physical Memory Allocator
 * 
 * Manages physical RAM frames using a bitmap.
 */
class PhysicalAllocator {
private:
    Bitmap* free_frames_;
    uint64_t total_frames_;
    uint64_t free_count_;
    uint64_t base_addr_;  // Base physical address
    
public:
    /**
     * Initialize with a range of physical memory.
     * 
     * @param base_addr Base physical address
     * @param size Total size in bytes
     */
    PhysicalAllocator(uint64_t base_addr, uint64_t size) 
        : base_addr_(base_addr),
          total_frames_(size / PAGE_SIZE),
          free_count_(size / PAGE_SIZE) {
        
        free_frames_ = new Bitmap(total_frames_);
        
        std::cout << "[PhysicalAllocator] Initialized with " 
                  << total_frames_ << " frames (" 
                  << (size / 1024 / 1024) << " MB)" << std::endl;
    }
    
    ~PhysicalAllocator() {
        delete free_frames_;
    }
    
    /**
     * Allocate a physical frame.
     * Returns physical address, or 0 if out of memory.
     */
    uint64_t allocateFrame() {
        if (free_count_ == 0) {
            std::cerr << "[PhysicalAllocator] Out of memory!" << std::endl;
            return 0;
        }
        
        // Find first free frame
        uint64_t frame_index = free_frames_->findFirstZero();
        if (frame_index >= total_frames_) {
            std::cerr << "[PhysicalAllocator] No free frames found!" << std::endl;
            return 0;
        }
        
        // Mark as used
        free_frames_->set(frame_index);
        free_count_--;
        
        // Return physical address
        return base_addr_ + (frame_index * PAGE_SIZE);
    }
    
    /**
     * Free a physical frame.
     */
    void freeFrame(uint64_t phys_addr) {
        if (phys_addr < base_addr_) {
            std::cerr << "[PhysicalAllocator] Invalid address: " << std::hex << phys_addr << std::dec << std::endl;
            return;
        }
        
        uint64_t frame_index = (phys_addr - base_addr_) / PAGE_SIZE;
        if (frame_index >= total_frames_) {
            std::cerr << "[PhysicalAllocator] Frame index out of range: " << frame_index << std::endl;
            return;
        }
        
        if (!free_frames_->get(frame_index)) {
            std::cerr << "[PhysicalAllocator] Double free detected: " << std::hex << phys_addr << std::dec << std::endl;
            return;
        }
        
        // Mark as free
        free_frames_->clear(frame_index);
        free_count_++;
    }
    
    /**
     * Get number of free frames.
     */
    uint64_t available() const {
        return free_count_;
    }
    
    /**
     * Get total number of frames.
     */
    uint64_t total() const {
        return total_frames_;
    }
    
    /**
     * Get memory usage statistics.
     */
    void printStats() const {
        uint64_t used = total_frames_ - free_count_;
        double usage_percent = 100.0 * used / total_frames_;
        
        std::cout << "[PhysicalAllocator] "
                  << "Used: " << used << " frames (" << (used * PAGE_SIZE / 1024 / 1024) << " MB), "
                  << "Free: " << free_count_ << " frames (" << (free_count_ * PAGE_SIZE / 1024 / 1024) << " MB), "
                  << "Usage: " << usage_percent << "%"
                  << std::endl;
    }
};

} // namespace os
