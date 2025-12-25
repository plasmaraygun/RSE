#pragma once

#include "Device.h"
#include <iostream>
#include <cstring>

/**
 * Console Device Driver for Braided OS
 * 
 * Provides stdin, stdout, stderr through a character device.
 * Simulated with std::cin/cout for now (no real hardware access).
 */

namespace os {

/**
 * Console device private data.
 */
struct ConsoleData {
    char input_buffer[1024];   // Line buffer for input
    uint32_t input_size;       // Current size of input buffer
    uint32_t input_pos;        // Current read position
    bool has_input;            // Is there buffered input?
    
    ConsoleData() : input_size(0), input_pos(0), has_input(false) {
        input_buffer[0] = '\0';
    }
};

/**
 * Console device operations.
 */

// Open console
int console_open(Device* dev) {
    std::cout << "[Console] Opened" << std::endl;
    return 0;
}

// Close console
int console_close(Device* dev) {
    std::cout << "[Console] Closed" << std::endl;
    return 0;
}

// Read from console (stdin)
ssize_t console_read(Device* dev, void* buf, size_t count) {
    ConsoleData* data = (ConsoleData*)dev->private_data;
    
    // If no buffered input, read a line
    if (!data->has_input || data->input_pos >= data->input_size) {
        std::cout << "[Console] Reading line..." << std::endl;
        
        // Read line from stdin
        if (!std::cin.getline(data->input_buffer, sizeof(data->input_buffer))) {
            return -1;  // EOF or error
        }
        
        data->input_size = strlen(data->input_buffer);
        
        // Add newline back (getline removes it)
        if (data->input_size < sizeof(data->input_buffer) - 1) {
            data->input_buffer[data->input_size++] = '\n';
            data->input_buffer[data->input_size] = '\0';
        }
        
        data->input_pos = 0;
        data->has_input = true;
    }
    
    // Copy from buffer to user buffer
    size_t available = data->input_size - data->input_pos;
    size_t to_copy = (count < available) ? count : available;
    
    memcpy(buf, data->input_buffer + data->input_pos, to_copy);
    data->input_pos += to_copy;
    
    // If we've consumed all input, mark as no input
    if (data->input_pos >= data->input_size) {
        data->has_input = false;
    }
    
    return to_copy;
}

// Write to console (stdout/stderr)
ssize_t console_write(Device* dev, const void* buf, size_t count) {
    // Write directly to stdout (no buffering for simplicity)
    std::cout.write((const char*)buf, count);
    std::cout.flush();
    
    return count;
}

// ioctl (not implemented)
int console_ioctl(Device* dev, unsigned long request, void* arg) {
    std::cerr << "[Console] ioctl not implemented" << std::endl;
    return -1;
}

/**
 * Create and initialize console device.
 */
Device* create_console_device() {
    Device* dev = new Device();
    
    strncpy(dev->name, "console", sizeof(dev->name) - 1);
    dev->type = DeviceType::CHARACTER;
    dev->private_data = new ConsoleData();
    
    dev->open = console_open;
    dev->close = console_close;
    dev->read = console_read;
    dev->write = console_write;
    dev->ioctl = console_ioctl;
    
    return dev;
}

/**
 * Destroy console device.
 */
void destroy_console_device(Device* dev) {
    if (dev->private_data) {
        delete (ConsoleData*)dev->private_data;
    }
    delete dev;
}

} // namespace os
