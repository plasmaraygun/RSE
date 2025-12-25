#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>

/**
 * Device Abstraction for Braided OS
 * 
 * Unified interface for all devices (console, disk, network, etc.).
 */

namespace os {

// Device types
enum class DeviceType {
    CHARACTER,  // Stream of bytes (console, serial)
    BLOCK       // Fixed-size blocks (disk, SSD)
};

// Forward declaration
struct Device;

// Device operation function pointers
typedef int (*DeviceOpenFunc)(Device* dev);
typedef int (*DeviceCloseFunc)(Device* dev);
typedef ssize_t (*DeviceReadFunc)(Device* dev, void* buf, size_t count);
typedef ssize_t (*DeviceWriteFunc)(Device* dev, const void* buf, size_t count);
typedef int (*DeviceIoctlFunc)(Device* dev, unsigned long request, void* arg);

/**
 * Device structure.
 */
struct Device {
    char name[64];             // Device name
    DeviceType type;           // Character or block
    void* private_data;        // Driver-specific data
    
    // Operations
    DeviceOpenFunc open;
    DeviceCloseFunc close;
    DeviceReadFunc read;
    DeviceWriteFunc write;
    DeviceIoctlFunc ioctl;
    
    Device() 
        : type(DeviceType::CHARACTER), private_data(nullptr),
          open(nullptr), close(nullptr), read(nullptr), write(nullptr), ioctl(nullptr) {
        name[0] = '\0';
    }
};

/**
 * Device Manager.
 * 
 * Manages all devices in the system.
 */
class DeviceManager {
private:
    static constexpr uint32_t MAX_DEVICES = 256;
    Device* devices_[MAX_DEVICES];
    uint32_t num_devices_;
    
public:
    DeviceManager() : num_devices_(0) {
        for (uint32_t i = 0; i < MAX_DEVICES; i++) {
            devices_[i] = nullptr;
        }
    }
    
    /**
     * Register a device.
     * Returns true on success, false if no space.
     */
    bool registerDevice(Device* dev) {
        if (num_devices_ >= MAX_DEVICES) {
            std::cerr << "[DeviceManager] No space for device: " << dev->name << std::endl;
            return false;
        }
        
        // Check if device with same name already exists
        for (uint32_t i = 0; i < num_devices_; i++) {
            if (devices_[i] && strcmp(devices_[i]->name, dev->name) == 0) {
                std::cerr << "[DeviceManager] Device already registered: " << dev->name << std::endl;
                return false;
            }
        }
        
        devices_[num_devices_++] = dev;
        
        std::cout << "[DeviceManager] Registered device: " << dev->name << std::endl;
        
        return true;
    }
    
    /**
     * Unregister a device.
     */
    bool unregisterDevice(const char* name) {
        for (uint32_t i = 0; i < num_devices_; i++) {
            if (devices_[i] && strcmp(devices_[i]->name, name) == 0) {
                // Shift remaining devices
                for (uint32_t j = i; j < num_devices_ - 1; j++) {
                    devices_[j] = devices_[j + 1];
                }
                devices_[num_devices_ - 1] = nullptr;
                num_devices_--;
                
                std::cout << "[DeviceManager] Unregistered device: " << name << std::endl;
                
                return true;
            }
        }
        
        std::cerr << "[DeviceManager] Device not found: " << name << std::endl;
        return false;
    }
    
    /**
     * Look up a device by name.
     * Returns pointer to device, or nullptr if not found.
     */
    Device* lookup(const char* name) {
        for (uint32_t i = 0; i < num_devices_; i++) {
            if (devices_[i] && strcmp(devices_[i]->name, name) == 0) {
                return devices_[i];
            }
        }
        
        return nullptr;
    }
    
    /**
     * List all devices.
     */
    void list() const {
        std::cout << "[DeviceManager] Devices (" << num_devices_ << "):" << std::endl;
        for (uint32_t i = 0; i < num_devices_; i++) {
            if (devices_[i]) {
                const char* type_str = (devices_[i]->type == DeviceType::CHARACTER) ? "char" : "block";
                std::cout << "  " << devices_[i]->name << " (" << type_str << ")" << std::endl;
            }
        }
    }
    
    /**
     * Get number of registered devices.
     */
    uint32_t count() const {
        return num_devices_;
    }
    
    /**
     * Print statistics.
     */
    void printStats() const {
        std::cout << "[DeviceManager] Devices: " << num_devices_ << " / " << MAX_DEVICES << std::endl;
    }
};

} // namespace os
