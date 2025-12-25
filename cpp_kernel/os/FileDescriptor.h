#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>

/**
 * File Descriptor Management for Braided OS
 * 
 * Per-process file descriptor table.
 */

namespace os {

// File descriptor flags
constexpr uint32_t O_RDONLY  = 0x0000;
constexpr uint32_t O_WRONLY  = 0x0001;
constexpr uint32_t O_RDWR    = 0x0002;
constexpr uint32_t O_CREAT   = 0x0040;
constexpr uint32_t O_TRUNC   = 0x0200;
constexpr uint32_t O_APPEND  = 0x0400;

// Seek whence (use system defines from unistd.h)
// SEEK_SET, SEEK_CUR, SEEK_END are already defined

// Forward declaration
struct MemFSFile;

/**
 * File Descriptor
 * 
 * Represents an open file in a process.
 */
struct FileDescriptor {
    int32_t fd;                // File descriptor number
    MemFSFile* file;           // Pointer to file
    uint64_t offset;           // Current read/write position
    uint32_t flags;            // Open flags (O_RDONLY, O_WRONLY, etc.)
    uint32_t ref_count;        // Reference count (for dup)
    bool in_use;               // Is this FD allocated?
    
    FileDescriptor() 
        : fd(-1), file(nullptr), offset(0), flags(0), ref_count(0), in_use(false) {}
    
    bool isReadable() const {
        return (flags & O_RDWR) || (flags & O_RDONLY) == O_RDONLY;
    }
    
    bool isWritable() const {
        return (flags & O_RDWR) || (flags & O_WRONLY);
    }
};

/**
 * File Descriptor Table
 * 
 * Per-process table of open files.
 */
class FileDescriptorTable {
private:
    static constexpr uint32_t MAX_FDS = 1024;
    FileDescriptor fds_[MAX_FDS];
    
public:
    FileDescriptorTable() {
        // Initialize all FDs as unused
        for (uint32_t i = 0; i < MAX_FDS; i++) {
            fds_[i].fd = i;
            fds_[i].in_use = false;
        }
        
        // Reserve FDs 0, 1, 2 for stdin, stdout, stderr
        fds_[0].in_use = true;  // stdin
        fds_[1].in_use = true;  // stdout
        fds_[2].in_use = true;  // stderr
    }
    
    /**
     * Allocate a new file descriptor.
     * Returns FD number, or -1 if no FDs available.
     */
    int32_t allocate(MemFSFile* file, uint32_t flags) {
        // Find first free FD (skip 0, 1, 2)
        for (uint32_t i = 3; i < MAX_FDS; i++) {
            if (!fds_[i].in_use) {
                fds_[i].file = file;
                fds_[i].offset = 0;
                fds_[i].flags = flags;
                fds_[i].ref_count = 1;
                fds_[i].in_use = true;
                return i;
            }
        }
        
        std::cerr << "[FileDescriptorTable] No free FDs!" << std::endl;
        return -1;
    }
    
    /**
     * Free a file descriptor.
     */
    void free(int32_t fd) {
        if (fd < 0 || fd >= MAX_FDS) {
            std::cerr << "[FileDescriptorTable] Invalid FD: " << fd << std::endl;
            return;
        }
        
        if (fd <= 2) {
            std::cerr << "[FileDescriptorTable] Cannot close stdin/stdout/stderr!" << std::endl;
            return;
        }
        
        if (!fds_[fd].in_use) {
            std::cerr << "[FileDescriptorTable] FD not in use: " << fd << std::endl;
            return;
        }
        
        fds_[fd].ref_count--;
        if (fds_[fd].ref_count == 0) {
            fds_[fd].file = nullptr;
            fds_[fd].offset = 0;
            fds_[fd].flags = 0;
            fds_[fd].in_use = false;
        }
    }
    
    /**
     * Get file descriptor by number.
     * Returns nullptr if invalid or not in use.
     */
    FileDescriptor* get(int32_t fd) {
        if (fd < 0 || fd >= MAX_FDS) {
            return nullptr;
        }
        
        if (!fds_[fd].in_use) {
            return nullptr;
        }
        
        return &fds_[fd];
    }
    
    /**
     * Duplicate a file descriptor (like dup()).
     */
    int32_t duplicate(int32_t old_fd) {
        FileDescriptor* old_desc = get(old_fd);
        if (!old_desc) {
            return -1;
        }
        
        // Find free FD
        for (uint32_t i = 3; i < MAX_FDS; i++) {
            if (!fds_[i].in_use) {
                fds_[i] = *old_desc;
                fds_[i].fd = i;
                fds_[i].ref_count = 1;
                old_desc->ref_count++;
                return i;
            }
        }
        
        return -1;
    }
    
    /**
     * Get number of open file descriptors.
     */
    uint32_t count() const {
        uint32_t c = 0;
        for (uint32_t i = 0; i < MAX_FDS; i++) {
            if (fds_[i].in_use) {
                c++;
            }
        }
        return c;
    }
    
    /**
     * Print FD table statistics.
     */
    void printStats() const {
        uint32_t open_fds = count();
        std::cout << "[FileDescriptorTable] Open FDs: " << open_fds 
                  << " / " << MAX_FDS << std::endl;
    }
};

} // namespace os
