#pragma once
/**
 * System Call Interface for ARQON Kernel
 * Phase 6.4: Syscall dispatch, process management
 */

#include <cstdint>
#include "memory.h"
#include "vfs.h"
#include "../drivers/serial.h"

namespace sys {

// Syscall numbers (Linux-compatible subset)
constexpr uint64_t SYS_READ     = 0;
constexpr uint64_t SYS_WRITE    = 1;
constexpr uint64_t SYS_OPEN     = 2;
constexpr uint64_t SYS_CLOSE    = 3;
constexpr uint64_t SYS_LSEEK    = 8;
constexpr uint64_t SYS_MMAP     = 9;
constexpr uint64_t SYS_MUNMAP   = 11;
constexpr uint64_t SYS_BRK      = 12;
constexpr uint64_t SYS_GETPID   = 39;
constexpr uint64_t SYS_EXIT     = 60;
constexpr uint64_t SYS_UNAME    = 63;
constexpr uint64_t SYS_GETCWD   = 79;
constexpr uint64_t SYS_CHDIR    = 80;

// Process state
struct Process {
    uint32_t pid;
    uint32_t parent_pid;
    char cwd[128];
    bool active;
    
    void init(uint32_t p, uint32_t pp) {
        pid = p;
        parent_pid = pp;
        cwd[0] = '/';
        cwd[1] = '\0';
        active = true;
    }
};

// Simple process table
constexpr uint32_t MAX_PROCS = 16;
extern Process g_procs[MAX_PROCS];
extern uint32_t g_current_pid;

// uname structure
struct UtsName {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};


/**
 * Syscall handler - called from interrupt handler
 * Arguments in: rdi, rsi, rdx, r10, r8, r9
 * Syscall number in: rax
 * Return value in: rax
 */
inline int64_t handle_syscall(uint64_t num, uint64_t a1, uint64_t a2, 
                               uint64_t a3, uint64_t a4, uint64_t a5) {
    switch (num) {
        case SYS_READ:
            return vfs::g_fs.read(static_cast<int32_t>(a1), 
                                  reinterpret_cast<void*>(a2),
                                  static_cast<uint32_t>(a3));
        
        case SYS_WRITE: {
            int32_t fd = static_cast<int32_t>(a1);
            const char* buf = reinterpret_cast<const char*>(a2);
            uint32_t count = static_cast<uint32_t>(a3);
            
            // Console output
            if (vfs::g_fs.is_console_fd(fd)) {
                auto& serial = drivers::getSerial();
                for (uint32_t i = 0; i < count; i++) {
                    serial.putchar(buf[i]);
                }
                return count;
            }
            
            return vfs::g_fs.write(fd, buf, count);
        }
        
        case SYS_OPEN:
            return vfs::g_fs.open(
                reinterpret_cast<const char*>(a1),
                static_cast<uint32_t>(a2),
                static_cast<uint32_t>(a3)
            );
        
        case SYS_CLOSE:
            return vfs::g_fs.close(static_cast<int32_t>(a1));
        
        case SYS_LSEEK:
            return vfs::g_fs.lseek(
                static_cast<int32_t>(a1),
                static_cast<int64_t>(a2),
                static_cast<int>(a3)
            );
        
        case SYS_BRK:
            // Simple brk - just return current break
            return 0x400000;  // Fixed heap start
        
        case SYS_GETPID:
            return g_current_pid;
        
        case SYS_EXIT:
            if (g_current_pid < MAX_PROCS) {
                g_procs[g_current_pid].active = false;
            }
            return 0;
        
        case SYS_UNAME: {
            UtsName* buf = reinterpret_cast<UtsName*>(a1);
            if (!buf) return -1;
            
            // Copy system info
            const char* sys = "ARQON";
            const char* node = "arqon";
            const char* rel = "0.1.0";
            const char* ver = "ARQON RSE Kernel";
            const char* mach = "x86_64";
            
            for (int i = 0; sys[i] && i < 64; i++) buf->sysname[i] = sys[i];
            buf->sysname[5] = '\0';
            for (int i = 0; node[i] && i < 64; i++) buf->nodename[i] = node[i];
            buf->nodename[5] = '\0';
            for (int i = 0; rel[i] && i < 64; i++) buf->release[i] = rel[i];
            buf->release[5] = '\0';
            for (int i = 0; ver[i] && i < 64; i++) buf->version[i] = ver[i];
            buf->version[16] = '\0';
            for (int i = 0; mach[i] && i < 64; i++) buf->machine[i] = mach[i];
            buf->machine[6] = '\0';
            
            return 0;
        }
        
        case SYS_GETCWD: {
            char* buf = reinterpret_cast<char*>(a1);
            uint64_t size = a2;
            if (!buf || size == 0) return -1;
            
            if (g_current_pid < MAX_PROCS) {
                const char* cwd = g_procs[g_current_pid].cwd;
                uint64_t len = 0;
                while (cwd[len]) len++;
                if (len + 1 > size) return -1;
                for (uint64_t i = 0; i <= len; i++) buf[i] = cwd[i];
                return reinterpret_cast<int64_t>(buf);
            }
            return -1;
        }
        
        case SYS_CHDIR: {
            const char* path = reinterpret_cast<const char*>(a1);
            if (!path) return -1;
            
            // Simple implementation - just set cwd
            if (g_current_pid < MAX_PROCS) {
                char* cwd = g_procs[g_current_pid].cwd;
                int i = 0;
                while (path[i] && i < 126) {
                    cwd[i] = path[i];
                    i++;
                }
                cwd[i] = '\0';
                return 0;
            }
            return -1;
        }
        
        default:
            return -1;  // ENOSYS
    }
}

// Initialize process table
inline void init_procs() {
    for (uint32_t i = 0; i < MAX_PROCS; i++) {
        g_procs[i].active = false;
    }
    // Create init process (pid 1)
    g_procs[1].init(1, 0);
    g_current_pid = 1;
}

} // namespace sys
