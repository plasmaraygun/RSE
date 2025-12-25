/**
 * OS Global Variables
 * Freestanding definitions for kernel subsystems
 */

#include "memory.h"
#include "vfs.h"
#include "syscalls.h"
#include "shell.h"

namespace mem {
    PhysicalAllocator g_phys_alloc;
    HeapAllocator g_heap;
}

namespace vfs {
    RamFS g_fs;
}

namespace sys {
    Process g_procs[MAX_PROCS];
    uint32_t g_current_pid = 1;
}

namespace shell {
    Shell g_shell;
}
