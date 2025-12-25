#pragma once

#include "FileDescriptor.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>

/**
 * Simple In-Memory File System (MemFS)
 * 
 * Files stored in RAM, no persistence.
 * Flat namespace (no directories).
 */

namespace os {

/**
 * In-memory file.
 */
struct MemFSFile {
    char name[256];            // File name
    uint8_t* data;             // File contents
    uint32_t size;             // Current size
    uint32_t capacity;         // Allocated capacity
    uint32_t mode;             // Permissions (not enforced yet)
    bool in_use;               // Is this file slot used?
    
    MemFSFile() 
        : data(nullptr), size(0), capacity(0), mode(0644), in_use(false) {
        name[0] = '\0';
    }
    
    ~MemFSFile() {
        if (data) {
            free(data);
            data = nullptr;
        }
    }
    
    /**
     * Ensure capacity for at least `new_size` bytes.
     */
    bool ensureCapacity(uint32_t new_size) {
        if (new_size <= capacity) {
            return true;
        }
        
        // Round up to next power of 2
        uint32_t new_capacity = capacity;
        if (new_capacity == 0) {
            new_capacity = 4096;  // Start with 4KB
        }
        
        while (new_capacity < new_size) {
            new_capacity *= 2;
        }
        
        // Reallocate
        uint8_t* new_data = (uint8_t*)realloc(data, new_capacity);
        if (!new_data) {
            std::cerr << "[MemFSFile] Out of memory!" << std::endl;
            return false;
        }
        
        data = new_data;
        capacity = new_capacity;
        
        return true;
    }
    
    /**
     * Read from file at offset.
     */
    int64_t read(uint8_t* buf, uint64_t offset, uint32_t count) {
        if (offset >= size) {
            return 0;  // EOF
        }
        
        uint32_t available = size - offset;
        uint32_t to_read = (count < available) ? count : available;
        
        std::memcpy(buf, data + offset, to_read);
        
        return to_read;
    }
    
    /**
     * Write to file at offset.
     */
    int64_t write(const uint8_t* buf, uint64_t offset, uint32_t count) {
        // Ensure capacity
        uint32_t new_size = offset + count;
        if (!ensureCapacity(new_size)) {
            return -1;  // Out of memory
        }
        
        // Write data
        std::memcpy(data + offset, buf, count);
        
        // Update size if we wrote past end
        if (new_size > size) {
            size = new_size;
        }
        
        return count;
    }
    
    /**
     * Truncate file to zero length.
     */
    void truncate() {
        size = 0;
    }
};

/**
 * In-memory file system.
 */
class MemFS {
private:
    static constexpr uint32_t MAX_FILES = 1024;
    MemFSFile files_[MAX_FILES];
    uint32_t num_files_;
    
public:
    MemFS() : num_files_(0) {}
    
    /**
     * Create a new file.
     * Returns pointer to file, or nullptr if failed.
     */
    MemFSFile* create(const char* name, uint32_t mode) {
        // Check if file already exists
        MemFSFile* existing = lookup(name);
        if (existing) {
            return existing;  // Already exists
        }
        
        // Find free slot
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (!files_[i].in_use) {
                strncpy(files_[i].name, name, sizeof(files_[i].name) - 1);
                files_[i].name[sizeof(files_[i].name) - 1] = '\0';
                files_[i].mode = mode;
                files_[i].in_use = true;
                files_[i].size = 0;
                files_[i].capacity = 0;
                files_[i].data = nullptr;
                num_files_++;
                
                std::cout << "[MemFS] Created file: " << name << std::endl;
                
                return &files_[i];
            }
        }
        
        std::cerr << "[MemFS] No free file slots!" << std::endl;
        return nullptr;
    }
    
    /**
     * Look up a file by name.
     * Returns pointer to file, or nullptr if not found.
     */
    MemFSFile* lookup(const char* name) {
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (files_[i].in_use && strcmp(files_[i].name, name) == 0) {
                return &files_[i];
            }
        }
        
        return nullptr;
    }
    
    /**
     * Delete a file.
     */
    bool remove(const char* name) {
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (files_[i].in_use && strcmp(files_[i].name, name) == 0) {
                if (files_[i].data) {
                    free(files_[i].data);
                    files_[i].data = nullptr;
                }
                files_[i].in_use = false;
                files_[i].size = 0;
                files_[i].capacity = 0;
                num_files_--;
                
                std::cout << "[MemFS] Deleted file: " << name << std::endl;
                
                return true;
            }
        }
        
        std::cerr << "[MemFS] File not found: " << name << std::endl;
        return false;
    }
    
    /**
     * List all files.
     */
    void list() const {
        std::cout << "[MemFS] Files (" << num_files_ << "):" << std::endl;
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (files_[i].in_use) {
                std::cout << "  " << files_[i].name 
                          << " (" << files_[i].size << " bytes)" << std::endl;
            }
        }
    }
    
    /**
     * Get statistics.
     */
    void printStats() const {
        uint64_t total_size = 0;
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (files_[i].in_use) {
                total_size += files_[i].size;
            }
        }
        
        std::cout << "[MemFS] Files: " << num_files_ << " / " << MAX_FILES
                  << ", Total size: " << (total_size / 1024) << " KB"
                  << std::endl;
    }
};

} // namespace os
