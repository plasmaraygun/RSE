#pragma once

/**
 * BraidShell Commands
 * Full set of shell utilities for the ARQON OS
 */

#include <cstdint>
#include "../os/VFS.h"
#include "../os/OSProcess.h"
#include "../drivers/framebuffer.h"

namespace shell {

// Forward declarations
class Shell;

// Command function type
using CommandFunc = int (*)(Shell* sh, int argc, char** argv);

// Command entry
struct Command {
    const char* name;
    const char* description;
    CommandFunc func;
};

// Color codes for visual output
namespace Color {
    constexpr uint32_t DIR = 0x5555FF;      // Blue for directories
    constexpr uint32_t EXEC = 0x55FF55;     // Green for executables
    constexpr uint32_t LINK = 0x55FFFF;     // Cyan for symlinks
    constexpr uint32_t FILE = 0xFFFFFF;     // White for regular files
    constexpr uint32_t ERROR = 0xFF5555;    // Red for errors
    constexpr uint32_t HEADER = 0xFFFF55;   // Yellow for headers
    constexpr uint32_t HIGHLIGHT = 0xFF55FF; // Magenta for highlights
}

// ============================================================================
// ls - List directory contents with visual flair
// ============================================================================
inline int cmd_ls(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    const char* path = (argc > 1) ? argv[1] : "/";
    
    // Header
    fb.setColor(Color::HEADER, 0);
    fb.printf("Directory: %s\n", path);
    fb.setColor(Color::FILE, 0);
    fb.write("────────────────────────────────────────\n");
    
    // Get directory listing from VFS
    os::VFS& vfs = os::getVFS();
    auto* dir = vfs.opendir(path);
    
    if (!dir) {
        fb.setColor(Color::ERROR, 0);
        fb.printf("ls: cannot access '%s': No such directory\n", path);
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    // Column formatting
    int col = 0;
    const int cols_per_row = 4;
    const int col_width = 20;
    
    os::DirEntry entry;
    size_t total_files = 0;
    size_t total_size = 0;
    
    while (vfs.readdir(dir, &entry)) {
        // Set color based on type
        switch (entry.type) {
            case os::FileType::DIRECTORY:
                fb.setColor(Color::DIR, 0);
                break;
            case os::FileType::SYMLINK:
                fb.setColor(Color::LINK, 0);
                break;
            default:
                if (entry.mode & 0111) {  // Executable
                    fb.setColor(Color::EXEC, 0);
                } else {
                    fb.setColor(Color::FILE, 0);
                }
                break;
        }
        
        // Print entry with padding
        int len = 0;
        const char* name = entry.name;
        while (name[len]) len++;
        
        fb.write(entry.name);
        
        // Add type indicator
        if (entry.type == os::FileType::DIRECTORY) {
            fb.write("/");
            len++;
        } else if (entry.mode & 0111) {
            fb.write("*");
            len++;
        }
        
        // Pad to column width
        for (int i = len; i < col_width; i++) {
            fb.putchar(' ');
        }
        
        col++;
        if (col >= cols_per_row) {
            fb.putchar('\n');
            col = 0;
        }
        
        total_files++;
        total_size += entry.size;
    }
    
    if (col > 0) fb.putchar('\n');
    
    vfs.closedir(dir);
    
    // Summary
    fb.setColor(Color::FILE, 0);
    fb.write("────────────────────────────────────────\n");
    fb.printf("%d items, %d bytes total\n", total_files, total_size);
    
    return 0;
}

// ============================================================================
// cat - Display file contents
// ============================================================================
inline int cmd_cat(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    if (argc < 2) {
        fb.setColor(Color::ERROR, 0);
        fb.write("Usage: cat <file> [file2...]\n");
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    os::VFS& vfs = os::getVFS();
    
    for (int i = 1; i < argc; i++) {
        int fd = vfs.open(argv[i], os::O_RDONLY);
        
        if (fd < 0) {
            fb.setColor(Color::ERROR, 0);
            fb.printf("cat: %s: No such file\n", argv[i]);
            fb.setColor(Color::FILE, 0);
            continue;
        }
        
        // Print filename header if multiple files
        if (argc > 2) {
            fb.setColor(Color::HEADER, 0);
            fb.printf("==> %s <==\n", argv[i]);
            fb.setColor(Color::FILE, 0);
        }
        
        // Read and display file
        char buf[512];
        ssize_t n;
        while ((n = vfs.read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            fb.write(buf);
        }
        
        vfs.close(fd);
        
        // Ensure newline at end
        fb.putchar('\n');
    }
    
    return 0;
}

// ============================================================================
// ps - Process viewer (htop-style)
// ============================================================================
inline int cmd_ps(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    // Header with colors
    fb.setColor(Color::HEADER, 0);
    fb.write("  PID  PPID  STATE    CPU%%  MEM%%   TIME    COMMAND\n");
    fb.setColor(Color::FILE, 0);
    fb.write("─────────────────────────────────────────────────────\n");
    
    // Get process list
    os::ProcessManager& pm = os::getProcessManager();
    
    size_t count = 0;
    for (size_t i = 0; i < pm.maxProcesses(); i++) {
        os::Process* proc = pm.getProcess(i);
        if (!proc || proc->state == os::ProcessState::UNUSED) continue;
        
        // Color based on state
        switch (proc->state) {
            case os::ProcessState::RUNNING:
                fb.setColor(Color::EXEC, 0);
                break;
            case os::ProcessState::BLOCKED:
                fb.setColor(Color::LINK, 0);
                break;
            case os::ProcessState::ZOMBIE:
                fb.setColor(Color::ERROR, 0);
                break;
            default:
                fb.setColor(Color::FILE, 0);
                break;
        }
        
        const char* state_str;
        switch (proc->state) {
            case os::ProcessState::READY: state_str = "READY"; break;
            case os::ProcessState::RUNNING: state_str = "RUN"; break;
            case os::ProcessState::BLOCKED: state_str = "BLOCK"; break;
            case os::ProcessState::ZOMBIE: state_str = "ZOMBIE"; break;
            default: state_str = "???"; break;
        }
        
        fb.printf("%5d %5d  %-7s %4d%%  %4d%%  %6ds  %s\n",
                 proc->pid,
                 proc->ppid,
                 state_str,
                 proc->cpu_percent,
                 proc->mem_percent,
                 proc->cpu_time / 1000,
                 proc->name);
        
        count++;
    }
    
    fb.setColor(Color::FILE, 0);
    fb.write("─────────────────────────────────────────────────────\n");
    fb.printf("Total: %d processes\n", count);
    
    return 0;
}

// ============================================================================
// echo - Print arguments
// ============================================================================
inline int cmd_echo(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    bool no_newline = false;
    int start = 1;
    
    // Check for -n flag
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n' && argv[1][2] == '\0') {
        no_newline = true;
        start = 2;
    }
    
    for (int i = start; i < argc; i++) {
        if (i > start) fb.putchar(' ');
        
        // Handle escape sequences
        const char* s = argv[i];
        while (*s) {
            if (*s == '\\' && *(s + 1)) {
                s++;
                switch (*s) {
                    case 'n': fb.putchar('\n'); break;
                    case 't': fb.putchar('\t'); break;
                    case 'r': fb.putchar('\r'); break;
                    case '\\': fb.putchar('\\'); break;
                    default: fb.putchar('\\'); fb.putchar(*s); break;
                }
            } else {
                fb.putchar(*s);
            }
            s++;
        }
    }
    
    if (!no_newline) fb.putchar('\n');
    
    return 0;
}

// ============================================================================
// mkdir - Create directory
// ============================================================================
inline int cmd_mkdir(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    if (argc < 2) {
        fb.setColor(Color::ERROR, 0);
        fb.write("Usage: mkdir <directory> [directory2...]\n");
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    os::VFS& vfs = os::getVFS();
    int errors = 0;
    
    for (int i = 1; i < argc; i++) {
        if (vfs.mkdir(argv[i], 0755) < 0) {
            fb.setColor(Color::ERROR, 0);
            fb.printf("mkdir: cannot create '%s': ", argv[i]);
            
            // Check if exists
            os::Stat st;
            if (vfs.stat(argv[i], &st) == 0) {
                fb.write("File exists\n");
            } else {
                fb.write("Permission denied or invalid path\n");
            }
            fb.setColor(Color::FILE, 0);
            errors++;
        } else {
            fb.setColor(Color::EXEC, 0);
            fb.printf("Created directory '%s'\n", argv[i]);
            fb.setColor(Color::FILE, 0);
        }
    }
    
    return errors > 0 ? 1 : 0;
}

// ============================================================================
// rm - Remove files/directories
// ============================================================================
inline int cmd_rm(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    if (argc < 2) {
        fb.setColor(Color::ERROR, 0);
        fb.write("Usage: rm [-r] <file> [file2...]\n");
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    os::VFS& vfs = os::getVFS();
    bool recursive = false;
    int start = 1;
    
    // Check for -r flag
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'r') {
        recursive = true;
        start = 2;
    }
    
    int errors = 0;
    
    for (int i = start; i < argc; i++) {
        os::Stat st;
        if (vfs.stat(argv[i], &st) < 0) {
            fb.setColor(Color::ERROR, 0);
            fb.printf("rm: cannot remove '%s': No such file\n", argv[i]);
            fb.setColor(Color::FILE, 0);
            errors++;
            continue;
        }
        
        if (st.type == os::FileType::DIRECTORY && !recursive) {
            fb.setColor(Color::ERROR, 0);
            fb.printf("rm: cannot remove '%s': Is a directory (use -r)\n", argv[i]);
            fb.setColor(Color::FILE, 0);
            errors++;
            continue;
        }
        
        if (vfs.unlink(argv[i]) < 0) {
            fb.setColor(Color::ERROR, 0);
            fb.printf("rm: cannot remove '%s': Permission denied\n", argv[i]);
            fb.setColor(Color::FILE, 0);
            errors++;
        }
    }
    
    return errors > 0 ? 1 : 0;
}

// ============================================================================
// pwd - Print working directory
// ============================================================================
inline int cmd_pwd(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    fb.printf("%s\n", os::getVFS().getcwd());
    return 0;
}

// ============================================================================
// cd - Change directory
// ============================================================================
inline int cmd_cd(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    const char* path = (argc > 1) ? argv[1] : "/";
    
    if (os::getVFS().chdir(path) < 0) {
        fb.setColor(Color::ERROR, 0);
        fb.printf("cd: %s: No such directory\n", path);
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    return 0;
}

// ============================================================================
// touch - Create empty file or update timestamp
// ============================================================================
inline int cmd_touch(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    if (argc < 2) {
        fb.setColor(Color::ERROR, 0);
        fb.write("Usage: touch <file> [file2...]\n");
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    os::VFS& vfs = os::getVFS();
    
    for (int i = 1; i < argc; i++) {
        int fd = vfs.open(argv[i], os::O_CREAT | os::O_WRONLY);
        if (fd >= 0) {
            vfs.close(fd);
        } else {
            fb.setColor(Color::ERROR, 0);
            fb.printf("touch: cannot create '%s'\n", argv[i]);
            fb.setColor(Color::FILE, 0);
        }
    }
    
    return 0;
}

// ============================================================================
// cp - Copy file
// ============================================================================
inline int cmd_cp(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    if (argc < 3) {
        fb.setColor(Color::ERROR, 0);
        fb.write("Usage: cp <source> <dest>\n");
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    os::VFS& vfs = os::getVFS();
    
    int src = vfs.open(argv[1], os::O_RDONLY);
    if (src < 0) {
        fb.setColor(Color::ERROR, 0);
        fb.printf("cp: cannot open '%s'\n", argv[1]);
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    int dst = vfs.open(argv[2], os::O_CREAT | os::O_WRONLY | os::O_TRUNC);
    if (dst < 0) {
        fb.setColor(Color::ERROR, 0);
        fb.printf("cp: cannot create '%s'\n", argv[2]);
        fb.setColor(Color::FILE, 0);
        vfs.close(src);
        return 1;
    }
    
    char buf[4096];
    ssize_t n;
    size_t total = 0;
    
    while ((n = vfs.read(src, buf, sizeof(buf))) > 0) {
        vfs.write(dst, buf, n);
        total += n;
    }
    
    vfs.close(src);
    vfs.close(dst);
    
    fb.printf("Copied %d bytes\n", total);
    return 0;
}

// ============================================================================
// mv - Move/rename file
// ============================================================================
inline int cmd_mv(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    if (argc < 3) {
        fb.setColor(Color::ERROR, 0);
        fb.write("Usage: mv <source> <dest>\n");
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    os::VFS& vfs = os::getVFS();
    
    if (vfs.rename(argv[1], argv[2]) < 0) {
        fb.setColor(Color::ERROR, 0);
        fb.printf("mv: cannot move '%s' to '%s'\n", argv[1], argv[2]);
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    return 0;
}

// ============================================================================
// head - Display first lines of file
// ============================================================================
inline int cmd_head(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    if (argc < 2) {
        fb.setColor(Color::ERROR, 0);
        fb.write("Usage: head [-n lines] <file>\n");
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    int lines = 10;
    const char* file = argv[1];
    
    // Parse -n option
    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'n') {
        lines = 0;
        const char* n = argv[2];
        while (*n >= '0' && *n <= '9') {
            lines = lines * 10 + (*n - '0');
            n++;
        }
        file = argv[3];
    }
    
    os::VFS& vfs = os::getVFS();
    int fd = vfs.open(file, os::O_RDONLY);
    
    if (fd < 0) {
        fb.setColor(Color::ERROR, 0);
        fb.printf("head: cannot open '%s'\n", file);
        fb.setColor(Color::FILE, 0);
        return 1;
    }
    
    int line_count = 0;
    char c;
    while (line_count < lines && vfs.read(fd, &c, 1) == 1) {
        fb.putchar(c);
        if (c == '\n') line_count++;
    }
    
    vfs.close(fd);
    return 0;
}

// ============================================================================
// clear - Clear screen
// ============================================================================
inline int cmd_clear(Shell* sh, int argc, char** argv) {
    drivers::getFramebuffer().clear();
    return 0;
}

// ============================================================================
// help - Display available commands
// ============================================================================
inline int cmd_help(Shell* sh, int argc, char** argv);

// ============================================================================
// exit/halt - Exit shell or halt system
// ============================================================================
inline int cmd_exit(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    fb.write("Halting system...\n");
    asm volatile("cli; hlt");
    return 0;
}

// ============================================================================
// Command table
// ============================================================================
inline const Command COMMANDS[] = {
    {"ls",      "List directory contents",          cmd_ls},
    {"cat",     "Display file contents",            cmd_cat},
    {"ps",      "Show running processes",           cmd_ps},
    {"echo",    "Print arguments",                  cmd_echo},
    {"mkdir",   "Create directory",                 cmd_mkdir},
    {"rm",      "Remove files/directories",         cmd_rm},
    {"pwd",     "Print working directory",          cmd_pwd},
    {"cd",      "Change directory",                 cmd_cd},
    {"touch",   "Create empty file",                cmd_touch},
    {"cp",      "Copy file",                        cmd_cp},
    {"mv",      "Move/rename file",                 cmd_mv},
    {"head",    "Display first lines of file",     cmd_head},
    {"clear",   "Clear screen",                     cmd_clear},
    {"help",    "Show this help",                   cmd_help},
    {"exit",    "Halt the system",                  cmd_exit},
    {"halt",    "Halt the system",                  cmd_exit},
    {nullptr,   nullptr,                            nullptr}
};

inline int cmd_help(Shell* sh, int argc, char** argv) {
    auto& fb = drivers::getFramebuffer();
    
    fb.setColor(Color::HEADER, 0);
    fb.write("BraidShell Commands:\n");
    fb.setColor(Color::FILE, 0);
    fb.write("────────────────────────────────────────\n");
    
    for (const Command* cmd = COMMANDS; cmd->name; cmd++) {
        fb.setColor(Color::EXEC, 0);
        fb.printf("  %-10s", cmd->name);
        fb.setColor(Color::FILE, 0);
        fb.printf(" %s\n", cmd->description);
    }
    
    return 0;
}

// ============================================================================
// Shell class
// ============================================================================
class Shell {
private:
    char cwd_[256];
    char line_[512];
    size_t line_len_;
    char* argv_[32];
    int argc_;
    
    void parseLine() {
        argc_ = 0;
        char* p = line_;
        
        while (*p && argc_ < 31) {
            // Skip whitespace
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            
            // Handle quotes
            if (*p == '"') {
                p++;
                argv_[argc_++] = p;
                while (*p && *p != '"') p++;
                if (*p) *p++ = '\0';
            } else {
                argv_[argc_++] = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p) *p++ = '\0';
            }
        }
        argv_[argc_] = nullptr;
    }
    
    CommandFunc findCommand(const char* name) {
        for (const Command* cmd = COMMANDS; cmd->name; cmd++) {
            // Simple strcmp
            const char* a = cmd->name;
            const char* b = name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == *b) return cmd->func;
        }
        return nullptr;
    }

public:
    Shell() : line_len_(0), argc_(0) {
        cwd_[0] = '/';
        cwd_[1] = '\0';
    }
    
    void run() {
        auto& fb = drivers::getFramebuffer();
        auto& kbd = drivers::getKeyboard();
        
        fb.setColor(Color::EXEC, 0);
        fb.write("\nBraidShell v1.0 - Type 'help' for commands\n\n");
        fb.setColor(Color::FILE, 0);
        
        while (true) {
            // Print prompt
            fb.setColor(Color::DIR, 0);
            fb.write(cwd_);
            fb.setColor(Color::FILE, 0);
            fb.write(" $ ");
            
            // Read line
            line_len_ = 0;
            while (true) {
                char c = kbd.getchar();
                
                if (c == '\n' || c == '\r') {
                    fb.putchar('\n');
                    line_[line_len_] = '\0';
                    break;
                } else if (c == '\b' || c == 127) {
                    if (line_len_ > 0) {
                        line_len_--;
                        fb.putchar('\b');
                        fb.putchar(' ');
                        fb.putchar('\b');
                    }
                } else if (c >= 32 && c < 127 && line_len_ < 510) {
                    line_[line_len_++] = c;
                    fb.putchar(c);
                }
            }
            
            if (line_len_ == 0) continue;
            
            // Parse and execute
            parseLine();
            if (argc_ == 0) continue;
            
            CommandFunc func = findCommand(argv_[0]);
            if (func) {
                func(this, argc_, argv_);
            } else {
                fb.setColor(Color::ERROR, 0);
                fb.printf("Unknown command: %s\n", argv_[0]);
                fb.setColor(Color::FILE, 0);
            }
        }
    }
    
    const char* getCwd() const { return cwd_; }
    void setCwd(const char* path) {
        int i = 0;
        while (path[i] && i < 255) {
            cwd_[i] = path[i];
            i++;
        }
        cwd_[i] = '\0';
    }
};

} // namespace shell
