/**
 * Bytecode VM Tests
 * 
 * Tests the real bytecode interpreter implementation.
 */

#include "../os/Bytecode.h"
#include <iostream>
#include <cassert>

using namespace os;

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << std::endl; return false; } } while(0)

// Test basic arithmetic
bool test_arithmetic() {
    BytecodeAssembler asm_;
    
    // Program: push 10, push 20, add, halt (result on stack)
    asm_.push_imm(10);
    asm_.push_imm(20);
    asm_.add();
    asm_.pop_reg(0);  // Store result in R0
    asm_.push_imm(0); // Exit code
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    int exit_code = vm.run();
    
    TEST_ASSERT(exit_code == 0, "exit code should be 0");
    TEST_ASSERT(vm.getState().registers[0] == 30, "10 + 20 should be 30");
    
    return true;
}

// Test multiplication and subtraction
bool test_mul_sub() {
    BytecodeAssembler asm_;
    
    // Program: (5 * 7) - 3 = 32
    asm_.push_imm(5);
    asm_.push_imm(7);
    asm_.mul();
    asm_.push_imm(3);
    asm_.sub();
    asm_.pop_reg(0);
    asm_.push_imm(0);
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    vm.run();
    
    TEST_ASSERT(vm.getState().registers[0] == 32, "5*7-3 should be 32");
    
    return true;
}

// Test conditional jump
bool test_conditional_jump() {
    BytecodeAssembler asm_;
    
    // Program: if (10 == 10) R0 = 42 else R0 = 0
    asm_.push_imm(10);
    asm_.push_imm(10);
    asm_.eq();           // Push 1 (true)
    asm_.jz(30);         // Skip if zero (won't skip)
    asm_.mov_imm(0, 42); // R0 = 42
    asm_.push_imm(0);
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    vm.run();
    
    TEST_ASSERT(vm.getState().registers[0] == 42, "R0 should be 42");
    
    return true;
}

// Test loop (sum 1 to 10)
bool test_loop() {
    BytecodeAssembler asm_;
    
    // R0 = sum, R1 = counter
    // sum = 0, counter = 10
    // while (counter > 0): sum += counter; counter--
    
    asm_.mov_imm(0, 0);   // R0 = 0 (sum)
    asm_.mov_imm(1, 10);  // R1 = 10 (counter)
    
    size_t loop_start = asm_.pos();
    
    // Check counter > 0
    asm_.push_reg(1);
    asm_.push_imm(0);
    asm_.gt();
    size_t jz_pos = asm_.pos();
    asm_.jz(0);  // Will patch
    
    // sum += counter
    asm_.push_reg(0);
    asm_.push_reg(1);
    asm_.add();
    asm_.pop_reg(0);
    
    // counter--
    asm_.push_reg(1);
    asm_.push_imm(1);
    asm_.sub();
    asm_.pop_reg(1);
    
    // Jump back to loop start
    asm_.jmp(static_cast<uint32_t>(loop_start));
    
    size_t loop_end = asm_.pos();
    
    // Patch the JZ to jump here
    uint32_t* jz_target = const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(asm_.code() + jz_pos + 1));
    *jz_target = static_cast<uint32_t>(loop_end);
    
    asm_.push_imm(0);
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    vm.run();
    
    // Sum of 1 to 10 = 55
    TEST_ASSERT(vm.getState().registers[0] == 55, "sum(1..10) should be 55");
    
    return true;
}

// Test syscall (print)
bool test_syscall() {
    BytecodeAssembler asm_;
    
    // SYS_PRINT(42)
    asm_.mov_imm(0, 1);   // R0 = syscall number (SYS_PRINT)
    asm_.mov_imm(1, 42);  // R1 = value to print
    asm_.syscall();
    
    // SYS_PRINT_CHAR('\n')
    asm_.mov_imm(0, 2);   // R0 = SYS_PRINT_CHAR
    asm_.mov_imm(1, '\n');
    asm_.syscall();
    
    asm_.push_imm(0);
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    
    std::cout << "  [syscall output: ";
    int exit_code = vm.run();
    std::cout << "]" << std::endl;
    
    TEST_ASSERT(exit_code == 0, "exit code should be 0");
    
    return true;
}

// Test memory operations
bool test_memory() {
    BytecodeAssembler asm_;
    
    // Store 12345 at heap address 0, then load it back
    asm_.push_imm(12345);
    asm_.store(0);        // heap[0] = 12345
    asm_.load(0);         // push heap[0]
    asm_.pop_reg(0);      // R0 = heap[0]
    asm_.push_imm(0);
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    vm.run();
    
    TEST_ASSERT(vm.getState().registers[0] == 12345, "memory load should return 12345");
    
    return true;
}

// Test function call
bool test_call() {
    BytecodeAssembler asm_;
    
    // Simpler test: use registers for args instead of stack
    // R1 = 5, R2 = 3, call add_func, result in R0
    asm_.mov_imm(1, 5);   // R1 = 5
    asm_.mov_imm(2, 3);   // R2 = 3
    size_t call_pos = asm_.pos();
    asm_.call(0);         // Will patch target
    // After return, result is in R0
    asm_.push_imm(0);
    asm_.halt();
    
    // add_func: R0 = R1 + R2, return
    size_t func_addr = asm_.pos();
    asm_.push_reg(1);
    asm_.push_reg(2);
    asm_.add();
    asm_.pop_reg(0);      // R0 = R1 + R2
    asm_.ret();
    
    // Patch call target
    uint32_t* call_target = const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(asm_.code() + call_pos + 1));
    *call_target = static_cast<uint32_t>(func_addr);
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    vm.run();
    
    TEST_ASSERT(vm.getState().registers[0] == 8, "add_func(5,3) should be 8");
    
    return true;
}

// Test bitwise operations
bool test_bitwise() {
    BytecodeAssembler asm_;
    
    // (0xFF & 0x0F) | 0x30 = 0x3F
    asm_.push_imm(0xFF);
    asm_.push_imm(0x0F);
    asm_.and_();
    asm_.push_imm(0x30);
    asm_.or_();
    asm_.pop_reg(0);
    asm_.push_imm(0);
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    vm.run();
    
    TEST_ASSERT(vm.getState().registers[0] == 0x3F, "bitwise result should be 0x3F");
    
    return true;
}

// Test Fibonacci
bool test_fibonacci() {
    BytecodeAssembler asm_;
    
    // Compute fib(10) iteratively
    // R0 = fib(n-2), R1 = fib(n-1), R2 = counter, R3 = temp
    
    asm_.mov_imm(0, 0);   // fib(0) = 0
    asm_.mov_imm(1, 1);   // fib(1) = 1
    asm_.mov_imm(2, 10);  // n = 10
    
    size_t loop_start = asm_.pos();
    
    // if counter <= 1, exit loop
    asm_.push_reg(2);
    asm_.push_imm(1);
    asm_.le();
    size_t jnz_pos = asm_.pos();
    asm_.jnz(0);  // Will patch
    
    // temp = fib(n-1) + fib(n-2)
    asm_.push_reg(0);
    asm_.push_reg(1);
    asm_.add();
    asm_.pop_reg(3);
    
    // fib(n-2) = fib(n-1)
    asm_.mov_reg(0, 1);
    
    // fib(n-1) = temp
    asm_.mov_reg(1, 3);
    
    // counter--
    asm_.push_reg(2);
    asm_.push_imm(1);
    asm_.sub();
    asm_.pop_reg(2);
    
    asm_.jmp(static_cast<uint32_t>(loop_start));
    
    size_t loop_end = asm_.pos();
    
    // Patch JNZ
    uint32_t* jnz_target = const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(asm_.code() + jnz_pos + 1));
    *jnz_target = static_cast<uint32_t>(loop_end);
    
    // Result is in R1
    asm_.mov_reg(0, 1);
    asm_.push_imm(0);
    asm_.halt();
    
    BytecodeVM vm;
    vm.loadProgram(asm_.code(), asm_.size());
    vm.run();
    
    // fib(10) = 55
    TEST_ASSERT(vm.getState().registers[0] == 55, "fib(10) should be 55");
    
    return true;
}

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  RSE Bytecode VM Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    int passed = 0, failed = 0;
    
    #define RUN_TEST(name) do { \
        std::cout << "  " #name "... "; \
        if (name()) { std::cout << "PASS" << std::endl; passed++; } \
        else { std::cout << "FAIL" << std::endl; failed++; } \
    } while(0)
    
    RUN_TEST(test_arithmetic);
    RUN_TEST(test_mul_sub);
    RUN_TEST(test_conditional_jump);
    RUN_TEST(test_loop);
    RUN_TEST(test_syscall);
    RUN_TEST(test_memory);
    RUN_TEST(test_call);
    RUN_TEST(test_bitwise);
    RUN_TEST(test_fibonacci);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  PASSED: " << passed << "  FAILED: " << failed << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
