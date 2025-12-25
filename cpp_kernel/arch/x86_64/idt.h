#pragma once

/**
 * Interrupt Descriptor Table (IDT) for x86_64
 * Handles CPU exceptions and hardware interrupts
 */

#include <cstdint>

namespace arch {

#pragma pack(push, 1)

// IDT entry (16 bytes for 64-bit)
struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;           // Interrupt stack table (0-7)
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
};

// IDT pointer
struct IDTPointer {
    uint16_t limit;
    uint64_t base;
};

#pragma pack(pop)

// Gate types
namespace GateType {
    constexpr uint8_t INTERRUPT = 0x8E;  // Present, DPL=0, Interrupt Gate
    constexpr uint8_t TRAP = 0x8F;       // Present, DPL=0, Trap Gate
    constexpr uint8_t USER_INT = 0xEE;   // Present, DPL=3, Interrupt Gate
}

// Interrupt numbers
namespace IRQ {
    // CPU Exceptions (0-31)
    constexpr uint8_t DIVIDE_ERROR = 0;
    constexpr uint8_t DEBUG = 1;
    constexpr uint8_t NMI = 2;
    constexpr uint8_t BREAKPOINT = 3;
    constexpr uint8_t OVERFLOW = 4;
    constexpr uint8_t BOUND_RANGE = 5;
    constexpr uint8_t INVALID_OPCODE = 6;
    constexpr uint8_t DEVICE_NOT_AVAIL = 7;
    constexpr uint8_t DOUBLE_FAULT = 8;
    constexpr uint8_t COPROCESSOR_SEG = 9;
    constexpr uint8_t INVALID_TSS = 10;
    constexpr uint8_t SEGMENT_NOT_PRESENT = 11;
    constexpr uint8_t STACK_FAULT = 12;
    constexpr uint8_t GENERAL_PROTECTION = 13;
    constexpr uint8_t PAGE_FAULT = 14;
    constexpr uint8_t X87_FPU = 16;
    constexpr uint8_t ALIGNMENT_CHECK = 17;
    constexpr uint8_t MACHINE_CHECK = 18;
    constexpr uint8_t SIMD_FP = 19;
    constexpr uint8_t VIRTUALIZATION = 20;
    constexpr uint8_t SECURITY = 30;
    
    // Hardware IRQs (32-47 after PIC remap)
    constexpr uint8_t TIMER = 32;
    constexpr uint8_t KEYBOARD = 33;
    constexpr uint8_t CASCADE = 34;
    constexpr uint8_t COM2 = 35;
    constexpr uint8_t COM1 = 36;
    constexpr uint8_t LPT2 = 37;
    constexpr uint8_t FLOPPY = 38;
    constexpr uint8_t LPT1 = 39;
    constexpr uint8_t RTC = 40;
    constexpr uint8_t FREE1 = 41;
    constexpr uint8_t FREE2 = 42;
    constexpr uint8_t FREE3 = 43;
    constexpr uint8_t MOUSE = 44;
    constexpr uint8_t FPU = 45;
    constexpr uint8_t ATA1 = 46;
    constexpr uint8_t ATA2 = 47;
    
    // System calls
    constexpr uint8_t SYSCALL = 0x80;
}

// Interrupt frame pushed by CPU
struct InterruptFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

// Interrupt handler type
using InterruptHandler = void (*)(InterruptFrame* frame);

class IDT {
private:
    static constexpr size_t IDT_ENTRIES = 256;
    
    IDTEntry entries_[IDT_ENTRIES];
    IDTPointer pointer_;
    InterruptHandler handlers_[IDT_ENTRIES];
    
    void setEntry(uint8_t index, uint64_t handler, uint16_t selector, uint8_t type, uint8_t ist = 0) {
        entries_[index].offset_low = handler & 0xFFFF;
        entries_[index].offset_mid = (handler >> 16) & 0xFFFF;
        entries_[index].offset_high = (handler >> 32) & 0xFFFFFFFF;
        entries_[index].selector = selector;
        entries_[index].ist = ist;
        entries_[index].type_attr = type;
        entries_[index].reserved = 0;
    }

public:
    IDT() {
        __builtin_memset(entries_, 0, sizeof(entries_));
        __builtin_memset(handlers_, 0, sizeof(handlers_));
        
        pointer_.limit = sizeof(entries_) - 1;
        pointer_.base = reinterpret_cast<uint64_t>(&entries_);
    }
    
    void registerHandler(uint8_t index, uint64_t stub, InterruptHandler handler, 
                         uint8_t type = GateType::INTERRUPT, uint8_t ist = 0) {
        setEntry(index, stub, 0x08, type, ist);  // 0x08 = kernel code segment
        handlers_[index] = handler;
    }
    
    void load() {
        asm volatile("lidt %0" : : "m"(pointer_));
    }
    
    InterruptHandler getHandler(uint8_t index) const {
        return handlers_[index];
    }
    
    void dispatch(InterruptFrame* frame) {
        uint8_t int_no = frame->int_no & 0xFF;
        
        if (handlers_[int_no]) {
            handlers_[int_no](frame);
        } else {
            // Unhandled interrupt - panic for exceptions
            if (int_no < 32) {
                // CPU exception - kernel panic
                asm volatile("cli; hlt");
            }
        }
        
        // Send EOI for hardware interrupts
        if (int_no >= 32 && int_no < 48) {
            // PIC EOI
            if (int_no >= 40) {
                outb(0xA0, 0x20);  // Slave PIC
            }
            outb(0x20, 0x20);      // Master PIC
        }
    }
    
private:
    static inline void outb(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }
};

// Global IDT instance
inline IDT& getIDT() {
    static IDT idt;
    return idt;
}

// PIC (Programmable Interrupt Controller) management
class PIC {
public:
    static void remap(uint8_t offset1 = 32, uint8_t offset2 = 40) {
        uint8_t mask1 = inb(0x21);
        uint8_t mask2 = inb(0xA1);
        
        // Start initialization
        outb(0x20, 0x11);  // Master PIC
        outb(0xA0, 0x11);  // Slave PIC
        io_wait();
        
        // Set vector offsets
        outb(0x21, offset1);
        outb(0xA1, offset2);
        io_wait();
        
        // Configure cascading
        outb(0x21, 0x04);  // Slave at IRQ2
        outb(0xA1, 0x02);  // Cascade identity
        io_wait();
        
        // 8086 mode
        outb(0x21, 0x01);
        outb(0xA1, 0x01);
        io_wait();
        
        // Restore masks
        outb(0x21, mask1);
        outb(0xA1, mask2);
    }
    
    static void disable() {
        outb(0xA1, 0xFF);
        outb(0x21, 0xFF);
    }
    
    static void enableIRQ(uint8_t irq) {
        uint16_t port = (irq < 8) ? 0x21 : 0xA1;
        uint8_t mask = inb(port);
        mask &= ~(1 << (irq % 8));
        outb(port, mask);
    }
    
    static void disableIRQ(uint8_t irq) {
        uint16_t port = (irq < 8) ? 0x21 : 0xA1;
        uint8_t mask = inb(port);
        mask |= (1 << (irq % 8));
        outb(port, mask);
    }
    
    static void sendEOI(uint8_t irq) {
        if (irq >= 8) {
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20);
    }
    
private:
    static inline void outb(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }
    
    static inline uint8_t inb(uint16_t port) {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }
    
    static inline void io_wait() {
        outb(0x80, 0);
    }
};

} // namespace arch
