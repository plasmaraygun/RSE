# System Call Interface Design

**Goal**: Enable applications to interact with the braided OS through a clean, POSIX-like API.

---

## What are System Calls?

System calls are the **interface** between user applications and the operating system kernel.

```
Application Code
      ↓
  syscall()  ← User space / Kernel space boundary
      ↓
OS Kernel
```

When an application needs to:
- Create a process
- Read a file
- Allocate memory
- Send network data

It makes a **system call** to ask the OS to do it.

---

## Traditional System Call Mechanism

### **x86-64 Linux**

1. Application puts syscall number in `rax`
2. Application puts arguments in `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`
3. Application executes `syscall` instruction
4. CPU switches to kernel mode
5. Kernel dispatches to appropriate handler
6. Kernel returns result in `rax`
7. CPU switches back to user mode

**Problem**: Requires CPU mode switching (expensive on old hardware)

---

## Braided OS System Call Mechanism

We'll use a **simplified** approach for now (can optimize later):

### **Function Call Interface**

```cpp
// Application code
int result = syscall(SYS_WRITE, fd, buffer, size);
```

No CPU mode switching needed (yet) - we're building the OS layer first, actual user/kernel separation comes later.

---

## System Call Categories

### **1. Process Management**
- `fork()` - Create child process
- `exec()` - Replace process with new program
- `exit()` - Terminate process
- `wait()` - Wait for child process
- `getpid()` - Get process ID
- `getppid()` - Get parent process ID
- `kill()` - Send signal to process

### **2. File I/O**
- `open()` - Open file
- `close()` - Close file
- `read()` - Read from file
- `write()` - Write to file
- `lseek()` - Seek in file
- `stat()` - Get file info
- `unlink()` - Delete file

### **3. Memory Management**
- `brk()` / `sbrk()` - Adjust heap size
- `mmap()` - Map memory
- `munmap()` - Unmap memory
- `mprotect()` - Change memory protection

### **4. Inter-Process Communication**
- `pipe()` - Create pipe
- `dup()` / `dup2()` - Duplicate file descriptor
- `signal()` - Set signal handler

### **5. Time & Sleep**
- `time()` - Get current time
- `sleep()` - Sleep for N seconds
- `nanosleep()` - High-resolution sleep

---

## System Call Numbers

```cpp
// Process management
#define SYS_FORK      1
#define SYS_EXEC      2
#define SYS_EXIT      3
#define SYS_WAIT      4
#define SYS_GETPID    5
#define SYS_GETPPID   6
#define SYS_KILL      7

// File I/O
#define SYS_OPEN      10
#define SYS_CLOSE     11
#define SYS_READ      12
#define SYS_WRITE     13
#define SYS_LSEEK     14
#define SYS_STAT      15
#define SYS_UNLINK    16

// Memory management
#define SYS_BRK       20
#define SYS_MMAP      21
#define SYS_MUNMAP    22
#define SYS_MPROTECT  23

// IPC
#define SYS_PIPE      30
#define SYS_DUP       31
#define SYS_DUP2      32
#define SYS_SIGNAL    33

// Time
#define SYS_TIME      40
#define SYS_SLEEP     41
#define SYS_NANOSLEEP 42
```

---

## System Call Handler Interface

Each system call has a handler function:

```cpp
typedef int64_t (*syscall_handler_t)(uint64_t arg1, uint64_t arg2, 
                                      uint64_t arg3, uint64_t arg4,
                                      uint64_t arg5, uint64_t arg6);
```

Example handlers:

```cpp
int64_t sys_getpid(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count, ...);
int64_t sys_fork(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
```

---

## System Call Dispatcher

The dispatcher routes syscall numbers to handlers:

```cpp
class SyscallDispatcher {
private:
    syscall_handler_t handlers[256];  // Up to 256 syscalls
    
public:
    int64_t dispatch(int syscall_num, 
                     uint64_t arg1, uint64_t arg2, uint64_t arg3,
                     uint64_t arg4, uint64_t arg5, uint64_t arg6) {
        
        if (syscall_num < 0 || syscall_num >= 256) {
            return -EINVAL;  // Invalid syscall number
        }
        
        syscall_handler_t handler = handlers[syscall_num];
        if (!handler) {
            return -ENOSYS;  // Not implemented
        }
        
        return handler(arg1, arg2, arg3, arg4, arg5, arg6);
    }
    
    void register_handler(int syscall_num, syscall_handler_t handler) {
        handlers[syscall_num] = handler;
    }
};
```

---

## Integration with Braided System

### **Per-Torus Syscall Handling**

Each torus has its own syscall dispatcher:

```
Application on Torus A → Syscall → Torus A dispatcher → Torus A handler
Application on Torus B → Syscall → Torus B dispatcher → Torus B handler
Application on Torus C → Syscall → Torus C dispatcher → Torus C handler
```

**No global syscall handler** - each torus handles its own syscalls independently.

### **Cross-Torus Operations**

Some syscalls might need to interact with other tori:

- `fork()` - Might place child on different torus (load balancing)
- `kill()` - Might send signal to process on different torus
- `pipe()` - Might connect processes on different tori

These use the **braid coordination** mechanism (projection exchange).

---

## Error Handling

System calls return:
- **Positive value** or **zero** on success
- **Negative value** on error (errno-style)

```cpp
// Success
int fd = syscall(SYS_OPEN, "/tmp/file.txt", O_RDONLY);
if (fd >= 0) {
    // Success - fd is valid file descriptor
}

// Error
int result = syscall(SYS_OPEN, "/nonexistent", O_RDONLY);
if (result < 0) {
    // Error - result is -errno
    // e.g., -ENOENT (file not found)
}
```

Common error codes:
```cpp
#define EPERM     1   // Operation not permitted
#define ENOENT    2   // No such file or directory
#define ESRCH     3   // No such process
#define EINTR     4   // Interrupted system call
#define EIO       5   // I/O error
#define EBADF     9   // Bad file descriptor
#define ENOMEM    12  // Out of memory
#define EACCES    13  // Permission denied
#define EINVAL    22  // Invalid argument
#define ENOSYS    38  // Function not implemented
```

---

## Implementation Priority

### **Phase 6.1.1: Core Process Management**
- `getpid()` - Simple, just return current PID
- `exit()` - Mark process as zombie
- `fork()` - Create child process (simplified)

### **Phase 6.1.2: Basic File I/O**
- `write()` - Write to stdout/stderr (console only for now)
- `read()` - Read from stdin (console only)

### **Phase 6.1.3: Advanced Process Management**
- `exec()` - Replace process (load new program)
- `wait()` - Wait for child to exit

### **Phase 6.1.4: Full File I/O**
- `open()`, `close()` - File operations
- `read()`, `write()` - Full file I/O
- `lseek()` - Seek in files

---

## Example: Implementing `getpid()`

```cpp
// Handler
int64_t sys_getpid(uint64_t, uint64_t, uint64_t, 
                   uint64_t, uint64_t, uint64_t) {
    OSProcess* current = get_current_process();
    if (!current) {
        return -ESRCH;  // No current process
    }
    return current->pid;
}

// Registration
dispatcher.register_handler(SYS_GETPID, sys_getpid);

// Usage
int pid = syscall(SYS_GETPID);
printf("My PID is %d\n", pid);
```

---

## Example: Implementing `write()`

```cpp
// Handler
int64_t sys_write(uint64_t fd, uint64_t buf_addr, uint64_t count,
                  uint64_t, uint64_t, uint64_t) {
    OSProcess* current = get_current_process();
    if (!current) {
        return -ESRCH;
    }
    
    // Check file descriptor
    if (fd >= OSProcess::MAX_FDS) {
        return -EBADF;
    }
    
    FileDescriptor* file = current->getFD(fd);
    if (!file) {
        return -EBADF;
    }
    
    // For now, just write to console (stdout/stderr)
    if (fd == 1 || fd == 2) {
        const char* buf = (const char*)buf_addr;
        for (size_t i = 0; i < count; i++) {
            putchar(buf[i]);
        }
        return count;
    }
    
    // Real file I/O not implemented yet
    return -ENOSYS;
}

// Registration
dispatcher.register_handler(SYS_WRITE, sys_write);

// Usage
const char* msg = "Hello, world!\n";
syscall(SYS_WRITE, 1, (uint64_t)msg, strlen(msg));
```

---

## Example: Implementing `fork()`

```cpp
// Handler
int64_t sys_fork(uint64_t, uint64_t, uint64_t,
                 uint64_t, uint64_t, uint64_t) {
    OSProcess* parent = get_current_process();
    if (!parent) {
        return -ESRCH;
    }
    
    // Allocate new PID
    uint32_t child_pid = allocate_pid();
    
    // Create child process
    OSProcess* child = new OSProcess(child_pid, parent->pid, parent->torus_id);
    
    // Copy parent's context
    child->context = parent->context;
    child->memory = parent->memory;
    child->priority = parent->priority;
    
    // Copy file descriptors
    for (int i = 0; i < OSProcess::MAX_FDS; i++) {
        child->open_files[i] = parent->open_files[i];
    }
    
    // Add child to scheduler
    get_scheduler(parent->torus_id)->addProcess(child);
    
    // Return child PID to parent, 0 to child
    // (In real implementation, child would see 0 when it runs)
    return child_pid;
}

// Registration
dispatcher.register_handler(SYS_FORK, sys_fork);

// Usage
int pid = syscall(SYS_FORK);
if (pid == 0) {
    // Child process
    printf("I'm the child!\n");
} else {
    // Parent process
    printf("I'm the parent, child PID is %d\n", pid);
}
```

---

## Testing Strategy

### **Unit Tests**
Test each syscall handler individually:
- `test_getpid()` - Verify PID is returned correctly
- `test_write()` - Verify output is written
- `test_fork()` - Verify child is created

### **Integration Tests**
Test syscalls working together:
- Fork + exit + wait
- Open + read + write + close
- Pipe + fork + read/write

### **Application Tests**
Build simple programs that use syscalls:
- Hello world (write)
- Echo (read + write)
- Fork bomb (fork + exit)

---

## Success Criteria

### **Functional**
- ✅ All core syscalls implemented
- ✅ Syscall dispatcher routes correctly
- ✅ Error handling works
- ✅ Cross-torus operations work

### **Performance**
- ✅ Syscall overhead < 100ns
- ✅ No global contention
- ✅ Scales with number of tori

---

## Next Steps

1. **Implement syscall numbers and error codes**
2. **Implement syscall dispatcher**
3. **Implement core syscalls (getpid, exit, fork)**
4. **Implement basic I/O (write to console)**
5. **Test with sample programs**

---

## The Vision

**Traditional OS**: Global syscall handler → bottleneck

**Braided OS**: Per-torus syscall handlers → no bottleneck

Applications can make syscalls without waiting for a global lock. Each torus handles its own syscalls independently.

**This is how we make old hardware fast.** 🚀
