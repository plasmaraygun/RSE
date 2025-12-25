#pragma once

/**
 * ATA/IDE Disk Driver
 * Supports PIO mode for basic disk I/O
 */

#include <cstdint>

namespace drivers {

// ATA ports (primary controller)
namespace ATAPort {
    constexpr uint16_t DATA = 0x1F0;
    constexpr uint16_t ERROR = 0x1F1;
    constexpr uint16_t FEATURES = 0x1F1;
    constexpr uint16_t SECTOR_COUNT = 0x1F2;
    constexpr uint16_t LBA_LOW = 0x1F3;
    constexpr uint16_t LBA_MID = 0x1F4;
    constexpr uint16_t LBA_HIGH = 0x1F5;
    constexpr uint16_t DRIVE_SELECT = 0x1F6;
    constexpr uint16_t STATUS = 0x1F7;
    constexpr uint16_t COMMAND = 0x1F7;
    constexpr uint16_t ALT_STATUS = 0x3F6;
    constexpr uint16_t CONTROL = 0x3F6;
}

// Secondary controller
namespace ATAPort2 {
    constexpr uint16_t DATA = 0x170;
    constexpr uint16_t STATUS = 0x177;
    constexpr uint16_t COMMAND = 0x177;
    constexpr uint16_t ALT_STATUS = 0x376;
}

// Status register bits
namespace ATAStatus {
    constexpr uint8_t ERR = 0x01;   // Error
    constexpr uint8_t IDX = 0x02;   // Index
    constexpr uint8_t CORR = 0x04;  // Corrected data
    constexpr uint8_t DRQ = 0x08;   // Data request ready
    constexpr uint8_t SRV = 0x10;   // Overlapped mode service request
    constexpr uint8_t DF = 0x20;    // Drive fault
    constexpr uint8_t RDY = 0x40;   // Ready
    constexpr uint8_t BSY = 0x80;   // Busy
}

// ATA commands
namespace ATACmd {
    constexpr uint8_t READ_SECTORS = 0x20;
    constexpr uint8_t READ_SECTORS_EXT = 0x24;
    constexpr uint8_t WRITE_SECTORS = 0x30;
    constexpr uint8_t WRITE_SECTORS_EXT = 0x34;
    constexpr uint8_t CACHE_FLUSH = 0xE7;
    constexpr uint8_t CACHE_FLUSH_EXT = 0xEA;
    constexpr uint8_t IDENTIFY = 0xEC;
}

// Drive identification data
struct ATAIdentify {
    uint16_t config;
    uint16_t cylinders;
    uint16_t reserved1;
    uint16_t heads;
    uint16_t reserved2[2];
    uint16_t sectors_per_track;
    uint16_t reserved3[3];
    char serial[20];
    uint16_t reserved4[3];
    char firmware[8];
    char model[40];
    uint16_t reserved5[2];
    uint16_t capabilities;
    uint16_t reserved6[9];
    uint32_t lba_sectors;
    uint16_t reserved7[38];
    uint64_t lba48_sectors;
    uint16_t reserved8[152];
} __attribute__((packed));

class ATADisk {
private:
    uint16_t base_port_;
    uint16_t ctrl_port_;
    uint8_t drive_;  // 0 = master, 1 = slave
    bool present_;
    bool lba48_;
    uint64_t sectors_;
    char model_[41];
    
    static inline void outb(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }
    
    static inline uint8_t inb(uint16_t port) {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }
    
    static inline void outw(uint16_t port, uint16_t value) {
        asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
    }
    
    static inline uint16_t inw(uint16_t port) {
        uint16_t value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }
    
    static inline void insw(uint16_t port, void* addr, uint32_t count) {
        asm volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
    }
    
    static inline void outsw(uint16_t port, const void* addr, uint32_t count) {
        asm volatile("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
    }
    
    void io_wait() {
        // Read alternate status 4 times for 400ns delay
        inb(ctrl_port_);
        inb(ctrl_port_);
        inb(ctrl_port_);
        inb(ctrl_port_);
    }
    
    bool waitBusy(uint32_t timeout_ms = 1000)  // Default 1 second timeout {
        for (uint32_t i = 0; i < timeout_ms * 100; i++) {
            uint8_t status = inb(base_port_ + 7);
            if (!(status & ATAStatus::BSY)) return true;
            io_wait();
        }
        return false;
    }
    
    bool waitDRQ(uint32_t timeout_ms = 1000)  // Default 1 second timeout {
        for (uint32_t i = 0; i < timeout_ms * 100; i++) {
            uint8_t status = inb(base_port_ + 7);
            if (status & ATAStatus::ERR) return false;
            if (status & ATAStatus::DF) return false;
            if (status & ATAStatus::DRQ) return true;
            io_wait();
        }
        return false;
    }
    
    void selectDrive() {
        outb(base_port_ + 6, 0xE0 | (drive_ << 4));
        io_wait();
    }

public:
    ATADisk(uint16_t base = ATAPort::DATA, uint16_t ctrl = ATAPort::ALT_STATUS, uint8_t drive = 0)
        : base_port_(base), ctrl_port_(ctrl), drive_(drive),
          present_(false), lba48_(false), sectors_(0) {
        model_[0] = '\0';
    }
    
    bool identify() {
        selectDrive();
        
        // Zero sector count and LBA registers
        outb(base_port_ + 2, 0);
        outb(base_port_ + 3, 0);
        outb(base_port_ + 4, 0);
        outb(base_port_ + 5, 0);
        
        // Send IDENTIFY command
        outb(base_port_ + 7, ATACmd::IDENTIFY);
        io_wait();
        
        // Check if drive exists
        uint8_t status = inb(base_port_ + 7);
        if (status == 0) {
            return false;  // No drive
        }
        
        // Wait for BSY to clear
        if (!waitBusy()) {
            return false;
        }
        
        // Check for ATAPI
        if (inb(base_port_ + 4) != 0 || inb(base_port_ + 5) != 0) {
            return false;  // ATAPI device, not ATA
        }
        
        // Wait for data
        if (!waitDRQ()) {
            return false;
        }
        
        // Read identification data
        ATAIdentify id;
        insw(base_port_, &id, 256);
        
        // Parse model string (byte-swapped)
        for (int i = 0; i < 40; i += 2) {
            model_[i] = id.model[i + 1];
            model_[i + 1] = id.model[i];
        }
        model_[40] = '\0';
        
        // Trim trailing spaces
        for (int i = 39; i >= 0 && model_[i] == ' '; i--) {
            model_[i] = '\0';
        }
        
        // Check LBA48 support
        lba48_ = (id.lba48_sectors != 0);
        sectors_ = lba48_ ? id.lba48_sectors : id.lba_sectors;
        
        present_ = true;
        return true;
    }
    
    bool read(uint64_t lba, uint8_t count, void* buffer) {
        if (!present_ || count == 0) return false;
        
        selectDrive();
        
        if (lba48_ || lba > 0x0FFFFFFF) {
            // LBA48 mode
            outb(base_port_ + 2, 0);  // High byte of sector count
            outb(base_port_ + 3, (lba >> 24) & 0xFF);
            outb(base_port_ + 4, (lba >> 32) & 0xFF);
            outb(base_port_ + 5, (lba >> 40) & 0xFF);
            outb(base_port_ + 2, count);
            outb(base_port_ + 3, lba & 0xFF);
            outb(base_port_ + 4, (lba >> 8) & 0xFF);
            outb(base_port_ + 5, (lba >> 16) & 0xFF);
            outb(base_port_ + 7, ATACmd::READ_SECTORS_EXT);
        } else {
            // LBA28 mode
            outb(base_port_ + 6, 0xE0 | (drive_ << 4) | ((lba >> 24) & 0x0F));
            outb(base_port_ + 2, count);
            outb(base_port_ + 3, lba & 0xFF);
            outb(base_port_ + 4, (lba >> 8) & 0xFF);
            outb(base_port_ + 5, (lba >> 16) & 0xFF);
            outb(base_port_ + 7, ATACmd::READ_SECTORS);
        }
        
        uint16_t* buf = reinterpret_cast<uint16_t*>(buffer);
        for (uint8_t i = 0; i < count; i++) {
            if (!waitDRQ()) return false;
            insw(base_port_, buf, 256);
            buf += 256;
        }
        
        return true;
    }
    
    bool write(uint64_t lba, uint8_t count, const void* buffer) {
        if (!present_ || count == 0) return false;
        
        selectDrive();
        
        if (lba48_ || lba > 0x0FFFFFFF) {
            // LBA48 mode
            outb(base_port_ + 2, 0);
            outb(base_port_ + 3, (lba >> 24) & 0xFF);
            outb(base_port_ + 4, (lba >> 32) & 0xFF);
            outb(base_port_ + 5, (lba >> 40) & 0xFF);
            outb(base_port_ + 2, count);
            outb(base_port_ + 3, lba & 0xFF);
            outb(base_port_ + 4, (lba >> 8) & 0xFF);
            outb(base_port_ + 5, (lba >> 16) & 0xFF);
            outb(base_port_ + 7, ATACmd::WRITE_SECTORS_EXT);
        } else {
            // LBA28 mode
            outb(base_port_ + 6, 0xE0 | (drive_ << 4) | ((lba >> 24) & 0x0F));
            outb(base_port_ + 2, count);
            outb(base_port_ + 3, lba & 0xFF);
            outb(base_port_ + 4, (lba >> 8) & 0xFF);
            outb(base_port_ + 5, (lba >> 16) & 0xFF);
            outb(base_port_ + 7, ATACmd::WRITE_SECTORS);
        }
        
        const uint16_t* buf = reinterpret_cast<const uint16_t*>(buffer);
        for (uint8_t i = 0; i < count; i++) {
            if (!waitDRQ()) return false;
            outsw(base_port_, buf, 256);
            buf += 256;
        }
        
        // Flush cache
        outb(base_port_ + 7, lba48_ ? ATACmd::CACHE_FLUSH_EXT : ATACmd::CACHE_FLUSH);
        waitBusy();
        
        return true;
    }
    
    bool isPresent() const { return present_; }
    bool isLBA48() const { return lba48_; }
    uint64_t getSectors() const { return sectors_; }
    uint64_t getSize() const { return sectors_ * 512; }
    const char* getModel() const { return model_; }
};

// Disk manager
class DiskManager {
private:
    static constexpr size_t MAX_DISKS = 4;
    ATADisk disks_[MAX_DISKS];
    size_t disk_count_;

public:
    DiskManager() : disk_count_(0) {}
    
    void scan() {
        // Primary master
        disks_[0] = ATADisk(ATAPort::DATA, ATAPort::ALT_STATUS, 0);
        if (disks_[0].identify()) disk_count_++;
        
        // Primary slave
        disks_[1] = ATADisk(ATAPort::DATA, ATAPort::ALT_STATUS, 1);
        if (disks_[1].identify()) disk_count_++;
        
        // Secondary master
        disks_[2] = ATADisk(ATAPort2::DATA, ATAPort2::ALT_STATUS, 0);
        if (disks_[2].identify()) disk_count_++;
        
        // Secondary slave
        disks_[3] = ATADisk(ATAPort2::DATA, ATAPort2::ALT_STATUS, 1);
        if (disks_[3].identify()) disk_count_++;
    }
    
    size_t count() const { return disk_count_; }
    
    ATADisk* getDisk(size_t index) {
        if (index >= MAX_DISKS) return nullptr;
        return disks_[index].isPresent() ? &disks_[index] : nullptr;
    }
    
    void printInfo() {
        for (size_t i = 0; i < MAX_DISKS; i++) {
            if (disks_[i].isPresent()) {
                // Would use klog here
            }
        }
    }
};

inline DiskManager& getDiskManager() {
    static DiskManager mgr;
    return mgr;
}

} // namespace drivers
