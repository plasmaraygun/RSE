#pragma once
/**
 * Simple Shell for ARQON Kernel
 * Phase 6.5: Basic userspace shell
 */

#include <cstdint>
#include "memory.h"
#include "vfs.h"
#include "syscalls.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/framebuffer.h"

namespace shell {

class Shell {
private:
    char cmd_[256];
    uint32_t cmd_len_;
    bool running_;
    
    bool str_eq(const char* a, const char* b) {
        while (*a && *b) {
            if (*a++ != *b++) return false;
        }
        return *a == *b;
    }
    
    bool str_starts(const char* str, const char* prefix) {
        while (*prefix) {
            if (*str++ != *prefix++) return false;
        }
        return true;
    }
    
    void print(const char* s) {
        auto& serial = drivers::getSerial();
        auto& fb = drivers::getFramebuffer();
        while (*s) {
            serial.putchar(*s);
            fb.putchar(*s);
            s++;
        }
    }
    
    void print_num(uint64_t n) {
        char buf[32];
        int i = 0;
        if (n == 0) {
            print("0");
            return;
        }
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
        while (i > 0) {
            char c[2] = { buf[--i], '\0' };
            print(c);
        }
    }
    
    void cmd_help() {
        print("ARQON Shell Commands:\n");
        print("  help       - Show this help\n");
        print("  info       - System information\n");
        print("  mem        - Memory statistics\n");
        print("  files      - List files\n");
        print("  cat <file> - Display file contents\n");
        print("  echo <msg> - Print message\n");
        print("  uname      - System name\n");
        print("  pwd        - Current directory\n");
        print("  clear      - Clear screen\n");
        print("  halt       - Shutdown system\n");
    }
    
    void cmd_info() {
        print("ARQON RSE Kernel v0.1.0\n");
        print("Architecture: x86_64\n");
        print("Phases: 6.2-6.5 (Memory, VFS, I/O, Userspace)\n");
    }
    
    void cmd_mem() {
        print("Physical Memory:\n");
        print("  Available: ");
        print_num(mem::g_phys_alloc.available());
        print(" frames (");
        print_num(mem::g_phys_alloc.available() * 4);
        print(" KB)\n");
        print("  Total: ");
        print_num(mem::g_phys_alloc.total());
        print(" frames (");
        print_num(mem::g_phys_alloc.total() * 4);
        print(" KB)\n");
    }
    
    void cmd_files() {
        print("Files: ");
        print_num(vfs::g_fs.file_count());
        print(" in filesystem\n");
        print("  /\n");
        print("  /dev\n");
    }
    
    void cmd_cat(const char* filename) {
        int32_t fd = vfs::g_fs.open(filename, vfs::O_RDONLY);
        if (fd < 0) {
            print("Error: File not found: ");
            print(filename);
            print("\n");
            return;
        }
        
        char buf[256];
        int64_t n;
        while ((n = vfs::g_fs.read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            print(buf);
        }
        print("\n");
        vfs::g_fs.close(fd);
    }
    
    void cmd_echo(const char* msg) {
        print(msg);
        print("\n");
    }
    
    void cmd_uname() {
        sys::UtsName uname;
        sys::handle_syscall(sys::SYS_UNAME, reinterpret_cast<uint64_t>(&uname), 0, 0, 0, 0);
        print(uname.sysname);
        print(" ");
        print(uname.nodename);
        print(" ");
        print(uname.release);
        print(" ");
        print(uname.version);
        print(" ");
        print(uname.machine);
        print("\n");
    }
    
    void cmd_pwd() {
        if (sys::g_current_pid < sys::MAX_PROCS) {
            print(sys::g_procs[sys::g_current_pid].cwd);
            print("\n");
        }
    }
    
    void cmd_clear() {
        drivers::getFramebuffer().clear();
    }
    
    void process_command() {
        cmd_[cmd_len_] = '\0';
        
        // Skip leading whitespace
        const char* cmd = cmd_;
        while (*cmd == ' ') cmd++;
        
        if (cmd[0] == '\0') {
            return;
        } else if (str_eq(cmd, "help")) {
            cmd_help();
        } else if (str_eq(cmd, "info")) {
            cmd_info();
        } else if (str_eq(cmd, "mem")) {
            cmd_mem();
        } else if (str_eq(cmd, "files")) {
            cmd_files();
        } else if (str_starts(cmd, "cat ")) {
            cmd_cat(cmd + 4);
        } else if (str_starts(cmd, "echo ")) {
            cmd_echo(cmd + 5);
        } else if (str_eq(cmd, "uname") || str_eq(cmd, "uname -a")) {
            cmd_uname();
        } else if (str_eq(cmd, "pwd")) {
            cmd_pwd();
        } else if (str_eq(cmd, "clear")) {
            cmd_clear();
        } else if (str_eq(cmd, "halt") || str_eq(cmd, "shutdown")) {
            print("Halting system...\n");
            running_ = false;
            asm volatile("cli; hlt");
        } else {
            print("Unknown command: ");
            print(cmd);
            print("\nType 'help' for commands.\n");
        }
    }

public:
    void init() {
        cmd_len_ = 0;
        running_ = true;
    }
    
    void run() {
        auto& serial = drivers::getSerial();
        auto& fb = drivers::getFramebuffer();
        auto& kbd = drivers::getKeyboard();
        
        print("\n");
        print("====================================\n");
        print("  ARQON RSE Kernel v0.1.0\n");
        print("  Phases 6.2-6.5 Complete\n");
        print("====================================\n");
        print("\nType 'help' for available commands.\n\n");
        
        while (running_) {
            print("arqon$ ");
            cmd_len_ = 0;
            
            // Read command
            while (running_) {
                char c = kbd.getchar();
                
                if (c == '\n' || c == '\r') {
                    serial.putchar('\n');
                    fb.putchar('\n');
                    break;
                } else if (c == '\b' || c == 127) {
                    if (cmd_len_ > 0) {
                        cmd_len_--;
                        serial.putchar('\b');
                        serial.putchar(' ');
                        serial.putchar('\b');
                        fb.putchar('\b');
                        fb.putchar(' ');
                        fb.putchar('\b');
                    }
                } else if (c >= 32 && c < 127 && cmd_len_ < 255) {
                    cmd_[cmd_len_++] = c;
                    serial.putchar(c);
                    fb.putchar(c);
                }
            }
            
            process_command();
        }
    }
};

extern Shell g_shell;

} // namespace shell
