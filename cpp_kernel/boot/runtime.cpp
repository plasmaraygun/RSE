/**
 * Freestanding Runtime Support
 * Minimal libc/libstdc++ stubs for kernel
 */

#include <cstdint>

extern "C" {

// String functions
int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *reinterpret_cast<const unsigned char*>(s1) - 
           *reinterpret_cast<const unsigned char*>(s2);
}

void* memcpy(void* dest, const void* src, uint64_t n) {
    auto* d = static_cast<uint8_t*>(dest);
    auto* s = static_cast<const uint8_t*>(src);
    while (n--) *d++ = *s++;
    return dest;
}

void* memset(void* dest, int c, uint64_t n) {
    auto* d = static_cast<uint8_t*>(dest);
    while (n--) *d++ = static_cast<uint8_t>(c);
    return dest;
}

void* memmove(void* dest, const void* src, uint64_t n) {
    auto* d = static_cast<uint8_t*>(dest);
    auto* s = static_cast<const uint8_t*>(src);
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

uint64_t strlen(const char* s) {
    uint64_t len = 0;
    while (*s++) len++;
    return len;
}

// C++ ABI guard functions for static initialization
int __cxa_guard_acquire(uint64_t* guard) {
    if (*guard & 1) return 0;
    *guard = 1;
    return 1;
}

void __cxa_guard_release(uint64_t* guard) {
    *guard = 1;
}

void __cxa_guard_abort(uint64_t*) {}

void __cxa_pure_virtual() {
    asm volatile("cli; hlt");
}

// Stack protector
uint64_t __stack_chk_guard = 0xDEADBEEFCAFEBABEULL;
void __stack_chk_fail() { asm volatile("cli; hlt"); }

} // extern "C"
