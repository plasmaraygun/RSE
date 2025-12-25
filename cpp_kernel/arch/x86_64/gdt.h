#pragma once

/**
 * Global Descriptor Table (GDT) for x86_64
 * Sets up segmentation for 64-bit long mode
 */

#include <cstdint>

namespace arch {

#pragma pack(push, 1)

// GDT entry (8 bytes)
struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
};

// GDT entry for 64-bit (16 bytes for TSS)
struct GDTEntry64 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

// GDT pointer
struct GDTPointer {
    uint16_t limit;
    uint64_t base;
};

// Task State Segment (TSS)
struct TSS {
    uint32_t reserved0;
    uint64_t rsp0;      // Stack pointer for ring 0
    uint64_t rsp1;      // Stack pointer for ring 1
    uint64_t rsp2;      // Stack pointer for ring 2
    uint64_t reserved1;
    uint64_t ist1;      // Interrupt stack table 1
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
};

#pragma pack(pop)

// GDT segment selectors
namespace Selector {
    constexpr uint16_t NULL_SEG = 0x00;
    constexpr uint16_t KERNEL_CODE = 0x08;
    constexpr uint16_t KERNEL_DATA = 0x10;
    constexpr uint16_t USER_CODE = 0x18 | 3;  // RPL 3
    constexpr uint16_t USER_DATA = 0x20 | 3;  // RPL 3
    constexpr uint16_t TSS_SEG = 0x28;
}

// Access byte flags
namespace Access {
    constexpr uint8_t PRESENT = 0x80;
    constexpr uint8_t RING0 = 0x00;
    constexpr uint8_t RING3 = 0x60;
    constexpr uint8_t SEGMENT = 0x10;
    constexpr uint8_t EXECUTABLE = 0x08;
    constexpr uint8_t DIRECTION = 0x04;
    constexpr uint8_t READWRITE = 0x02;
    constexpr uint8_t ACCESSED = 0x01;
    constexpr uint8_t TSS_AVAILABLE = 0x09;
}

// Granularity flags
namespace Granularity {
    constexpr uint8_t PAGE = 0x80;
    constexpr uint8_t LONG_MODE = 0x20;
    constexpr uint8_t SIZE_32 = 0x40;
}

class GDT {
private:
    static constexpr size_t GDT_ENTRIES = 7;  // null, kcode, kdata, ucode, udata, tss (2)
    
    GDTEntry entries_[GDT_ENTRIES];
    GDTPointer pointer_;
    TSS tss_;
    
    void setEntry(size_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
        entries_[index].base_low = base & 0xFFFF;
        entries_[index].base_middle = (base >> 16) & 0xFF;
        entries_[index].base_high = (base >> 24) & 0xFF;
        entries_[index].limit_low = limit & 0xFFFF;
        entries_[index].granularity = (limit >> 16) & 0x0F;
        entries_[index].granularity |= granularity & 0xF0;
        entries_[index].access = access;
    }
    
    void setTSSEntry(size_t index, uint64_t base, uint32_t limit) {
        auto* tss_entry = reinterpret_cast<GDTEntry64*>(&entries_[index]);
        tss_entry->limit_low = limit & 0xFFFF;
        tss_entry->base_low = base & 0xFFFF;
        tss_entry->base_middle = (base >> 16) & 0xFF;
        tss_entry->access = Access::PRESENT | Access::TSS_AVAILABLE;
        tss_entry->granularity = (limit >> 16) & 0x0F;
        tss_entry->base_high = (base >> 24) & 0xFF;
        tss_entry->base_upper = (base >> 32) & 0xFFFFFFFF;
        tss_entry->reserved = 0;
    }

public:
    GDT() {
        // Null descriptor
        setEntry(0, 0, 0, 0, 0);
        
        // Kernel code segment (64-bit)
        setEntry(1, 0, 0xFFFFF, 
                 Access::PRESENT | Access::SEGMENT | Access::EXECUTABLE | Access::READWRITE,
                 Granularity::PAGE | Granularity::LONG_MODE);
        
        // Kernel data segment
        setEntry(2, 0, 0xFFFFF,
                 Access::PRESENT | Access::SEGMENT | Access::READWRITE,
                 Granularity::PAGE | Granularity::LONG_MODE);
        
        // User code segment (64-bit)
        setEntry(3, 0, 0xFFFFF,
                 Access::PRESENT | Access::RING3 | Access::SEGMENT | Access::EXECUTABLE | Access::READWRITE,
                 Granularity::PAGE | Granularity::LONG_MODE);
        
        // User data segment
        setEntry(4, 0, 0xFFFFF,
                 Access::PRESENT | Access::RING3 | Access::SEGMENT | Access::READWRITE,
                 Granularity::PAGE | Granularity::LONG_MODE);
        
        // Initialize TSS
        __builtin_memset(&tss_, 0, sizeof(TSS));
        tss_.iopb_offset = sizeof(TSS);
        
        // TSS descriptor (takes 2 entries)
        setTSSEntry(5, reinterpret_cast<uint64_t>(&tss_), sizeof(TSS) - 1);
        
        // Set up pointer
        pointer_.limit = sizeof(entries_) - 1;
        pointer_.base = reinterpret_cast<uint64_t>(&entries_);
    }
    
    void load() {
        // Load GDT
        asm volatile("lgdt %0" : : "m"(pointer_));
        
        // Reload segment registers
        asm volatile(
            "mov %0, %%ds\n"
            "mov %0, %%es\n"
            "mov %0, %%fs\n"
            "mov %0, %%gs\n"
            "mov %0, %%ss\n"
            : : "r"(static_cast<uint16_t>(Selector::KERNEL_DATA))
        );
        
        // Reload CS via far return
        asm volatile(
            "pushq %0\n"
            "pushq $1f\n"
            "lretq\n"
            "1:\n"
            : : "i"(Selector::KERNEL_CODE)
        );
        
        // Load TSS
        asm volatile("ltr %0" : : "r"(static_cast<uint16_t>(Selector::TSS_SEG)));
    }
    
    void setKernelStack(uint64_t stack) {
        tss_.rsp0 = stack;
    }
    
    void setIST(int ist_num, uint64_t stack) {
        if (ist_num >= 1 && ist_num <= 7) {
            uint64_t* ist_ptr = &tss_.ist1;
            ist_ptr[ist_num - 1] = stack;
        }
    }
    
    TSS* getTSS() { return &tss_; }
};

// Global GDT instance
inline GDT& getGDT() {
    static GDT gdt;
    return gdt;
}

} // namespace arch
