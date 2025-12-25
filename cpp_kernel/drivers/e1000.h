#pragma once

/**
 * Intel E1000 Network Driver
 * Supports E1000/E1000E network cards (common in QEMU/VirtualBox)
 */

#include <cstdint>

namespace drivers {

// E1000 register offsets
namespace E1000Reg {
    constexpr uint32_t CTRL = 0x0000;      // Device Control
    constexpr uint32_t STATUS = 0x0008;    // Device Status
    constexpr uint32_t EECD = 0x0010;      // EEPROM Control
    constexpr uint32_t EERD = 0x0014;      // EEPROM Read
    constexpr uint32_t ICR = 0x00C0;       // Interrupt Cause Read
    constexpr uint32_t IMS = 0x00D0;       // Interrupt Mask Set
    constexpr uint32_t IMC = 0x00D8;       // Interrupt Mask Clear
    constexpr uint32_t RCTL = 0x0100;      // Receive Control
    constexpr uint32_t TCTL = 0x0400;      // Transmit Control
    constexpr uint32_t RDBAL = 0x2800;     // RX Descriptor Base Low
    constexpr uint32_t RDBAH = 0x2804;     // RX Descriptor Base High
    constexpr uint32_t RDLEN = 0x2808;     // RX Descriptor Length
    constexpr uint32_t RDH = 0x2810;       // RX Descriptor Head
    constexpr uint32_t RDT = 0x2818;       // RX Descriptor Tail
    constexpr uint32_t TDBAL = 0x3800;     // TX Descriptor Base Low
    constexpr uint32_t TDBAH = 0x3804;     // TX Descriptor Base High
    constexpr uint32_t TDLEN = 0x3808;     // TX Descriptor Length
    constexpr uint32_t TDH = 0x3810;       // TX Descriptor Head
    constexpr uint32_t TDT = 0x3818;       // TX Descriptor Tail
    constexpr uint32_t RAL0 = 0x5400;      // Receive Address Low
    constexpr uint32_t RAH0 = 0x5404;      // Receive Address High
    constexpr uint32_t MTA = 0x5200;       // Multicast Table Array
}

// Control register bits
namespace E1000Ctrl {
    constexpr uint32_t FD = 1 << 0;        // Full Duplex
    constexpr uint32_t LRST = 1 << 3;      // Link Reset
    constexpr uint32_t ASDE = 1 << 5;      // Auto-Speed Detection Enable
    constexpr uint32_t SLU = 1 << 6;       // Set Link Up
    constexpr uint32_t ILOS = 1 << 7;      // Invert Loss-of-Signal
    constexpr uint32_t RST = 1 << 26;      // Device Reset
    constexpr uint32_t VME = 1 << 30;      // VLAN Mode Enable
    constexpr uint32_t PHY_RST = 1 << 31;  // PHY Reset
}

// Receive control bits
namespace E1000RCtl {
    constexpr uint32_t EN = 1 << 1;        // Receiver Enable
    constexpr uint32_t SBP = 1 << 2;       // Store Bad Packets
    constexpr uint32_t UPE = 1 << 3;       // Unicast Promiscuous Enable
    constexpr uint32_t MPE = 1 << 4;       // Multicast Promiscuous Enable
    constexpr uint32_t LPE = 1 << 5;       // Long Packet Enable
    constexpr uint32_t BAM = 1 << 15;      // Broadcast Accept Mode
    constexpr uint32_t BSIZE_256 = 3 << 16;
    constexpr uint32_t BSIZE_512 = 2 << 16;
    constexpr uint32_t BSIZE_1024 = 1 << 16;
    constexpr uint32_t BSIZE_2048 = 0 << 16;
    constexpr uint32_t BSIZE_4096 = (3 << 16) | (1 << 25);
    constexpr uint32_t BSIZE_8192 = (2 << 16) | (1 << 25);
    constexpr uint32_t BSIZE_16384 = (1 << 16) | (1 << 25);
    constexpr uint32_t SECRC = 1 << 26;    // Strip Ethernet CRC
}

// Transmit control bits
namespace E1000TCtl {
    constexpr uint32_t EN = 1 << 1;        // Transmit Enable
    constexpr uint32_t PSP = 1 << 3;       // Pad Short Packets
    constexpr uint32_t CT_SHIFT = 4;       // Collision Threshold
    constexpr uint32_t COLD_SHIFT = 12;    // Collision Distance
    constexpr uint32_t SWXOFF = 1 << 22;   // Software XOFF
    constexpr uint32_t RTLC = 1 << 24;     // Re-transmit on Late Collision
}

// Interrupt bits
namespace E1000Int {
    constexpr uint32_t TXDW = 1 << 0;      // TX Descriptor Written Back
    constexpr uint32_t TXQE = 1 << 1;      // TX Queue Empty
    constexpr uint32_t LSC = 1 << 2;       // Link Status Change
    constexpr uint32_t RXSEQ = 1 << 3;     // RX Sequence Error
    constexpr uint32_t RXDMT0 = 1 << 4;    // RX Descriptor Minimum Threshold
    constexpr uint32_t RXO = 1 << 6;       // Receiver Overrun
    constexpr uint32_t RXT0 = 1 << 7;      // Receiver Timer Interrupt
}

// Receive descriptor
struct E1000RxDesc {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

// RX status bits
namespace RxStatus {
    constexpr uint8_t DD = 1 << 0;         // Descriptor Done
    constexpr uint8_t EOP = 1 << 1;        // End of Packet
}

// Transmit descriptor
struct E1000TxDesc {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;           // Checksum Offset
    uint8_t cmd;           // Command
    uint8_t status;
    uint8_t css;           // Checksum Start
    uint16_t special;
} __attribute__((packed));

// TX command bits
namespace TxCmd {
    constexpr uint8_t EOP = 1 << 0;        // End of Packet
    constexpr uint8_t IFCS = 1 << 1;       // Insert FCS
    constexpr uint8_t IC = 1 << 2;         // Insert Checksum
    constexpr uint8_t RS = 1 << 3;         // Report Status
    constexpr uint8_t DEXT = 1 << 5;       // Descriptor Extension
    constexpr uint8_t VLE = 1 << 6;        // VLAN Packet Enable
    constexpr uint8_t IDE = 1 << 7;        // Interrupt Delay Enable
}

// TX status bits
namespace TxStatus {
    constexpr uint8_t DD = 1 << 0;         // Descriptor Done
}

class E1000 {
private:
    static constexpr size_t RX_DESC_COUNT = 32;
    static constexpr size_t TX_DESC_COUNT = 8;
    static constexpr size_t RX_BUFFER_SIZE = 2048;
    static constexpr size_t TX_BUFFER_SIZE = 2048;
    
    volatile uint8_t* mmio_base_;
    uint32_t io_base_;
    bool use_mmio_;
    bool initialized_;
    
    uint8_t mac_[6];
    
    // Descriptors (must be 16-byte aligned)
    E1000RxDesc* rx_descs_;
    E1000TxDesc* tx_descs_;
    
    // Buffers
    uint8_t* rx_buffers_;
    uint8_t* tx_buffers_;
    
    uint16_t rx_cur_;
    uint16_t tx_cur_;
    
    void writeReg(uint32_t reg, uint32_t value) {
        if (use_mmio_) {
            *reinterpret_cast<volatile uint32_t*>(mmio_base_ + reg) = value;
        } else {
            outl(io_base_, reg);
            outl(io_base_ + 4, value);
        }
    }
    
    uint32_t readReg(uint32_t reg) {
        if (use_mmio_) {
            return *reinterpret_cast<volatile uint32_t*>(mmio_base_ + reg);
        } else {
            outl(io_base_, reg);
            return inl(io_base_ + 4);
        }
    }
    
    static inline void outl(uint16_t port, uint32_t value) {
        asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
    }
    
    static inline uint32_t inl(uint16_t port) {
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }
    
    uint16_t readEEPROM(uint8_t addr) {
        writeReg(E1000Reg::EERD, 1 | (static_cast<uint32_t>(addr) << 8));
        
        uint32_t val;
        while (!((val = readReg(E1000Reg::EERD)) & (1 << 4))) {
            asm volatile("pause");
        }
        
        return static_cast<uint16_t>(val >> 16);
    }
    
    void readMAC() {
        // Try EEPROM first
        uint32_t eecd = readReg(E1000Reg::EECD);
        if (eecd & (1 << 8)) {  // EEPROM present
            uint16_t word = readEEPROM(0);
            mac_[0] = word & 0xFF;
            mac_[1] = word >> 8;
            word = readEEPROM(1);
            mac_[2] = word & 0xFF;
            mac_[3] = word >> 8;
            word = readEEPROM(2);
            mac_[4] = word & 0xFF;
            mac_[5] = word >> 8;
        } else {
            // Read from RAL/RAH
            uint32_t ral = readReg(E1000Reg::RAL0);
            uint32_t rah = readReg(E1000Reg::RAH0);
            mac_[0] = ral & 0xFF;
            mac_[1] = (ral >> 8) & 0xFF;
            mac_[2] = (ral >> 16) & 0xFF;
            mac_[3] = (ral >> 24) & 0xFF;
            mac_[4] = rah & 0xFF;
            mac_[5] = (rah >> 8) & 0xFF;
        }
    }
    
    void initRx() {
        // Allocate RX descriptors and buffers
        // In real kernel, use physical memory allocator
        // For now, assume buffers are pre-allocated
        
        // Set up RX descriptor ring
        uint64_t rx_phys = reinterpret_cast<uint64_t>(rx_descs_);
        writeReg(E1000Reg::RDBAL, rx_phys & 0xFFFFFFFF);
        writeReg(E1000Reg::RDBAH, rx_phys >> 32);
        writeReg(E1000Reg::RDLEN, RX_DESC_COUNT * sizeof(E1000RxDesc));
        writeReg(E1000Reg::RDH, 0);
        writeReg(E1000Reg::RDT, RX_DESC_COUNT - 1);
        
        // Initialize descriptors
        for (size_t i = 0; i < RX_DESC_COUNT; i++) {
            rx_descs_[i].buffer_addr = reinterpret_cast<uint64_t>(rx_buffers_ + i * RX_BUFFER_SIZE);
            rx_descs_[i].status = 0;
        }
        
        // Enable receiver
        writeReg(E1000Reg::RCTL, 
                 E1000RCtl::EN | E1000RCtl::BAM | E1000RCtl::BSIZE_2048 | E1000RCtl::SECRC);
    }
    
    void initTx() {
        // Set up TX descriptor ring
        uint64_t tx_phys = reinterpret_cast<uint64_t>(tx_descs_);
        writeReg(E1000Reg::TDBAL, tx_phys & 0xFFFFFFFF);
        writeReg(E1000Reg::TDBAH, tx_phys >> 32);
        writeReg(E1000Reg::TDLEN, TX_DESC_COUNT * sizeof(E1000TxDesc));
        writeReg(E1000Reg::TDH, 0);
        writeReg(E1000Reg::TDT, 0);
        
        // Initialize descriptors
        for (size_t i = 0; i < TX_DESC_COUNT; i++) {
            tx_descs_[i].buffer_addr = reinterpret_cast<uint64_t>(tx_buffers_ + i * TX_BUFFER_SIZE);
            tx_descs_[i].status = TxStatus::DD;  // Mark as done
            tx_descs_[i].cmd = 0;
        }
        
        // Enable transmitter
        writeReg(E1000Reg::TCTL,
                 E1000TCtl::EN | E1000TCtl::PSP | 
                 (15 << E1000TCtl::CT_SHIFT) |
                 (64 << E1000TCtl::COLD_SHIFT) |
                 E1000TCtl::RTLC);
    }

public:
    E1000() : mmio_base_(nullptr), io_base_(0), use_mmio_(false), 
              initialized_(false), rx_descs_(nullptr), tx_descs_(nullptr),
              rx_buffers_(nullptr), tx_buffers_(nullptr), rx_cur_(0), tx_cur_(0) {
        __builtin_memset(mac_, 0, 6);
    }
    
    bool init(uint64_t mmio_addr, uint32_t io_addr = 0) {
        if (mmio_addr) {
            mmio_base_ = reinterpret_cast<volatile uint8_t*>(mmio_addr);
            use_mmio_ = true;
        } else if (io_addr) {
            io_base_ = io_addr;
            use_mmio_ = false;
        } else {
            return false;
        }
        
        // Reset device
        writeReg(E1000Reg::CTRL, E1000Ctrl::RST);
        for (int i = 0; i < 1000; i++)  // EEPROM read timeout iterations {
            asm volatile("pause");
        }
        
        // Wait for reset to complete
        while (readReg(E1000Reg::CTRL) & E1000Ctrl::RST) {
            asm volatile("pause");
        }
        
        // Disable interrupts
        writeReg(E1000Reg::IMC, 0xFFFFFFFF);
        
        // Read MAC address
        readMAC();
        
        // Set link up
        writeReg(E1000Reg::CTRL, E1000Ctrl::SLU | E1000Ctrl::ASDE);
        
        // Clear multicast table
        for (int i = 0; i < 128; i++) {
            writeReg(E1000Reg::MTA + i * 4, 0);
        }
        
        // Allocate buffers (simplified - real kernel would use allocator)
        // These would need to be properly allocated from physical memory
        static E1000RxDesc rx_desc_array[RX_DESC_COUNT] __attribute__((aligned(16)));
        static E1000TxDesc tx_desc_array[TX_DESC_COUNT] __attribute__((aligned(16)));
        static uint8_t rx_buf_array[RX_DESC_COUNT * RX_BUFFER_SIZE] __attribute__((aligned(16)));
        static uint8_t tx_buf_array[TX_DESC_COUNT * TX_BUFFER_SIZE] __attribute__((aligned(16)));
        
        rx_descs_ = rx_desc_array;
        tx_descs_ = tx_desc_array;
        rx_buffers_ = rx_buf_array;
        tx_buffers_ = tx_buf_array;
        
        // Initialize RX and TX
        initRx();
        initTx();
        
        // Enable interrupts
        writeReg(E1000Reg::IMS, E1000Int::RXT0 | E1000Int::LSC);
        
        initialized_ = true;
        return true;
    }
    
    bool send(const void* data, size_t len) {
        if (!initialized_ || len > TX_BUFFER_SIZE) return false;
        
        // Wait for descriptor to be available
        while (!(tx_descs_[tx_cur_].status & TxStatus::DD)) {
            asm volatile("pause");
        }
        
        // Copy data to buffer
        __builtin_memcpy(tx_buffers_ + tx_cur_ * TX_BUFFER_SIZE, data, len);
        
        // Set up descriptor
        tx_descs_[tx_cur_].length = len;
        tx_descs_[tx_cur_].cmd = TxCmd::EOP | TxCmd::IFCS | TxCmd::RS;
        tx_descs_[tx_cur_].status = 0;
        
        // Update tail
        uint16_t old_cur = tx_cur_;
        tx_cur_ = (tx_cur_ + 1) % TX_DESC_COUNT;
        writeReg(E1000Reg::TDT, tx_cur_);
        
        // Wait for send to complete
        while (!(tx_descs_[old_cur].status & TxStatus::DD)) {
            asm volatile("pause");
        }
        
        return true;
    }
    
    int receive(void* buffer, size_t max_len) {
        if (!initialized_) return -1;
        
        // Check if packet available
        if (!(rx_descs_[rx_cur_].status & RxStatus::DD)) {
            return 0;  // No packet
        }
        
        uint16_t len = rx_descs_[rx_cur_].length;
        if (len > max_len) len = max_len;
        
        // Copy data
        __builtin_memcpy(buffer, rx_buffers_ + rx_cur_ * RX_BUFFER_SIZE, len);
        
        // Reset descriptor
        rx_descs_[rx_cur_].status = 0;
        
        // Update tail
        uint16_t old_cur = rx_cur_;
        rx_cur_ = (rx_cur_ + 1) % RX_DESC_COUNT;
        writeReg(E1000Reg::RDT, old_cur);
        
        return len;
    }
    
    void handleInterrupt() {
        uint32_t icr = readReg(E1000Reg::ICR);
        
        if (icr & E1000Int::LSC) {
            // Link status changed
            uint32_t status = readReg(E1000Reg::STATUS);
            // Handle link up/down
        }
        
        if (icr & E1000Int::RXT0) {
            // Packet received - handled in receive()
        }
    }
    
    bool isInitialized() const { return initialized_; }
    
    const uint8_t* getMAC() const { return mac_; }
    
    bool isLinkUp() const {
        return (readReg(E1000Reg::STATUS) & 2) != 0;
    }
};

inline E1000& getNIC() {
    static E1000 nic;
    return nic;
}

} // namespace drivers
