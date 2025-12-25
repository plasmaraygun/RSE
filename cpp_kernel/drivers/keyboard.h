#pragma once

/**
 * PS/2 Keyboard Driver
 * Handles keyboard input via IRQ1
 */

#include <cstdint>

namespace drivers {

// Keyboard ports
namespace KbdPort {
    constexpr uint16_t DATA = 0x60;
    constexpr uint16_t STATUS = 0x64;
    constexpr uint16_t COMMAND = 0x64;
}

// Status register bits
namespace KbdStatus {
    constexpr uint8_t OUTPUT_FULL = 0x01;
    constexpr uint8_t INPUT_FULL = 0x02;
    constexpr uint8_t SYSTEM = 0x04;
    constexpr uint8_t COMMAND = 0x08;
    constexpr uint8_t TIMEOUT = 0x40;
    constexpr uint8_t PARITY = 0x80;
}

// Special keys
namespace Key {
    constexpr uint8_t ESCAPE = 0x01;
    constexpr uint8_t BACKSPACE = 0x0E;
    constexpr uint8_t TAB = 0x0F;
    constexpr uint8_t ENTER = 0x1C;
    constexpr uint8_t CTRL = 0x1D;
    constexpr uint8_t LSHIFT = 0x2A;
    constexpr uint8_t RSHIFT = 0x36;
    constexpr uint8_t ALT = 0x38;
    constexpr uint8_t CAPSLOCK = 0x3A;
    constexpr uint8_t F1 = 0x3B;
    constexpr uint8_t F12 = 0x58;
    constexpr uint8_t NUMLOCK = 0x45;
    constexpr uint8_t SCROLLLOCK = 0x46;
    
    constexpr uint8_t RELEASE = 0x80;
}

// US QWERTY scancode to ASCII mapping
constexpr char SCANCODE_TO_ASCII[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // F1-F10
    0, 0,  // Num lock, Scroll lock
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
    0, 0, 0,  // F11, F12
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

constexpr char SCANCODE_TO_ASCII_SHIFT[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

class Keyboard {
private:
    static constexpr size_t BUFFER_SIZE = 256;
    
    char buffer_[BUFFER_SIZE];
    volatile size_t read_pos_;
    volatile size_t write_pos_;
    
    bool shift_pressed_;
    bool ctrl_pressed_;
    bool alt_pressed_;
    bool capslock_;
    
    static inline void outb(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }
    
    static inline uint8_t inb(uint16_t port) {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }
    
    void waitInput() {
        while (inb(KbdPort::STATUS) & KbdStatus::INPUT_FULL) {
            asm volatile("pause");
        }
    }
    
    void waitOutput() {
        while (!(inb(KbdPort::STATUS) & KbdStatus::OUTPUT_FULL)) {
            asm volatile("pause");
        }
    }

public:
    Keyboard() : read_pos_(0), write_pos_(0), 
                 shift_pressed_(false), ctrl_pressed_(false),
                 alt_pressed_(false), capslock_(false) {}
    
    void init() {
        // Wait for keyboard controller
        waitInput();
        
        // Disable devices
        outb(KbdPort::COMMAND, 0xAD);
        waitInput();
        outb(KbdPort::COMMAND, 0xA7);
        
        // Flush output buffer
        inb(KbdPort::DATA);
        
        // Read controller config
        waitInput();
        outb(KbdPort::COMMAND, 0x20);
        waitOutput();
        uint8_t config = inb(KbdPort::DATA);
        
        // Disable IRQs and translation
        config &= ~0x43;
        
        // Write config
        waitInput();
        outb(KbdPort::COMMAND, 0x60);
        waitInput();
        outb(KbdPort::DATA, config);
        
        // Self test
        waitInput();
        outb(KbdPort::COMMAND, 0xAA);
        waitOutput();
        if (inb(KbdPort::DATA) != 0x55) {
            return;  // Controller test failed
        }
        
        // Enable first port
        waitInput();
        outb(KbdPort::COMMAND, 0xAE);
        
        // Enable IRQ
        config |= 0x01;
        waitInput();
        outb(KbdPort::COMMAND, 0x60);
        waitInput();
        outb(KbdPort::DATA, config);
        
        // Reset keyboard
        waitInput();
        outb(KbdPort::DATA, 0xFF);
        waitOutput();
        inb(KbdPort::DATA);  // ACK
    }
    
    // Called from IRQ handler
    void handleInterrupt() {
        uint8_t scancode = inb(KbdPort::DATA);
        
        // Handle key release
        if (scancode & Key::RELEASE) {
            uint8_t key = scancode & ~Key::RELEASE;
            if (key == Key::LSHIFT || key == Key::RSHIFT) {
                shift_pressed_ = false;
            } else if (key == Key::CTRL) {
                ctrl_pressed_ = false;
            } else if (key == Key::ALT) {
                alt_pressed_ = false;
            }
            return;
        }
        
        // Handle modifier keys
        if (scancode == Key::LSHIFT || scancode == Key::RSHIFT) {
            shift_pressed_ = true;
            return;
        }
        if (scancode == Key::CTRL) {
            ctrl_pressed_ = true;
            return;
        }
        if (scancode == Key::ALT) {
            alt_pressed_ = true;
            return;
        }
        if (scancode == Key::CAPSLOCK) {
            capslock_ = !capslock_;
            return;
        }
        
        // Convert to ASCII
        char c = 0;
        if (scancode < 128) {
            bool use_shift = shift_pressed_ ^ capslock_;
            c = use_shift ? SCANCODE_TO_ASCII_SHIFT[scancode] : SCANCODE_TO_ASCII[scancode];
        }
        
        // Handle Ctrl+C
        if (ctrl_pressed_ && (c == 'c' || c == 'C')) {
            c = 3;  // ETX
        }
        
        // Add to buffer
        if (c) {
            size_t next = (write_pos_ + 1) % BUFFER_SIZE;
            if (next != read_pos_) {
                buffer_[write_pos_] = c;
                write_pos_ = next;
            }
        }
    }
    
    bool hasInput() const {
        return read_pos_ != write_pos_;
    }
    
    char getchar() {
        while (!hasInput()) {
            asm volatile("hlt");
        }
        
        char c = buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) % BUFFER_SIZE;
        return c;
    }
    
    // Non-blocking read
    int read(char* buf, size_t len) {
        size_t count = 0;
        while (count < len && hasInput()) {
            buf[count++] = buffer_[read_pos_];
            read_pos_ = (read_pos_ + 1) % BUFFER_SIZE;
        }
        return count;
    }
    
    bool isShiftPressed() const { return shift_pressed_; }
    bool isCtrlPressed() const { return ctrl_pressed_; }
    bool isAltPressed() const { return alt_pressed_; }
};

// Global keyboard instance
inline Keyboard& getKeyboard() {
    static Keyboard kbd;
    return kbd;
}

} // namespace drivers
