#pragma once
/**
 * Freestanding Virtual File System for ARQON Kernel
 * Phase 6.3: VFS layer, file operations, RAM filesystem
 */

#include <cstdint>

namespace vfs {

constexpr uint32_t MAX_FILES = 64;
constexpr uint32_t MAX_FDS = 32;
constexpr uint32_t MAX_FILE_SIZE = 4096;
constexpr uint32_t MAX_PATH = 128;

// Open flags
constexpr uint32_t O_RDONLY = 0;
constexpr uint32_t O_WRONLY = 1;
constexpr uint32_t O_RDWR   = 2;
constexpr uint32_t O_CREAT  = 0x40;
constexpr uint32_t O_TRUNC  = 0x200;
constexpr uint32_t O_APPEND = 0x400;

// Seek modes
constexpr int SEEK_SET = 0;
constexpr int SEEK_CUR = 1;
constexpr int SEEK_END = 2;

// File types
enum class FileType : uint8_t {
    REGULAR,
    DIRECTORY,
    DEVICE
};

/**
 * In-memory file structure
 */
struct File {
    char name[MAX_PATH];
    FileType type;
    uint32_t size;
    uint32_t mode;
    uint8_t data[MAX_FILE_SIZE];
    bool in_use;
    
    void init(const char* n, FileType t, uint32_t m) {
        for (int i = 0; i < MAX_PATH - 1 && n[i]; i++) {
            name[i] = n[i];
            name[i + 1] = '\0';
        }
        type = t;
        mode = m;
        size = 0;
        in_use = true;
    }
    
    int64_t read(uint8_t* buf, uint32_t offset, uint32_t count) {
        if (offset >= size) return 0;
        if (offset + count > size) count = size - offset;
        for (uint32_t i = 0; i < count; i++) {
            buf[i] = data[offset + i];
        }
        return count;
    }
    
    int64_t write(const uint8_t* buf, uint32_t offset, uint32_t count) {
        if (offset + count > MAX_FILE_SIZE) {
            count = MAX_FILE_SIZE - offset;
        }
        for (uint32_t i = 0; i < count; i++) {
            data[offset + i] = buf[i];
        }
        if (offset + count > size) {
            size = offset + count;
        }
        return count;
    }
};

/**
 * File descriptor
 */
struct FileDescriptor {
    File* file;
    uint32_t flags;
    uint32_t offset;
    bool in_use;
    
    bool is_readable() const { return (flags & 3) != O_WRONLY; }
    bool is_writable() const { return (flags & 3) != O_RDONLY; }
};

/**
 * RAM-based filesystem
 */
class RamFS {
private:
    File files_[MAX_FILES];
    FileDescriptor fds_[MAX_FDS];
    
    bool str_eq(const char* a, const char* b) const {
        while (*a && *b) {
            if (*a++ != *b++) return false;
        }
        return *a == *b;
    }

public:
    void init() {
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            files_[i].in_use = false;
        }
        for (uint32_t i = 0; i < MAX_FDS; i++) {
            fds_[i].in_use = false;
        }
        
        // Create root directory
        files_[0].init("/", FileType::DIRECTORY, 0755);
        
        // Create /dev directory
        files_[1].init("/dev", FileType::DIRECTORY, 0755);
        
        // Reserve stdin/stdout/stderr (fd 0, 1, 2)
        for (int i = 0; i < 3; i++) {
            fds_[i].in_use = true;
            fds_[i].file = nullptr;  // Console device
            fds_[i].flags = (i == 0) ? O_RDONLY : O_WRONLY;
            fds_[i].offset = 0;
        }
    }
    
    File* lookup(const char* path) {
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (files_[i].in_use && str_eq(files_[i].name, path)) {
                return &files_[i];
            }
        }
        return nullptr;
    }
    
    File* create(const char* path, uint32_t mode) {
        // Find free slot
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (!files_[i].in_use) {
                files_[i].init(path, FileType::REGULAR, mode);
                return &files_[i];
            }
        }
        return nullptr;
    }
    
    int32_t open(const char* path, uint32_t flags, uint32_t mode = 0644) {
        File* file = lookup(path);
        
        // Create if needed
        if (!file && (flags & O_CREAT)) {
            file = create(path, mode);
        }
        
        if (!file) return -1;
        
        // Truncate if requested
        if (flags & O_TRUNC) {
            file->size = 0;
        }
        
        // Allocate fd
        for (uint32_t i = 3; i < MAX_FDS; i++) {  // Skip 0,1,2
            if (!fds_[i].in_use) {
                fds_[i].in_use = true;
                fds_[i].file = file;
                fds_[i].flags = flags;
                fds_[i].offset = (flags & O_APPEND) ? file->size : 0;
                return i;
            }
        }
        
        return -1;  // No fd available
    }
    
    int64_t read(int32_t fd, void* buf, uint32_t count) {
        if (fd < 0 || fd >= MAX_FDS || !fds_[fd].in_use) return -1;
        
        FileDescriptor& desc = fds_[fd];
        
        // Console read (fd 0)
        if (!desc.file) {
            // Would read from keyboard - for now return 0
            return 0;
        }
        
        if (!desc.is_readable()) return -1;
        
        int64_t bytes = desc.file->read(
            reinterpret_cast<uint8_t*>(buf), desc.offset, count
        );
        if (bytes > 0) desc.offset += bytes;
        return bytes;
    }
    
    int64_t write(int32_t fd, const void* buf, uint32_t count) {
        if (fd < 0 || fd >= MAX_FDS || !fds_[fd].in_use) return -1;
        
        FileDescriptor& desc = fds_[fd];
        
        // Console write (fd 1, 2) - handled by caller
        if (!desc.file) {
            return count;  // Pretend success, actual output done by syscall
        }
        
        if (!desc.is_writable()) return -1;
        
        int64_t bytes = desc.file->write(
            reinterpret_cast<const uint8_t*>(buf), desc.offset, count
        );
        if (bytes > 0) desc.offset += bytes;
        return bytes;
    }
    
    int32_t close(int32_t fd) {
        if (fd < 3 || fd >= MAX_FDS) return -1;  // Can't close stdin/out/err
        if (!fds_[fd].in_use) return -1;
        
        fds_[fd].in_use = false;
        fds_[fd].file = nullptr;
        return 0;
    }
    
    int64_t lseek(int32_t fd, int64_t offset, int whence) {
        if (fd < 0 || fd >= MAX_FDS || !fds_[fd].in_use) return -1;
        if (!fds_[fd].file) return -1;  // Can't seek console
        
        FileDescriptor& desc = fds_[fd];
        int64_t new_off = 0;
        
        switch (whence) {
            case SEEK_SET: new_off = offset; break;
            case SEEK_CUR: new_off = desc.offset + offset; break;
            case SEEK_END: new_off = desc.file->size + offset; break;
            default: return -1;
        }
        
        if (new_off < 0) new_off = 0;
        desc.offset = new_off;
        return new_off;
    }
    
    bool is_console_fd(int32_t fd) const {
        return fd >= 0 && fd < 3;
    }
    
    uint32_t file_count() const {
        uint32_t count = 0;
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (files_[i].in_use) count++;
        }
        return count;
    }
};

// Global filesystem
extern RamFS g_fs;

} // namespace vfs
