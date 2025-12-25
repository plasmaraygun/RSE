#pragma once

#include "FileDescriptor.h"
#include "MemFS.h"
#include <cstdint>
#include <iostream>

/**
 * Virtual File System (VFS) for Braided OS
 * 
 * Provides unified interface for file operations.
 */

namespace os {

class VFS {
private:
    MemFS* fs_;
    FileDescriptorTable* fd_table_;
    
public:
    VFS(MemFS* fs, FileDescriptorTable* fd_table) 
        : fs_(fs), fd_table_(fd_table) {}
    
    /**
     * Open a file.
     * 
     * @param path File path
     * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND)
     * @param mode Permissions (if creating)
     * @return File descriptor, or -1 on error
     */
    int32_t open(const char* path, uint32_t flags, uint32_t mode = 0644) {
        // Look up file
        MemFSFile* file = fs_->lookup(path);
        
        // Create if doesn't exist and O_CREAT is set
        if (!file && (flags & O_CREAT)) {
            file = fs_->create(path, mode);
            if (!file) {
                std::cerr << "[VFS] Failed to create file: " << path << std::endl;
                return -1;
            }
        }
        
        // File not found
        if (!file) {
            std::cerr << "[VFS] File not found: " << path << std::endl;
            return -1;
        }
        
        // Truncate if O_TRUNC is set
        if (flags & O_TRUNC) {
            file->truncate();
        }
        
        // Allocate file descriptor
        int32_t fd = fd_table_->allocate(file, flags);
        if (fd < 0) {
            std::cerr << "[VFS] Failed to allocate FD" << std::endl;
            return -1;
        }
        
        // If O_APPEND, set offset to end
        if (flags & O_APPEND) {
            FileDescriptor* desc = fd_table_->get(fd);
            if (desc) {
                desc->offset = file->size;
            }
        }
        
        return fd;
    }
    
    /**
     * Read from a file.
     * 
     * @param fd File descriptor
     * @param buf Buffer to read into
     * @param count Number of bytes to read
     * @return Number of bytes read, or -1 on error
     */
    int64_t read(int32_t fd, void* buf, uint32_t count) {
        // Get file descriptor
        FileDescriptor* desc = fd_table_->get(fd);
        if (!desc) {
            std::cerr << "[VFS] Invalid FD: " << fd << std::endl;
            return -1;
        }
        
        // Check if readable
        if (!desc->isReadable()) {
            std::cerr << "[VFS] FD not readable: " << fd << std::endl;
            return -1;
        }
        
        // Read from file
        int64_t bytes_read = desc->file->read((uint8_t*)buf, desc->offset, count);
        if (bytes_read < 0) {
            return -1;
        }
        
        // Update offset
        desc->offset += bytes_read;
        
        return bytes_read;
    }
    
    /**
     * Write to a file.
     * 
     * @param fd File descriptor
     * @param buf Buffer to write from
     * @param count Number of bytes to write
     * @return Number of bytes written, or -1 on error
     */
    int64_t write(int32_t fd, const void* buf, uint32_t count) {
        // Get file descriptor
        FileDescriptor* desc = fd_table_->get(fd);
        if (!desc) {
            std::cerr << "[VFS] Invalid FD: " << fd << std::endl;
            return -1;
        }
        
        // Check if writable
        if (!desc->isWritable()) {
            std::cerr << "[VFS] FD not writable: " << fd << std::endl;
            return -1;
        }
        
        // Write to file
        int64_t bytes_written = desc->file->write((const uint8_t*)buf, desc->offset, count);
        if (bytes_written < 0) {
            return -1;
        }
        
        // Update offset
        desc->offset += bytes_written;
        
        return bytes_written;
    }
    
    /**
     * Close a file descriptor.
     * 
     * @param fd File descriptor
     * @return 0 on success, -1 on error
     */
    int32_t close(int32_t fd) {
        FileDescriptor* desc = fd_table_->get(fd);
        if (!desc) {
            std::cerr << "[VFS] Invalid FD: " << fd << std::endl;
            return -1;
        }
        
        fd_table_->free(fd);
        
        return 0;
    }
    
    /**
     * Seek to a position in a file.
     * 
     * @param fd File descriptor
     * @param offset Offset to seek to
     * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
     * @return New offset, or -1 on error
     */
    int64_t lseek(int32_t fd, int64_t offset, int whence) {
        FileDescriptor* desc = fd_table_->get(fd);
        if (!desc) {
            std::cerr << "[VFS] Invalid FD: " << fd << std::endl;
            return -1;
        }
        
        int64_t new_offset = 0;
        
        switch (whence) {
            case SEEK_SET:
                new_offset = offset;
                break;
            
            case SEEK_CUR:
                new_offset = desc->offset + offset;
                break;
            
            case SEEK_END:
                new_offset = desc->file->size + offset;
                break;
            
            default:
                std::cerr << "[VFS] Invalid whence: " << whence << std::endl;
                return -1;
        }
        
        // Check bounds
        if (new_offset < 0) {
            new_offset = 0;
        }
        
        desc->offset = new_offset;
        
        return new_offset;
    }
    
    /**
     * Delete a file.
     * 
     * @param path File path
     * @return 0 on success, -1 on error
     */
    int32_t unlink(const char* path) {
        if (fs_->remove(path)) {
            return 0;
        }
        return -1;
    }
    
    /**
     * Print VFS statistics.
     */
    void printStats() const {
        fs_->printStats();
        fd_table_->printStats();
    }
};

} // namespace os
