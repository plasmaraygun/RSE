# Virtual File System (VFS) Design

**Goal**: Provide a unified interface for file operations across different file systems.

---

## Overview

The VFS (Virtual File System) is the abstraction layer that allows applications to work with files without knowing the underlying file system implementation.

Key responsibilities:
1. **File operations** - open, read, write, close, seek
2. **Directory operations** - mkdir, rmdir, readdir
3. **Path resolution** - Convert paths to inodes
4. **File descriptors** - Per-process file descriptor table
5. **Caching** - Cache frequently accessed files/directories

---

## Key Concepts

### **Inode (Index Node)**

An **inode** represents a file or directory in the file system:

```cpp
struct Inode {
    uint32_t inode_number;     // Unique identifier
    uint32_t type;             // File, directory, symlink, etc.
    uint32_t mode;             // Permissions (rwxrwxrwx)
    uint32_t size;             // File size in bytes
    uint32_t blocks;           // Number of blocks allocated
    uint64_t atime;            // Last access time
    uint64_t mtime;            // Last modification time
    uint64_t ctime;            // Last status change time
    uint32_t links;            // Number of hard links
    void* data;                // File system specific data
};
```

### **File Descriptor**

A **file descriptor** is a per-process handle to an open file:

```cpp
struct FileDescriptor {
    uint32_t fd;               // File descriptor number
    Inode* inode;              // Pointer to inode
    uint64_t offset;           // Current read/write position
    uint32_t flags;            // O_RDONLY, O_WRONLY, O_RDWR, etc.
    uint32_t ref_count;        // Reference count (for dup)
};
```

### **File Table**

Each process has a **file descriptor table**:

```
FD 0 → stdin  (keyboard)
FD 1 → stdout (console)
FD 2 → stderr (console)
FD 3 → /home/user/file.txt
FD 4 → /tmp/data.bin
...
```

---

## VFS Architecture

### **Layered Design**

```
┌─────────────────────────────────────┐
│      Application Layer              │
│   (open, read, write, close)        │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│      VFS Layer                      │
│   (Path resolution, FD management)  │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│      File System Layer              │
│   (MemFS, Ext4, FAT32, etc.)        │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│      Block Device Layer             │
│   (Disk, RAM disk, network)         │
└─────────────────────────────────────┘
```

### **Per-Torus VFS**

Each torus has its own VFS instance:

```
Torus A:
- File descriptor table for processes on Torus A
- Inode cache for Torus A
- File system mount points

Torus B:
- File descriptor table for processes on Torus B
- Inode cache for Torus B
- File system mount points

Torus C:
- File descriptor table for processes on Torus C
- Inode cache for Torus C
- File system mount points
```

**No global VFS** → No bottleneck!

---

## File Operations

### **open()**

Open a file and return a file descriptor:

```cpp
int open(const char* path, int flags, int mode);

// Flags:
// O_RDONLY - Read only
// O_WRONLY - Write only
// O_RDWR   - Read/write
// O_CREAT  - Create if doesn't exist
// O_TRUNC  - Truncate to zero length
// O_APPEND - Append to end

// Example:
int fd = open("/home/user/file.txt", O_RDWR | O_CREAT, 0644);
```

**Steps**:
1. Resolve path to inode
2. Check permissions
3. Allocate file descriptor
4. Initialize offset to 0
5. Return FD

### **read()**

Read from a file:

```cpp
ssize_t read(int fd, void* buf, size_t count);

// Example:
char buffer[1024];
ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
```

**Steps**:
1. Look up file descriptor
2. Check if readable
3. Read from inode at current offset
4. Update offset
5. Return bytes read

### **write()**

Write to a file:

```cpp
ssize_t write(int fd, const void* buf, size_t count);

// Example:
const char* data = "Hello, world!";
ssize_t bytes_written = write(fd, data, strlen(data));
```

**Steps**:
1. Look up file descriptor
2. Check if writable
3. Write to inode at current offset
4. Update offset and file size
5. Return bytes written

### **close()**

Close a file descriptor:

```cpp
int close(int fd);

// Example:
close(fd);
```

**Steps**:
1. Look up file descriptor
2. Decrement reference count
3. If ref_count == 0, free FD
4. Return 0 on success

### **seek()**

Change file offset:

```cpp
off_t lseek(int fd, off_t offset, int whence);

// Whence:
// SEEK_SET - Set to offset
// SEEK_CUR - Set to current + offset
// SEEK_END - Set to size + offset

// Example:
lseek(fd, 0, SEEK_SET);  // Rewind to start
```

---

## Directory Operations

### **mkdir()**

Create a directory:

```cpp
int mkdir(const char* path, mode_t mode);

// Example:
mkdir("/home/user/documents", 0755);
```

### **rmdir()**

Remove a directory:

```cpp
int rmdir(const char* path);

// Example:
rmdir("/home/user/old_dir");
```

### **readdir()**

Read directory entries:

```cpp
struct dirent* readdir(DIR* dirp);

// Example:
DIR* dir = opendir("/home/user");
struct dirent* entry;
while ((entry = readdir(dir)) != NULL) {
    printf("%s\n", entry->d_name);
}
closedir(dir);
```

---

## Path Resolution

Convert a path string to an inode:

```
"/home/user/file.txt"
  ↓
1. Start at root inode (/)
2. Look up "home" in root directory
3. Look up "user" in /home directory
4. Look up "file.txt" in /home/user directory
5. Return inode for file.txt
```

**Challenges**:
- Symbolic links (follow or not?)
- Mount points (cross file system boundaries)
- Permissions (check at each level)
- ".." and "." (parent and current directory)

---

## Simplified Implementation (Phase 6.3)

For now, we'll implement a **simple in-memory file system (MemFS)**:

### **MemFS Features**

- Files stored in RAM
- No persistence (lost on reboot)
- Simple flat structure
- Fast (no disk I/O)

### **MemFS Structure**

```cpp
struct MemFSFile {
    char name[256];
    uint8_t* data;
    uint32_t size;
    uint32_t capacity;
    uint32_t mode;
};

struct MemFS {
    MemFSFile files[1024];  // Max 1024 files
    uint32_t num_files;
};
```

### **Limitations**

- No directories (flat namespace)
- No persistence
- Limited to 1024 files
- No permissions (yet)

**But it's enough to prove the concept!**

---

## Implementation Plan

### **Phase 6.3.1: File Descriptors**
- Implement FileDescriptor structure
- Implement per-process FD table
- Implement allocate/free FD

### **Phase 6.3.2: MemFS**
- Implement in-memory file system
- Implement create/delete file
- Implement read/write operations

### **Phase 6.3.3: VFS Layer**
- Implement open/close/read/write syscalls
- Integrate with FD table
- Integrate with MemFS

### **Phase 6.3.4: Testing**
- Test file operations
- Test FD management
- Test edge cases

---

## Success Criteria

### **Functional**
- ✅ open() creates/opens files
- ✅ read() reads file contents
- ✅ write() writes file contents
- ✅ close() closes file descriptors
- ✅ Multiple files can be open simultaneously
- ✅ File offsets work correctly

### **Performance**
- ✅ O(1) FD lookup
- ✅ O(n) file lookup (acceptable for small n)
- ✅ Minimal overhead

---

## Future Enhancements

### **Phase 6.3+**
- Real file system (Ext4, FAT32)
- Persistence (write to disk)
- Directories (hierarchical structure)
- Permissions (rwxrwxrwx)
- Symbolic links
- Hard links
- File locking
- Memory-mapped files (mmap)

---

## The Vision

**Traditional OS**: Global VFS → bottleneck

**Braided OS**: Per-torus VFS → no bottleneck

Each torus manages its own file descriptors and file system cache independently. No locks, no contention, perfect scaling.

**This is how we make old hardware fast.** 🚀
