/**
 * ARQON RSE Kernel Entry Point
 * Main kernel initialization and startup
 */

#include "multiboot.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/framebuffer.h"
#include "memory.h"
#include "vfs.h"
#include "syscalls.h"
#include "shell.h"

// External symbols from linker
extern "C" {
    extern uint64_t _kernel_start;
    extern uint64_t _kernel_end;
    extern uint64_t isr_stub_table[];
}

// Global boot info
static boot::BootInfo g_boot_info;

// Interrupt dispatcher (called from assembly)
extern "C" void interrupt_dispatch(arch::InterruptFrame* frame) {
    arch::getIDT().dispatch(frame);
}

// Exception handlers
static void handle_divide_error(arch::InterruptFrame* frame) {
    kpanic("Divide by zero at RIP=%p", frame->rip);
}

static void handle_debug(arch::InterruptFrame* frame) {
    klog("Debug exception at RIP=%p", frame->rip);
}

static void handle_breakpoint(arch::InterruptFrame* frame) {
    klog("Breakpoint at RIP=%p", frame->rip);
}

static void handle_invalid_opcode(arch::InterruptFrame* frame) {
    kpanic("Invalid opcode at RIP=%p", frame->rip);
}

static void handle_double_fault(arch::InterruptFrame* frame) {
    kpanic("Double fault! Error=%x RIP=%p", frame->error_code, frame->rip);
}

static void handle_gpf(arch::InterruptFrame* frame) {
    kpanic("General Protection Fault! Error=%x RIP=%p", frame->error_code, frame->rip);
}

static void handle_page_fault(arch::InterruptFrame* frame) {
    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    kpanic("Page Fault! Address=%p Error=%x RIP=%p", cr2, frame->error_code, frame->rip);
}

// Hardware interrupt handlers
static volatile uint64_t g_ticks = 0;

static void handle_timer(arch::InterruptFrame*) {
    g_ticks++;
}

static void handle_keyboard(arch::InterruptFrame*) {
    drivers::getKeyboard().handleInterrupt();
}

// Syscall handler
static void handle_syscall(arch::InterruptFrame* frame) {
    // RAX = syscall number
    // RDI, RSI, RDX, R10, R8, R9 = arguments
    uint64_t syscall_num = frame->rax;
    
    switch (syscall_num) {
        case 0: {  // sys_read(fd, buf, count)
            int fd = static_cast<int>(frame->rdi);
            char* buf = reinterpret_cast<char*>(frame->rsi);
            size_t count = frame->rdx;
            
            if (fd == 0) {  // stdin - read from keyboard buffer
                auto& kb = drivers::getKeyboard();
                size_t read = 0;
                while (read < count) {
                    char c = kb.getChar();
                    if (c == 0) break;  // No more input
                    buf[read++] = c;
                    if (c == '\n') break;  // Line complete
                }
                frame->rax = read;
            } else {
                frame->rax = static_cast<uint64_t>(-9);  // EBADF
            }
            break;
        }
        case 1: {  // sys_write(fd, buf, count)
            int fd = static_cast<int>(frame->rdi);
            const char* buf = reinterpret_cast<const char*>(frame->rsi);
            size_t count = frame->rdx;
            
            if (fd == 1 || fd == 2) {  // stdout/stderr
                auto& fb = drivers::getFramebuffer();
                for (size_t i = 0; i < count; i++) {
                    fb.putChar(buf[i]);
                }
                frame->rax = count;
            } else {
                frame->rax = static_cast<uint64_t>(-9);  // EBADF
            }
            break;
        }
        case 60: // sys_exit
            klog("Process exit with code %d", frame->rdi);
            asm volatile("cli; hlt");
            break;
        default:
            frame->rax = static_cast<uint64_t>(-38);  // ENOSYS
            break;
    }
}

// Initialize interrupts
static void init_interrupts() {
    klog("Initializing interrupts...");
    
    auto& idt = arch::getIDT();
    
    // Register CPU exception handlers
    idt.registerHandler(arch::IRQ::DIVIDE_ERROR, isr_stub_table[0], handle_divide_error);
    idt.registerHandler(arch::IRQ::DEBUG, isr_stub_table[1], handle_debug);
    idt.registerHandler(arch::IRQ::BREAKPOINT, isr_stub_table[3], handle_breakpoint, arch::GateType::TRAP);
    idt.registerHandler(arch::IRQ::INVALID_OPCODE, isr_stub_table[6], handle_invalid_opcode);
    idt.registerHandler(arch::IRQ::DOUBLE_FAULT, isr_stub_table[8], handle_double_fault, arch::GateType::INTERRUPT, 1);
    idt.registerHandler(arch::IRQ::GENERAL_PROTECTION, isr_stub_table[13], handle_gpf);
    idt.registerHandler(arch::IRQ::PAGE_FAULT, isr_stub_table[14], handle_page_fault);
    
    // Remap PIC
    arch::PIC::remap(32, 40);
    
    // Register hardware interrupt handlers
    idt.registerHandler(arch::IRQ::TIMER, isr_stub_table[32], handle_timer);
    idt.registerHandler(arch::IRQ::KEYBOARD, isr_stub_table[33], handle_keyboard);
    
    // Register syscall handler
    idt.registerHandler(arch::IRQ::SYSCALL, isr_stub_table[128], handle_syscall, arch::GateType::USER_INT);
    
    // Load IDT
    idt.load();
    
    // Enable timer and keyboard IRQs
    arch::PIC::enableIRQ(0);  // Timer
    arch::PIC::enableIRQ(1);  // Keyboard
    
    klog("Interrupts initialized");
}

// Print memory map
static void print_memory_map() {
    klog("Memory map (%d entries):", g_boot_info.mmap_count);
    
    uint64_t total_available = 0;
    for (size_t i = 0; i < g_boot_info.mmap_count; i++) {
        const auto& entry = g_boot_info.mmap[i];
        const char* type_str;
        
        switch (static_cast<boot::MemoryType>(entry.type)) {
            case boot::MemoryType::AVAILABLE: 
                type_str = "Available"; 
                total_available += entry.length;
                break;
            case boot::MemoryType::RESERVED: type_str = "Reserved"; break;
            case boot::MemoryType::ACPI_RECLAIMABLE: type_str = "ACPI Reclaimable"; break;
            case boot::MemoryType::ACPI_NVS: type_str = "ACPI NVS"; break;
            case boot::MemoryType::BAD_MEMORY: type_str = "Bad"; break;
            default: type_str = "Unknown"; break;
        }
        
        klog("  %p - %p [%s]", 
                     entry.base_addr, 
                     entry.base_addr + entry.length,
                     type_str);
    }
    
    klog("Total available: %d MB", total_available / (1024 * 1024));
}

// Print banner
static void print_banner() {
    const char* banner = R"(
    _    ____   ___   ___  _   _   ____  ____  _____ 
   / \  |  _ \ / _ \ / _ \| \ | | |  _ \/ ___|| ____|
  / _ \ | |_) | | | | | | |  \| | | |_) \___ \|  _|  
 / ___ \|  _ <| |_| | |_| | |\  | |  _ < ___) | |___ 
/_/   \_\_| \_\\__\_\\___/|_| \_| |_| \_\____/|_____|
                                                      
    Recursive Spatial Engine - Real OS Kernel v0.1
)";
    
    auto& fb = drivers::getFramebuffer();
    fb.setColor(0x00FF00, 0x000000);  // Green on black
    fb.write(banner);
    fb.setColor(0xFFFFFF, 0x000000);  // White on black
}

// Shell loop
static void shell_loop() {
    auto& serial = drivers::getSerial();
    auto& fb = drivers::getFramebuffer();
    auto& kbd = drivers::getKeyboard();
    
    char cmd[256];
    size_t cmd_len = 0;
    
    fb.write("\nARQON> ");
    serial.write("\nARQON> ");
    
    while (true) {
        char c = kbd.getchar();
        
        if (c == '\n' || c == '\r') {
            fb.putchar('\n');
            serial.putchar('\n');
            cmd[cmd_len] = '\0';
            
            // Process command
            if (cmd_len > 0) {
                if (__builtin_strcmp(cmd, "help") == 0) {
                    fb.write("Commands: help, info, mem, ticks, clear, halt\n");
                } else if (__builtin_strcmp(cmd, "info") == 0) {
                    fb.printf("ARQON RSE Kernel v0.1\n");
                    fb.printf("Kernel: %p - %p\n", &_kernel_start, &_kernel_end);
                    fb.printf("Framebuffer: %dx%d\n", fb.getWidth(), fb.getHeight());
                } else if (__builtin_strcmp(cmd, "mem") == 0) {
                    fb.printf("Total memory: %d MB\n", g_boot_info.total_memory / (1024 * 1024));
                } else if (__builtin_strcmp(cmd, "ticks") == 0) {
                    fb.printf("Timer ticks: %d\n", g_ticks);
                } else if (__builtin_strcmp(cmd, "clear") == 0) {
                    fb.clear();
                } else if (__builtin_strcmp(cmd, "halt") == 0) {
                    fb.write("Halting system...\n");
                    asm volatile("cli; hlt");
                } else {
                    fb.printf("Unknown command: %s\n", cmd);
                }
            }
            
            cmd_len = 0;
            fb.write("ARQON> ");
            serial.write("ARQON> ");
            
        } else if (c == '\b' || c == 127) {
            if (cmd_len > 0) {
                cmd_len--;
                fb.putchar('\b');
                fb.putchar(' ');
                fb.putchar('\b');
            }
        } else if (c >= 32 && c < 127 && cmd_len < 255) {
            cmd[cmd_len++] = c;
            fb.putchar(c);
            serial.putchar(c);
        }
    }
}

// Kernel main entry point
extern "C" void kernel_main(uint32_t magic, void* mbi) {
    // Initialize serial first for early debug output
    auto& serial = drivers::getSerial();
    serial.init(115200);
    
    klog("ARQON RSE Kernel starting...");
    klog("Multiboot magic: %x", magic);
    
    // Verify multiboot
    if (magic != boot::MULTIBOOT2_MAGIC) {
        kpanic("Invalid multiboot magic: %x (expected %x)", magic, boot::MULTIBOOT2_MAGIC);
    }
    
    // Parse boot info
    klog("Parsing multiboot info at %p", mbi);
    g_boot_info = boot::parse_multiboot_info(mbi);
    
    klog("Total memory: %d MB", g_boot_info.total_memory / (1024 * 1024));
    
    if (g_boot_info.cmdline) {
        klog("Command line: %s", g_boot_info.cmdline);
    }
    
    // Initialize GDT
    klog("Initializing GDT...");
    arch::getGDT().load();
    klog("GDT loaded");
    
    // Initialize interrupts
    init_interrupts();
    
    // Initialize keyboard
    klog("Initializing keyboard...");
    drivers::getKeyboard().init();
    
    // Initialize framebuffer - always use VGA text mode for now
    // (graphical framebuffer requires page table mapping for MMIO region)
    klog("Initializing framebuffer...");
    auto& fb = drivers::getFramebuffer();
    fb.initVGA();
    klog("Using VGA text mode");
    
    // Phase 6.2: Initialize memory management
    klog("Initializing memory management...");
    // Find available memory region from memory map
    uint64_t mem_base = 0x200000;  // Start at 2MB (after kernel)
    uint64_t mem_size = g_boot_info.total_memory > 0x200000 ? 
                        g_boot_info.total_memory - 0x200000 : 0x1000000;
    mem::g_phys_alloc.init(mem_base, mem_size);
    
    // Initialize kernel heap (1MB at 16MB mark)
    mem::g_heap.init(0x1000000, 0x100000);
    klog("Memory: %d KB available", mem::g_phys_alloc.available() * 4);
    
    // Phase 6.3: Initialize VFS
    klog("Initializing filesystem...");
    vfs::g_fs.init();
    klog("VFS initialized with %d files", vfs::g_fs.file_count());
    
    // Phase 6.4: Initialize process table
    klog("Initializing process table...");
    sys::init_procs();
    klog("Process table ready, init pid=%d", sys::g_current_pid);
    
    // Print banner
    print_banner();
    
    // Print memory map
    print_memory_map();
    
    // Enable interrupts
    klog("Enabling interrupts...");
    asm volatile("sti");
    
    klog("Kernel initialization complete!");
    klog("Phases 6.2-6.5 active: Memory, VFS, Syscalls, Shell");
    
    // Phase 6.5: Enter shell
    shell::g_shell.init();
    shell::g_shell.run();
    
    // Should never reach here
    kpanic("Kernel main returned!");
}
