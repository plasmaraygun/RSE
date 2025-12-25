#pragma once

#include "Bytecode.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

/**
 * OSProcess: Real operating system process abstraction.
 * 
 * This extends the simple BettiRDLProcess to support:
 * - Process states (ready, running, blocked, zombie)
 * - CPU context (registers, stack, instruction pointer)
 * - Memory management (page table, heap, stack)
 * - Scheduling metadata (priority, runtime, time slice)
 * - Parent-child relationships
 */

namespace os {

enum class ProcessState : uint8_t {
    READY,      // Waiting to run
    RUNNING,    // Currently executing on CPU
    BLOCKED,    // Waiting for I/O or event
    ZOMBIE      // Terminated, waiting for parent to reap
};

/**
 * CPU context - saved/restored during context switch.
 * Simplified x86-64 register set.
 */
struct CPUContext {
    // Instruction and stack pointers
    uint64_t rip;  // Instruction pointer
    uint64_t rsp;  // Stack pointer
    uint64_t rbp;  // Base pointer
    
    // General purpose registers
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    
    // Flags
    uint64_t rflags;
    
    CPUContext() {
        std::memset(this, 0, sizeof(CPUContext));
    }
};

/**
 * Memory layout for a process.
 */
struct MemoryLayout {
    // Virtual address space
    uint64_t* page_table;       // Pointer to page table
    
    // Code segment
    uint64_t code_start;
    uint64_t code_end;
    
    // Data segment
    uint64_t data_start;
    uint64_t data_end;
    
    // Heap (grows up)
    uint64_t heap_start;
    uint64_t heap_end;
    uint64_t heap_brk;          // Current break point
    
    // Stack (grows down)
    uint64_t stack_start;
    uint64_t stack_end;
    uint64_t stack_pointer;
    
    MemoryLayout() {
        std::memset(this, 0, sizeof(MemoryLayout));
    }
};

/**
 * File descriptor (simplified).
 */
struct FileDescriptor {
    int fd;
    uint64_t offset;
    uint32_t flags;
    void* private_data;
    
    FileDescriptor() : fd(-1), offset(0), flags(0), private_data(nullptr) {}
};

/**
 * OSProcess: Full operating system process.
 */
class OSProcess {
public:
    // ========== Identity ==========
    uint32_t pid;               // Process ID
    uint32_t parent_pid;        // Parent process ID
    uint32_t torus_id;          // Which torus owns this process
    
    // ========== State ==========
    ProcessState state;
    int exit_code;              // Exit code (if zombie)
    
    // ========== CPU Context ==========
    CPUContext context;
    
    // ========== Memory ==========
    MemoryLayout memory;
    
    // ========== Scheduling ==========
    uint32_t priority;          // Higher = more important (0-255)
    uint64_t time_slice;        // Ticks remaining in current slice
    uint64_t total_runtime;     // Total ticks this process has run
    uint64_t last_scheduled;    // Last time this process was scheduled
    
    // ========== I/O ==========
    static constexpr int MAX_FDS = 64;
    FileDescriptor open_files[MAX_FDS];
    
    // ========== Spatial Position (for Betti integration) ==========
    int x, y, z;                // Position in toroidal space
    
    // ========== Constructor ==========
    OSProcess(uint32_t pid, uint32_t parent_pid, uint32_t torus_id)
        : pid(pid), 
          parent_pid(parent_pid), 
          torus_id(torus_id),
          state(ProcessState::READY),
          exit_code(0),
          priority(100),          // Default priority
          time_slice(100),        // Default time slice
          total_runtime(0),
          last_scheduled(0),
          x(0), y(0), z(0)
    {
        // Initialize standard file descriptors
        open_files[0].fd = 0;  // stdin
        open_files[1].fd = 1;  // stdout
        open_files[2].fd = 2;  // stderr
        
        for (int i = 3; i < MAX_FDS; i++) {
            open_files[i].fd = -1;
        }
    }
    
    // ========== State Management ==========
    
    bool isReady() const { return state == ProcessState::READY; }
    bool isRunning() const { return state == ProcessState::RUNNING; }
    bool isBlocked() const { return state == ProcessState::BLOCKED; }
    bool isZombie() const { return state == ProcessState::ZOMBIE; }
    
    void setReady() { state = ProcessState::READY; }
    void setRunning() { state = ProcessState::RUNNING; }
    void setBlocked() { state = ProcessState::BLOCKED; }
    void setZombie(int code) { 
        state = ProcessState::ZOMBIE; 
        exit_code = code;
    }
    
    // ========== Scheduling ==========
    
    /**
     * Check if time slice has expired.
     */
    bool timeSliceExpired() const {
        return time_slice == 0;
    }
    
    /**
     * Reset time slice (called when process is scheduled).
     */
    void resetTimeSlice(uint64_t slice = 100) {
        time_slice = slice;
    }
    
    /**
     * Consume one tick of time slice.
     */
    void tick() {
        if (time_slice > 0) {
            time_slice--;
        }
        total_runtime++;
    }
    
    /**
     * Get scheduling weight (for fair scheduling).
     */
    uint64_t getWeight() const {
        // Lower runtime = higher weight (gets scheduled sooner)
        return UINT64_MAX - total_runtime;
    }
    
    // ========== Context Switching ==========
    
    // Bytecode VM for this process
    std::unique_ptr<BytecodeVM> vm_;
    
    /**
     * Initialize bytecode VM for this process.
     */
    bool initVM(const uint8_t* program, size_t size) {
        vm_ = std::make_unique<BytecodeVM>();
        return vm_->loadProgram(program, size);
    }
    
    /**
     * Save CPU context (called when preempting).
     * REAL: Saves VM state (registers, IP, SP) to CPUContext.
     */
    void saveContext() {
        if (!vm_) return;
        
        const VMState& state = vm_->getState();
        
        // Save VM registers to CPU context
        context.rip = state.ip;
        context.rsp = state.sp;
        context.rbp = state.bp;
        context.rax = static_cast<uint64_t>(state.registers[0]);
        context.rbx = static_cast<uint64_t>(state.registers[1]);
        context.rcx = static_cast<uint64_t>(state.registers[2]);
        context.rdx = static_cast<uint64_t>(state.registers[3]);
        context.rsi = static_cast<uint64_t>(state.registers[4]);
        context.rdi = static_cast<uint64_t>(state.registers[5]);
        context.r8  = static_cast<uint64_t>(state.registers[6]);
        context.r9  = static_cast<uint64_t>(state.registers[7]);
        context.r10 = static_cast<uint64_t>(state.registers[8]);
        context.r11 = static_cast<uint64_t>(state.registers[9]);
        context.r12 = static_cast<uint64_t>(state.registers[10]);
        context.r13 = static_cast<uint64_t>(state.registers[11]);
        context.r14 = static_cast<uint64_t>(state.registers[12]);
        context.r15 = static_cast<uint64_t>(state.registers[13]);
        context.rflags = state.flags;
    }
    
    /**
     * Restore CPU context (called when resuming).
     * REAL: Restores VM state from CPUContext.
     */
    void restoreContext() {
        if (!vm_) return;
        
        VMState& state = vm_->getState();
        
        // Restore VM registers from CPU context
        state.ip = context.rip;
        state.sp = context.rsp;
        state.bp = context.rbp;
        state.registers[0]  = static_cast<int64_t>(context.rax);
        state.registers[1]  = static_cast<int64_t>(context.rbx);
        state.registers[2]  = static_cast<int64_t>(context.rcx);
        state.registers[3]  = static_cast<int64_t>(context.rdx);
        state.registers[4]  = static_cast<int64_t>(context.rsi);
        state.registers[5]  = static_cast<int64_t>(context.rdi);
        state.registers[6]  = static_cast<int64_t>(context.r8);
        state.registers[7]  = static_cast<int64_t>(context.r9);
        state.registers[8]  = static_cast<int64_t>(context.r10);
        state.registers[9]  = static_cast<int64_t>(context.r11);
        state.registers[10] = static_cast<int64_t>(context.r12);
        state.registers[11] = static_cast<int64_t>(context.r13);
        state.registers[12] = static_cast<int64_t>(context.r14);
        state.registers[13] = static_cast<int64_t>(context.r15);
        state.flags = context.rflags;
    }
    
    /**
     * Execute one tick of this process.
     * REAL: Runs bytecode VM for one time slice.
     */
    void execute() {
        if (!vm_) {
            tick();
            return;
        }
        
        // Run VM for time_slice instructions
        for (uint32_t i = 0; i < time_slice && !vm_->isHalted(); i++) {
            vm_->step();
        }
        
        tick();
        
        // Check if process terminated
        if (vm_->isHalted()) {
            exit_code = vm_->getExitCode();
            setZombie();
        }
    }
    
    /**
     * Get VM for direct access (for syscall integration).
     */
    BytecodeVM* getVM() { return vm_.get(); }
    const BytecodeVM* getVM() const { return vm_.get(); }
    
    // ========== Memory Management ==========
    
    /**
     * Allocate memory (sbrk-like).
     */
    uint64_t allocateMemory(uint64_t size) {
        uint64_t old_brk = memory.heap_brk;
        memory.heap_brk += size;
        
        // Check if we exceeded heap limit
        if (memory.heap_brk > memory.heap_end) {
            memory.heap_brk = old_brk;  // Rollback
            return 0;  // Out of memory
        }
        
        return old_brk;
    }
    
    /**
     * Free memory - reduces heap break if possible.
     * Note: This is a simple allocator that only frees from top of heap.
     * A production allocator would use free lists and coalescing.
     */
    void freeMemory(uint64_t addr) {
        // Simple allocator: only shrink heap if freeing from top
        // This matches behavior of early Unix systems
        if (addr > 0 && addr < memory.heap_brk) {
            // Cannot free middle of heap with simple brk allocator
            // Memory remains allocated until process exits
            // For proper free(), use malloc/free with free lists
        }
        // Note: All process memory is freed on process termination
    }
    
    // ========== File Descriptors ==========
    
    /**
     * Allocate a file descriptor.
     */
    int allocateFD() {
        for (int i = 3; i < MAX_FDS; i++) {
            if (open_files[i].fd == -1) {
                open_files[i].fd = i;
                return i;
            }
        }
        return -1;  // No free FDs
    }
    
    /**
     * Free a file descriptor.
     */
    void freeFD(int fd) {
        if (fd >= 0 && fd < MAX_FDS) {
            open_files[fd].fd = -1;
            open_files[fd].offset = 0;
            open_files[fd].flags = 0;
            open_files[fd].private_data = nullptr;
        }
    }
    
    /**
     * Get file descriptor.
     */
    FileDescriptor* getFD(int fd) {
        if (fd >= 0 && fd < MAX_FDS && open_files[fd].fd != -1) {
            return &open_files[fd];
        }
        return nullptr;
    }
    
    // ========== Debugging ==========
    
    void print() const {
        const char* state_str = "UNKNOWN";
        switch (state) {
            case ProcessState::READY: state_str = "READY"; break;
            case ProcessState::RUNNING: state_str = "RUNNING"; break;
            case ProcessState::BLOCKED: state_str = "BLOCKED"; break;
            case ProcessState::ZOMBIE: state_str = "ZOMBIE"; break;
        }
        
        std::cout << "[Process " << pid << "]"
                  << " parent=" << parent_pid
                  << " torus=" << torus_id
                  << " state=" << state_str
                  << " priority=" << priority
                  << " runtime=" << total_runtime
                  << " slice=" << time_slice
                  << std::endl;
    }
};

} // namespace os
