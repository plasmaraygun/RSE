# I/O System Design

**Goal**: Provide device abstraction and I/O operations for applications.

---

## Overview

The I/O system allows applications to interact with hardware devices (console, disk, network, etc.) through a unified interface.

Key responsibilities:
1. **Device abstraction** - Uniform interface for all devices
2. **Console I/O** - stdin, stdout, stderr
3. **Device drivers** - Hardware-specific implementations
4. **Buffering** - Efficient I/O with buffering
5. **Integration with VFS** - Devices as files (/dev/console, /dev/null, etc.)

---

## Key Concepts

### **Device**

A **device** is an abstraction for hardware:

```cpp
struct Device {
    char name[64];             // Device name (e.g., "console")
    DeviceType type;           // Character or block device
    
    // Operations
    int (*open)(Device* dev);
    int (*close)(Device* dev);
    ssize_t (*read)(Device* dev, void* buf, size_t count);
    ssize_t (*write)(Device* dev, const void* buf, size_t count);
    int (*ioctl)(Device* dev, unsigned long request, void* arg);
};
```

### **Device Types**

- **Character devices**: Stream of bytes (console, serial port)
- **Block devices**: Fixed-size blocks (disk, SSD)

### **Special Devices**

- **/dev/console** - System console (keyboard + screen)
- **/dev/null** - Null device (discards all writes)
- **/dev/zero** - Zero device (returns zeros)
- **/dev/random** - Random number generator

---

## I/O Architecture

### **Layered Design**

```
┌─────────────────────────────────────┐
│      Application Layer              │
│   (printf, scanf, read, write)      │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│      VFS Layer                      │
│   (File descriptors, open/close)    │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│      Device Layer                   │
│   (Device abstraction)              │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│      Driver Layer                   │
│   (Console, disk, network drivers)  │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│      Hardware Layer                 │
│   (Actual hardware)                 │
└─────────────────────────────────────┘
```

### **Per-Torus I/O**

Each torus has its own device table:

```
Torus A:
- Console device
- Disk device
- Network device

Torus B:
- Console device
- Disk device
- Network device

Torus C:
- Console device
- Disk device
- Network device
```

**No global device manager** → No bottleneck!

---

## Console Driver

### **Console Device**

The console is a character device that provides:
- **stdin** (FD 0) - Read from keyboard
- **stdout** (FD 1) - Write to screen
- **stderr** (FD 2) - Write to screen (errors)

### **Console Operations**

```cpp
// Read from console (blocking)
ssize_t console_read(Device* dev, void* buf, size_t count);

// Write to console
ssize_t console_write(Device* dev, const void* buf, size_t count);
```

### **Buffering**

For efficiency, we buffer console I/O:
- **Input buffer** - Store typed characters until Enter is pressed
- **Output buffer** - Batch writes to reduce syscalls

---

## Integration with VFS

### **Device Files**

Devices appear as files in /dev:

```
/dev/console  → Console device
/dev/null     → Null device
/dev/zero     → Zero device
```

### **Opening Devices**

```cpp
int fd = open("/dev/console", O_RDWR);
write(fd, "Hello\n", 6);
close(fd);
```

### **stdin/stdout/stderr**

FDs 0, 1, 2 are pre-opened to /dev/console:

```cpp
// Application code
printf("Hello");  // → write(1, "Hello", 5) → /dev/console
```

---

## Simplified Implementation (Phase 6.4)

For now, we'll implement a **simple console driver**:

### **Console Features**

- Read from stdin (simulated with std::cin)
- Write to stdout/stderr (simulated with std::cout/std::cerr)
- Line buffering for input
- No buffering for output (for simplicity)

### **Console Structure**

```cpp
struct ConsoleDevice {
    char input_buffer[1024];
    uint32_t input_size;
    uint32_t input_pos;
    
    ssize_t read(void* buf, size_t count);
    ssize_t write(const void* buf, size_t count);
};
```

### **Limitations**

- No real hardware access (simulated with std::cin/cout)
- No interrupt handling (polling only)
- No advanced features (colors, cursor control, etc.)

**But it's enough to prove the concept!**

---

## Implementation Plan

### **Phase 6.4.1: Device Abstraction**
- Implement Device structure
- Implement DeviceManager
- Implement device registration

### **Phase 6.4.2: Console Driver**
- Implement ConsoleDevice
- Implement read/write operations
- Implement buffering

### **Phase 6.4.3: Integration**
- Connect devices to VFS
- Map FDs 0, 1, 2 to console
- Update syscalls to use devices

### **Phase 6.4.4: Testing**
- Test console I/O
- Test device operations
- Test integration with VFS

---

## Success Criteria

### **Functional**
- ✅ Console read works (stdin)
- ✅ Console write works (stdout/stderr)
- ✅ FDs 0, 1, 2 work correctly
- ✅ Applications can use printf/scanf
- ✅ Device abstraction is clean

### **Performance**
- ✅ O(1) device lookup
- ✅ Minimal overhead
- ✅ Efficient buffering

---

## Future Enhancements

### **Phase 6.4+**
- Real hardware drivers (keyboard, VGA, serial)
- Interrupt handling (IRQs)
- DMA (Direct Memory Access)
- Block devices (disk, SSD)
- Network devices (Ethernet, WiFi)
- Device hotplug
- Power management

---

## The Vision

**Traditional OS**: Global device manager → bottleneck

**Braided OS**: Per-torus device manager → no bottleneck

Each torus manages its own devices independently. No locks, no contention, perfect scaling.

**This is how we make old hardware fast.** 🚀
