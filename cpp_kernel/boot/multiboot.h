#pragma once

/**
 * Multiboot2 Header for GRUB/bootloader compatibility
 * Follows Multiboot2 specification
 */

#include <cstdint>

// Freestanding size_t definition
using size_t = decltype(sizeof(0));

namespace boot {

// Multiboot2 magic numbers
constexpr uint32_t MULTIBOOT2_MAGIC = 0x36d76289;
constexpr uint32_t MULTIBOOT2_HEADER_MAGIC = 0xe85250d6;
constexpr uint32_t MULTIBOOT2_ARCHITECTURE_I386 = 0;

// Multiboot2 header tag types
enum class TagType : uint16_t {
    END = 0,
    INFORMATION_REQUEST = 1,
    ADDRESS = 2,
    ENTRY_ADDRESS = 3,
    CONSOLE_FLAGS = 4,
    FRAMEBUFFER = 5,
    MODULE_ALIGN = 6,
    EFI_BOOT_SERVICES = 7,
    EFI_I386_ENTRY = 8,
    EFI_AMD64_ENTRY = 9,
    RELOCATABLE = 10
};

// Multiboot2 info tag types
enum class InfoTagType : uint32_t {
    END = 0,
    BOOT_CMD_LINE = 1,
    BOOT_LOADER_NAME = 2,
    MODULE = 3,
    BASIC_MEMINFO = 4,
    BOOTDEV = 5,
    MMAP = 6,
    VBE = 7,
    FRAMEBUFFER = 8,
    ELF_SECTIONS = 9,
    APM = 10,
    EFI32 = 11,
    EFI64 = 12,
    SMBIOS = 13,
    ACPI_OLD = 14,
    ACPI_NEW = 15,
    NETWORK = 16,
    EFI_MMAP = 17,
    EFI_BOOT_SERVICES_NOT_TERMINATED = 18,
    EFI32_IMAGE_HANDLE = 19,
    EFI64_IMAGE_HANDLE = 20,
    IMAGE_LOAD_BASE_ADDR = 21
};

// Memory map entry types
enum class MemoryType : uint32_t {
    AVAILABLE = 1,
    RESERVED = 2,
    ACPI_RECLAIMABLE = 3,
    ACPI_NVS = 4,
    BAD_MEMORY = 5
};

#pragma pack(push, 1)

// Multiboot2 header
struct MultibootHeader {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
};

// Generic tag header
struct TagHeader {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
};

// End tag
struct EndTag {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
};

// Framebuffer tag
struct FramebufferTag {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
};

// Info tag header (from bootloader)
struct InfoTagHeader {
    uint32_t type;
    uint32_t size;
};

// Boot command line
struct CmdLineTag {
    uint32_t type;
    uint32_t size;
    char string[];  // Null-terminated
};

// Basic memory info
struct BasicMemInfoTag {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;  // KB below 1MB
    uint32_t mem_upper;  // KB above 1MB
};

// Memory map entry
struct MMapEntry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

// Memory map tag
struct MMapTag {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    MMapEntry entries[];
};

// Framebuffer info
struct FramebufferInfoTag {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
};

// Module info
struct ModuleTag {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
};

#pragma pack(pop)

// Parsed boot info
struct BootInfo {
    uint64_t total_memory;           // Total available memory in bytes
    uint64_t kernel_start;           // Kernel physical start
    uint64_t kernel_end;             // Kernel physical end
    uint64_t framebuffer_addr;       // Framebuffer address
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint8_t framebuffer_bpp;
    const char* cmdline;             // Boot command line
    const char* bootloader_name;
    
    // Memory map
    static constexpr size_t MAX_MMAP_ENTRIES = 64;
    MMapEntry mmap[MAX_MMAP_ENTRIES];
    size_t mmap_count;
    
    // Modules
    static constexpr size_t MAX_MODULES = 16;
    struct Module {
        uint64_t start;
        uint64_t end;
        const char* cmdline;
    } modules[MAX_MODULES];
    size_t module_count;
};

// Parse multiboot2 info structure
inline BootInfo parse_multiboot_info(void* mbi_addr) {
    BootInfo info = {};
    
    uint32_t total_size = *reinterpret_cast<uint32_t*>(mbi_addr);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(mbi_addr) + 8;
    uint8_t* end = reinterpret_cast<uint8_t*>(mbi_addr) + total_size;
    
    while (ptr < end) {
        auto* tag = reinterpret_cast<InfoTagHeader*>(ptr);
        
        if (tag->type == 0) break;  // End tag
        
        switch (static_cast<InfoTagType>(tag->type)) {
            case InfoTagType::BOOT_CMD_LINE: {
                auto* cmd = reinterpret_cast<CmdLineTag*>(tag);
                info.cmdline = cmd->string;
                break;
            }
            
            case InfoTagType::BASIC_MEMINFO: {
                auto* mem = reinterpret_cast<BasicMemInfoTag*>(tag);
                info.total_memory = (static_cast<uint64_t>(mem->mem_upper) + 1024) * 1024;
                break;
            }
            
            case InfoTagType::MMAP: {
                auto* mmap = reinterpret_cast<MMapTag*>(tag);
                size_t entry_count = (mmap->size - 16) / mmap->entry_size;
                
                for (size_t i = 0; i < entry_count && info.mmap_count < BootInfo::MAX_MMAP_ENTRIES; i++) {
                    auto* entry = reinterpret_cast<MMapEntry*>(
                        reinterpret_cast<uint8_t*>(mmap->entries) + i * mmap->entry_size
                    );
                    info.mmap[info.mmap_count++] = *entry;
                    
                    if (entry->type == static_cast<uint32_t>(MemoryType::AVAILABLE)) {
                        info.total_memory += entry->length;
                    }
                }
                break;
            }
            
            case InfoTagType::FRAMEBUFFER: {
                auto* fb = reinterpret_cast<FramebufferInfoTag*>(tag);
                info.framebuffer_addr = fb->framebuffer_addr;
                info.framebuffer_width = fb->framebuffer_width;
                info.framebuffer_height = fb->framebuffer_height;
                info.framebuffer_pitch = fb->framebuffer_pitch;
                info.framebuffer_bpp = fb->framebuffer_bpp;
                break;
            }
            
            case InfoTagType::MODULE: {
                auto* mod = reinterpret_cast<ModuleTag*>(tag);
                if (info.module_count < BootInfo::MAX_MODULES) {
                    info.modules[info.module_count].start = mod->mod_start;
                    info.modules[info.module_count].end = mod->mod_end;
                    info.modules[info.module_count].cmdline = mod->cmdline;
                    info.module_count++;
                }
                break;
            }
            
            default:
                break;
        }
        
        // Move to next tag (8-byte aligned)
        ptr += (tag->size + 7) & ~7;
    }
    
    return info;
}

} // namespace boot
