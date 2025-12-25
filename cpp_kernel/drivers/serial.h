#pragma once

/**
 * Serial Port Driver (COM1-COM4)
 * Provides early console output before framebuffer is available
 */

#include <cstdint>

// Freestanding size_t
using size_t = decltype(sizeof(0));

namespace drivers {

// COM port addresses
namespace COM {
    constexpr uint16_t COM1 = 0x3F8;
    constexpr uint16_t COM2 = 0x2F8;
    constexpr uint16_t COM3 = 0x3E8;
    constexpr uint16_t COM4 = 0x2E8;
}

// Port offsets
namespace SerialReg {
    constexpr uint16_t DATA = 0;           // Data register (R/W)
    constexpr uint16_t INT_ENABLE = 1;     // Interrupt enable
    constexpr uint16_t FIFO_CTRL = 2;      // FIFO control
    constexpr uint16_t LINE_CTRL = 3;      // Line control
    constexpr uint16_t MODEM_CTRL = 4;     // Modem control
    constexpr uint16_t LINE_STATUS = 5;    // Line status
    constexpr uint16_t MODEM_STATUS = 6;   // Modem status
    constexpr uint16_t SCRATCH = 7;        // Scratch register
    
    // When DLAB=1
    constexpr uint16_t DIVISOR_LOW = 0;    // Divisor latch low
    constexpr uint16_t DIVISOR_HIGH = 1;   // Divisor latch high
}

// Line status bits
namespace LineStatus {
    constexpr uint8_t DATA_READY = 0x01;
    constexpr uint8_t OVERRUN_ERROR = 0x02;
    constexpr uint8_t PARITY_ERROR = 0x04;
    constexpr uint8_t FRAMING_ERROR = 0x08;
    constexpr uint8_t BREAK_INDICATOR = 0x10;
    constexpr uint8_t TX_HOLDING_EMPTY = 0x20;
    constexpr uint8_t TX_EMPTY = 0x40;
    constexpr uint8_t IMPENDING_ERROR = 0x80;
}

class Serial {
private:
    uint16_t port_;
    bool initialized_;
    
    static inline void outb(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }
    
    static inline uint8_t inb(uint16_t port) {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }
    
    bool transmitEmpty() const {
        return (inb(port_ + SerialReg::LINE_STATUS) & LineStatus::TX_HOLDING_EMPTY) != 0;
    }
    
    bool dataReady() const {
        return (inb(port_ + SerialReg::LINE_STATUS) & LineStatus::DATA_READY) != 0;
    }

public:
    Serial(uint16_t port = COM::COM1) : port_(port), initialized_(false) {}
    
    bool init(uint32_t baud = 115200) {
        // Calculate divisor for baud rate
        uint16_t divisor = 115200 / baud;
        
        // Disable interrupts
        outb(port_ + SerialReg::INT_ENABLE, 0x00);
        
        // Enable DLAB (set baud rate divisor)
        outb(port_ + SerialReg::LINE_CTRL, 0x80);
        
        // Set divisor
        outb(port_ + SerialReg::DIVISOR_LOW, divisor & 0xFF);
        outb(port_ + SerialReg::DIVISOR_HIGH, (divisor >> 8) & 0xFF);
        
        // 8 bits, no parity, one stop bit
        outb(port_ + SerialReg::LINE_CTRL, 0x03);
        
        // Enable FIFO, clear them, with 14-byte threshold
        outb(port_ + SerialReg::FIFO_CTRL, 0xC7);
        
        // IRQs enabled, RTS/DSR set
        outb(port_ + SerialReg::MODEM_CTRL, 0x0B);
        
        // Test serial chip (loopback mode)
        outb(port_ + SerialReg::MODEM_CTRL, 0x1E);
        outb(port_ + SerialReg::DATA, 0xAE);
        
        if (inb(port_ + SerialReg::DATA) != 0xAE) {
            return false;
        }
        
        // Normal operation mode
        outb(port_ + SerialReg::MODEM_CTRL, 0x0F);
        
        initialized_ = true;
        return true;
    }
    
    void putchar(char c) {
        if (!initialized_) return;
        
        while (!transmitEmpty()) {
            asm volatile("pause");
        }
        
        outb(port_ + SerialReg::DATA, c);
    }
    
    char getchar() {
        if (!initialized_) return 0;
        
        while (!dataReady()) {
            asm volatile("pause");
        }
        
        return inb(port_ + SerialReg::DATA);
    }
    
    void write(const char* str) {
        while (*str) {
            if (*str == '\n') {
                putchar('\r');
            }
            putchar(*str++);
        }
    }
    
    void write(const char* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            if (data[i] == '\n') {
                putchar('\r');
            }
            putchar(data[i]);
        }
    }
    
    // Printf-style formatting
    void printf(const char* fmt, ...) {
        __builtin_va_list args;
        __builtin_va_start(args, fmt);
        
        while (*fmt) {
            if (*fmt == '%' && *(fmt + 1)) {
                fmt++;
                switch (*fmt) {
                    case 's': {
                        const char* s = __builtin_va_arg(args, const char*);
                        write(s ? s : "(null)");
                        break;
                    }
                    case 'd':
                    case 'i': {
                        int val = __builtin_va_arg(args, int);
                        printInt(val);
                        break;
                    }
                    case 'u': {
                        unsigned val = __builtin_va_arg(args, unsigned);
                        printUint(val);
                        break;
                    }
                    case 'x': {
                        unsigned val = __builtin_va_arg(args, unsigned);
                        printHex(val);
                        break;
                    }
                    case 'p': {
                        uint64_t val = __builtin_va_arg(args, uint64_t);
                        write("0x");
                        printHex64(val);
                        break;
                    }
                    case 'c': {
                        char c = static_cast<char>(__builtin_va_arg(args, int));
                        putchar(c);
                        break;
                    }
                    case '%':
                        putchar('%');
                        break;
                    default:
                        putchar('%');
                        putchar(*fmt);
                        break;
                }
            } else {
                if (*fmt == '\n') putchar('\r');
                putchar(*fmt);
            }
            fmt++;
        }
        
        __builtin_va_end(args);
    }
    
private:
    void printInt(int val) {
        if (val < 0) {
            putchar('-');
            val = -val;
        }
        printUint(static_cast<unsigned>(val));
    }
    
    void printUint(unsigned val) {
        char buf[12];
        int i = 0;
        do {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        } while (val);
        
        while (i--) {
            putchar(buf[i]);
        }
    }
    
    void printHex(unsigned val) {
        static const char hex[] = "0123456789abcdef";
        char buf[8];
        int i = 0;
        do {
            buf[i++] = hex[val & 0xF];
            val >>= 4;
        } while (val);
        
        while (i--) {
            putchar(buf[i]);
        }
    }
    
    void printHex64(uint64_t val) {
        static const char hex[] = "0123456789abcdef";
        char buf[16];
        int i = 0;
        do {
            buf[i++] = hex[val & 0xF];
            val >>= 4;
        } while (val);
        
        while (i < 16) buf[i++] = '0';
        
        while (i--) {
            putchar(buf[i]);
        }
    }
};

// Global serial console
inline Serial& getSerial() {
    static Serial serial(COM::COM1);
    return serial;
}

} // namespace drivers

// Kernel logging macros (outside namespace - macros are not namespaced)
#define klog(fmt, ...) drivers::getSerial().printf("[KERNEL] " fmt "\n", ##__VA_ARGS__)
#define kpanic(fmt, ...) do { \
    drivers::getSerial().printf("[PANIC] " fmt "\n", ##__VA_ARGS__); \
    asm volatile("cli; hlt"); \
} while(0)
