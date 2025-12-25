#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <iostream>

/**
 * RSE Bytecode Virtual Machine
 * 
 * A real, functional bytecode interpreter for running programs
 * on the RSE toroidal architecture.
 * 
 * Design: Stack-based VM (like JVM/Python VM)
 * - 64-bit registers (16 general purpose)
 * - 64KB stack per process
 * - 64KB heap per process
 * - Syscall interface to RSE kernel
 */

namespace os {

// ============================================================================
// Bytecode Opcodes
// ============================================================================

enum class Opcode : uint8_t {
    // Stack operations
    NOP         = 0x00,
    PUSH_IMM    = 0x01,  // Push immediate value
    PUSH_REG    = 0x02,  // Push register value
    POP_REG     = 0x03,  // Pop to register
    DUP         = 0x04,  // Duplicate top of stack
    SWAP        = 0x05,  // Swap top two values
    
    // Arithmetic (operate on stack)
    ADD         = 0x10,
    SUB         = 0x11,
    MUL         = 0x12,
    DIV         = 0x13,
    MOD         = 0x14,
    NEG         = 0x15,
    
    // Bitwise
    AND         = 0x20,
    OR          = 0x21,
    XOR         = 0x22,
    NOT         = 0x23,
    SHL         = 0x24,
    SHR         = 0x25,
    
    // Comparison (push 1 or 0)
    EQ          = 0x30,
    NE          = 0x31,
    LT          = 0x32,
    LE          = 0x33,
    GT          = 0x34,
    GE          = 0x35,
    
    // Control flow
    JMP         = 0x40,  // Unconditional jump
    JZ          = 0x41,  // Jump if zero
    JNZ         = 0x42,  // Jump if not zero
    CALL        = 0x43,  // Call subroutine
    RET         = 0x44,  // Return from subroutine
    
    // Memory
    LOAD        = 0x50,  // Load from heap address
    STORE       = 0x51,  // Store to heap address
    LOAD_REG    = 0x52,  // Load from address in register
    STORE_REG   = 0x53,  // Store to address in register
    
    // Register operations
    MOV_IMM     = 0x60,  // Move immediate to register
    MOV_REG     = 0x61,  // Move register to register
    
    // System
    SYSCALL     = 0xF0,  // System call (syscall number in R0)
    HALT        = 0xFF,  // Stop execution
};

// ============================================================================
// Bytecode VM State
// ============================================================================

struct VMState {
    // Registers (R0-R15)
    static constexpr size_t NUM_REGISTERS = 16;
    std::array<int64_t, NUM_REGISTERS> registers{};
    
    // Special registers
    uint64_t ip = 0;      // Instruction pointer
    uint64_t sp = 0;      // Stack pointer
    uint64_t bp = 0;      // Base pointer (for call frames)
    uint64_t flags = 0;   // Status flags
    
    // Memory
    static constexpr size_t STACK_SIZE = 64 * 1024;  // 64KB
    static constexpr size_t HEAP_SIZE = 64 * 1024;   // 64KB
    static constexpr size_t CODE_SIZE = 64 * 1024;   // 64KB
    
    std::array<uint8_t, STACK_SIZE> stack{};
    std::array<uint8_t, HEAP_SIZE> heap{};
    std::array<uint8_t, CODE_SIZE> code{};
    
    // Execution state
    bool running = false;
    bool halted = false;
    uint64_t cycles = 0;
    int exit_code = 0;
    
    // Syscall result
    int64_t syscall_result = 0;
    
    VMState() {
        sp = STACK_SIZE;  // Stack grows down
        bp = STACK_SIZE;
    }
    
    void reset() {
        registers.fill(0);
        ip = 0;
        sp = STACK_SIZE;
        bp = STACK_SIZE;
        flags = 0;
        stack.fill(0);
        heap.fill(0);
        running = false;
        halted = false;
        cycles = 0;
        exit_code = 0;
    }
};

// ============================================================================
// Bytecode Interpreter
// ============================================================================

class BytecodeVM {
private:
    VMState state_;
    
    // Stack operations
    void push(int64_t value) {
        if (state_.sp < 8) {
            std::cerr << "[VM] Stack overflow!" << std::endl;
            state_.halted = true;
            return;
        }
        state_.sp -= 8;
        std::memcpy(&state_.stack[state_.sp], &value, 8);
    }
    
    int64_t pop() {
        if (state_.sp >= VMState::STACK_SIZE) {
            std::cerr << "[VM] Stack underflow!" << std::endl;
            state_.halted = true;
            return 0;
        }
        int64_t value;
        std::memcpy(&value, &state_.stack[state_.sp], 8);
        state_.sp += 8;
        return value;
    }
    
    int64_t peek() {
        if (state_.sp >= VMState::STACK_SIZE) return 0;
        int64_t value;
        std::memcpy(&value, &state_.stack[state_.sp], 8);
        return value;
    }
    
    // Fetch operations
    uint8_t fetchByte() {
        if (state_.ip >= VMState::CODE_SIZE) {
            state_.halted = true;
            return 0;
        }
        return state_.code[state_.ip++];
    }
    
    int64_t fetchImm64() {
        if (state_.ip + 8 > VMState::CODE_SIZE) {
            state_.halted = true;
            return 0;
        }
        int64_t value;
        std::memcpy(&value, &state_.code[state_.ip], 8);
        state_.ip += 8;
        return value;
    }
    
    uint32_t fetchImm32() {
        if (state_.ip + 4 > VMState::CODE_SIZE) {
            state_.halted = true;
            return 0;
        }
        uint32_t value;
        std::memcpy(&value, &state_.code[state_.ip], 4);
        state_.ip += 4;
        return value;
    }
    
    // Memory access
    int64_t loadHeap(uint64_t addr) {
        if (addr + 8 > VMState::HEAP_SIZE) {
            std::cerr << "[VM] Heap read out of bounds: " << addr << std::endl;
            return 0;
        }
        int64_t value;
        std::memcpy(&value, &state_.heap[addr], 8);
        return value;
    }
    
    void storeHeap(uint64_t addr, int64_t value) {
        if (addr + 8 > VMState::HEAP_SIZE) {
            std::cerr << "[VM] Heap write out of bounds: " << addr << std::endl;
            return;
        }
        std::memcpy(&state_.heap[addr], &value, 8);
    }
    
public:
    BytecodeVM() = default;
    
    // Load bytecode program
    bool loadProgram(const uint8_t* program, size_t size) {
        if (size > VMState::CODE_SIZE) {
            std::cerr << "[VM] Program too large: " << size << " bytes" << std::endl;
            return false;
        }
        state_.reset();
        std::memcpy(state_.code.data(), program, size);
        return true;
    }
    
    // Execute single instruction
    bool step() {
        if (state_.halted) return false;
        
        state_.cycles++;
        Opcode op = static_cast<Opcode>(fetchByte());
        
        int64_t a, b;
        uint8_t reg, reg2;
        uint32_t addr;
        
        switch (op) {
            case Opcode::NOP:
                break;
                
            case Opcode::PUSH_IMM:
                push(fetchImm64());
                break;
                
            case Opcode::PUSH_REG:
                reg = fetchByte();
                if (reg < VMState::NUM_REGISTERS) {
                    push(state_.registers[reg]);
                }
                break;
                
            case Opcode::POP_REG:
                reg = fetchByte();
                if (reg < VMState::NUM_REGISTERS) {
                    state_.registers[reg] = pop();
                }
                break;
                
            case Opcode::DUP:
                push(peek());
                break;
                
            case Opcode::SWAP:
                a = pop();
                b = pop();
                push(a);
                push(b);
                break;
                
            // Arithmetic
            case Opcode::ADD:
                b = pop(); a = pop();
                push(a + b);
                break;
                
            case Opcode::SUB:
                b = pop(); a = pop();
                push(a - b);
                break;
                
            case Opcode::MUL:
                b = pop(); a = pop();
                push(a * b);
                break;
                
            case Opcode::DIV:
                b = pop(); a = pop();
                push(b != 0 ? a / b : 0);
                break;
                
            case Opcode::MOD:
                b = pop(); a = pop();
                push(b != 0 ? a % b : 0);
                break;
                
            case Opcode::NEG:
                push(-pop());
                break;
                
            // Bitwise
            case Opcode::AND:
                b = pop(); a = pop();
                push(a & b);
                break;
                
            case Opcode::OR:
                b = pop(); a = pop();
                push(a | b);
                break;
                
            case Opcode::XOR:
                b = pop(); a = pop();
                push(a ^ b);
                break;
                
            case Opcode::NOT:
                push(~pop());
                break;
                
            case Opcode::SHL:
                b = pop(); a = pop();
                push(a << b);
                break;
                
            case Opcode::SHR:
                b = pop(); a = pop();
                push(static_cast<uint64_t>(a) >> b);
                break;
                
            // Comparison
            case Opcode::EQ:
                b = pop(); a = pop();
                push(a == b ? 1 : 0);
                break;
                
            case Opcode::NE:
                b = pop(); a = pop();
                push(a != b ? 1 : 0);
                break;
                
            case Opcode::LT:
                b = pop(); a = pop();
                push(a < b ? 1 : 0);
                break;
                
            case Opcode::LE:
                b = pop(); a = pop();
                push(a <= b ? 1 : 0);
                break;
                
            case Opcode::GT:
                b = pop(); a = pop();
                push(a > b ? 1 : 0);
                break;
                
            case Opcode::GE:
                b = pop(); a = pop();
                push(a >= b ? 1 : 0);
                break;
                
            // Control flow
            case Opcode::JMP:
                addr = fetchImm32();
                state_.ip = addr;
                break;
                
            case Opcode::JZ:
                addr = fetchImm32();
                if (pop() == 0) state_.ip = addr;
                break;
                
            case Opcode::JNZ:
                addr = fetchImm32();
                if (pop() != 0) state_.ip = addr;
                break;
                
            case Opcode::CALL:
                addr = fetchImm32();
                push(static_cast<int64_t>(state_.ip));  // Return address
                push(static_cast<int64_t>(state_.bp));  // Old base pointer
                state_.bp = state_.sp;
                state_.ip = addr;
                break;
                
            case Opcode::RET:
                state_.sp = state_.bp;
                state_.bp = static_cast<uint64_t>(pop());
                state_.ip = static_cast<uint64_t>(pop());
                break;
                
            // Memory
            case Opcode::LOAD:
                addr = fetchImm32();
                push(loadHeap(addr));
                break;
                
            case Opcode::STORE:
                addr = fetchImm32();
                storeHeap(addr, pop());
                break;
                
            case Opcode::LOAD_REG:
                reg = fetchByte();
                if (reg < VMState::NUM_REGISTERS) {
                    push(loadHeap(static_cast<uint64_t>(state_.registers[reg])));
                }
                break;
                
            case Opcode::STORE_REG:
                reg = fetchByte();
                if (reg < VMState::NUM_REGISTERS) {
                    storeHeap(static_cast<uint64_t>(state_.registers[reg]), pop());
                }
                break;
                
            // Register operations
            case Opcode::MOV_IMM:
                reg = fetchByte();
                if (reg < VMState::NUM_REGISTERS) {
                    state_.registers[reg] = fetchImm64();
                }
                break;
                
            case Opcode::MOV_REG:
                reg = fetchByte();
                reg2 = fetchByte();
                if (reg < VMState::NUM_REGISTERS && reg2 < VMState::NUM_REGISTERS) {
                    state_.registers[reg] = state_.registers[reg2];
                }
                break;
                
            // System
            case Opcode::SYSCALL:
                handleSyscall();
                break;
                
            case Opcode::HALT:
                state_.exit_code = static_cast<int>(pop());
                state_.halted = true;
                break;
                
            default:
                std::cerr << "[VM] Unknown opcode: 0x" << std::hex 
                          << static_cast<int>(op) << std::dec << std::endl;
                state_.halted = true;
                return false;
        }
        
        return !state_.halted;
    }
    
    // Run until halt or max cycles
    int run(uint64_t max_cycles = 1000000) {
        state_.running = true;
        
        while (!state_.halted && state_.cycles < max_cycles) {
            if (!step()) break;
        }
        
        state_.running = false;
        
        if (state_.cycles >= max_cycles && !state_.halted) {
            std::cerr << "[VM] Max cycles reached, forcing halt" << std::endl;
            state_.exit_code = -1;
        }
        
        return state_.exit_code;
    }
    
    // Syscall handler (can be overridden)
    virtual void handleSyscall() {
        // R0 = syscall number
        // R1-R5 = arguments
        // Result goes to R0
        
        int64_t syscall_num = state_.registers[0];
        
        switch (syscall_num) {
            case 0: // SYS_EXIT
                state_.exit_code = static_cast<int>(state_.registers[1]);
                state_.halted = true;
                break;
                
            case 1: // SYS_PRINT (print R1 as integer)
                std::cout << state_.registers[1];
                state_.registers[0] = 0;
                break;
                
            case 2: // SYS_PRINT_CHAR (print R1 as char)
                std::cout << static_cast<char>(state_.registers[1]);
                state_.registers[0] = 0;
                break;
                
            case 3: // SYS_READ_INT (read integer to R0)
                std::cin >> state_.registers[0];
                break;
                
            case 10: // SYS_ALLOC (allocate R1 bytes, return address in R0)
                // Simple bump allocator from heap
                state_.registers[0] = state_.registers[15];  // R15 = heap pointer
                state_.registers[15] += state_.registers[1];
                if (static_cast<uint64_t>(state_.registers[15]) > VMState::HEAP_SIZE) {
                    state_.registers[0] = 0;  // Allocation failed
                    state_.registers[15] -= state_.registers[1];
                }
                break;
                
            case 20: // SYS_GET_TORUS_ID (get current torus ID)
                state_.registers[0] = 0;  // Default to torus 0
                break;
                
            case 21: // SYS_SEND_EVENT (send event to coordinates in R1,R2,R3 with payload R4)
                // This would integrate with BettiRDLKernel
                state_.registers[0] = 0;  // Success
                break;
                
            case 22: // SYS_GET_TIME (get current kernel time)
                state_.registers[0] = static_cast<int64_t>(state_.cycles);
                break;
                
            default:
                std::cerr << "[VM] Unknown syscall: " << syscall_num << std::endl;
                state_.registers[0] = -1;
                break;
        }
    }
    
    // Accessors
    const VMState& getState() const { return state_; }
    VMState& getState() { return state_; }
    uint64_t getCycles() const { return state_.cycles; }
    bool isHalted() const { return state_.halted; }
    int getExitCode() const { return state_.exit_code; }
    
    // Set register (for initialization)
    void setRegister(uint8_t reg, int64_t value) {
        if (reg < VMState::NUM_REGISTERS) {
            state_.registers[reg] = value;
        }
    }
    
    // Write to heap (for initialization)
    void writeHeap(uint64_t addr, const void* data, size_t size) {
        if (addr + size <= VMState::HEAP_SIZE) {
            std::memcpy(&state_.heap[addr], data, size);
        }
    }
};

// ============================================================================
// Bytecode Assembler (for creating programs)
// ============================================================================

class BytecodeAssembler {
private:
    std::array<uint8_t, VMState::CODE_SIZE> code_{};
    size_t pos_ = 0;
    
    void emit(uint8_t byte) {
        if (pos_ < code_.size()) {
            code_[pos_++] = byte;
        }
    }
    
    void emit64(int64_t value) {
        if (pos_ + 8 <= code_.size()) {
            std::memcpy(&code_[pos_], &value, 8);
            pos_ += 8;
        }
    }
    
    void emit32(uint32_t value) {
        if (pos_ + 4 <= code_.size()) {
            std::memcpy(&code_[pos_], &value, 4);
            pos_ += 4;
        }
    }
    
public:
    BytecodeAssembler() = default;
    
    size_t pos() const { return pos_; }
    
    // Instructions
    void nop() { emit(static_cast<uint8_t>(Opcode::NOP)); }
    void push_imm(int64_t val) { emit(static_cast<uint8_t>(Opcode::PUSH_IMM)); emit64(val); }
    void push_reg(uint8_t reg) { emit(static_cast<uint8_t>(Opcode::PUSH_REG)); emit(reg); }
    void pop_reg(uint8_t reg) { emit(static_cast<uint8_t>(Opcode::POP_REG)); emit(reg); }
    void dup() { emit(static_cast<uint8_t>(Opcode::DUP)); }
    void swap() { emit(static_cast<uint8_t>(Opcode::SWAP)); }
    
    void add() { emit(static_cast<uint8_t>(Opcode::ADD)); }
    void sub() { emit(static_cast<uint8_t>(Opcode::SUB)); }
    void mul() { emit(static_cast<uint8_t>(Opcode::MUL)); }
    void div() { emit(static_cast<uint8_t>(Opcode::DIV)); }
    void mod() { emit(static_cast<uint8_t>(Opcode::MOD)); }
    void neg() { emit(static_cast<uint8_t>(Opcode::NEG)); }
    
    void and_() { emit(static_cast<uint8_t>(Opcode::AND)); }
    void or_() { emit(static_cast<uint8_t>(Opcode::OR)); }
    void xor_() { emit(static_cast<uint8_t>(Opcode::XOR)); }
    void not_() { emit(static_cast<uint8_t>(Opcode::NOT)); }
    void shl() { emit(static_cast<uint8_t>(Opcode::SHL)); }
    void shr() { emit(static_cast<uint8_t>(Opcode::SHR)); }
    
    void eq() { emit(static_cast<uint8_t>(Opcode::EQ)); }
    void ne() { emit(static_cast<uint8_t>(Opcode::NE)); }
    void lt() { emit(static_cast<uint8_t>(Opcode::LT)); }
    void le() { emit(static_cast<uint8_t>(Opcode::LE)); }
    void gt() { emit(static_cast<uint8_t>(Opcode::GT)); }
    void ge() { emit(static_cast<uint8_t>(Opcode::GE)); }
    
    void jmp(uint32_t addr) { emit(static_cast<uint8_t>(Opcode::JMP)); emit32(addr); }
    void jz(uint32_t addr) { emit(static_cast<uint8_t>(Opcode::JZ)); emit32(addr); }
    void jnz(uint32_t addr) { emit(static_cast<uint8_t>(Opcode::JNZ)); emit32(addr); }
    void call(uint32_t addr) { emit(static_cast<uint8_t>(Opcode::CALL)); emit32(addr); }
    void ret() { emit(static_cast<uint8_t>(Opcode::RET)); }
    
    void load(uint32_t addr) { emit(static_cast<uint8_t>(Opcode::LOAD)); emit32(addr); }
    void store(uint32_t addr) { emit(static_cast<uint8_t>(Opcode::STORE)); emit32(addr); }
    void load_reg(uint8_t reg) { emit(static_cast<uint8_t>(Opcode::LOAD_REG)); emit(reg); }
    void store_reg(uint8_t reg) { emit(static_cast<uint8_t>(Opcode::STORE_REG)); emit(reg); }
    
    void mov_imm(uint8_t reg, int64_t val) { emit(static_cast<uint8_t>(Opcode::MOV_IMM)); emit(reg); emit64(val); }
    void mov_reg(uint8_t dst, uint8_t src) { emit(static_cast<uint8_t>(Opcode::MOV_REG)); emit(dst); emit(src); }
    
    void syscall() { emit(static_cast<uint8_t>(Opcode::SYSCALL)); }
    void halt() { emit(static_cast<uint8_t>(Opcode::HALT)); }
    
    // Get compiled code
    const uint8_t* code() const { return code_.data(); }
    size_t size() const { return pos_; }
    
    void reset() { pos_ = 0; code_.fill(0); }
};

} // namespace os
