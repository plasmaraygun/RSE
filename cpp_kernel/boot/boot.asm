; ARQON RSE Kernel Boot Assembly
; Multiboot2 header and entry point for x86_64

[BITS 32]

; Multiboot2 constants
MULTIBOOT2_MAGIC        equ 0xe85250d6
MULTIBOOT2_ARCH_I386    equ 0
MULTIBOOT2_HEADER_LEN   equ multiboot_header_end - multiboot_header
MULTIBOOT2_CHECKSUM     equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH_I386 + MULTIBOOT2_HEADER_LEN)

; Page table constants
PAGE_PRESENT    equ 1 << 0
PAGE_WRITE      equ 1 << 1
PAGE_HUGE       equ 1 << 7

section .multiboot
align 8
multiboot_header:
    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH_I386
    dd MULTIBOOT2_HEADER_LEN
    dd MULTIBOOT2_CHECKSUM
    
    ; Framebuffer tag
    align 8
    dw 5                    ; Type: framebuffer
    dw 0                    ; Flags
    dd 20                   ; Size
    dd 1024                 ; Width
    dd 768                  ; Height
    dd 32                   ; Depth
    
    ; End tag
    align 8
    dw 0                    ; Type: end
    dw 0                    ; Flags
    dd 8                    ; Size
multiboot_header_end:

section .bss
align 4096
; Page tables for identity mapping
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096

; Kernel stack
align 16
stack_bottom:
    resb 65536              ; 64KB stack
stack_top:

section .rodata
; GDT for 64-bit mode (must be in initialized section!)
align 16
gdt64:
    dq 0                                              ; Null descriptor
gdt64_code: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)  ; Code segment
gdt64_data: equ $ - gdt64
    dq (1 << 44) | (1 << 47) | (1 << 41)              ; Data segment
gdt64_end:

gdt64_pointer:
    dw gdt64_end - gdt64 - 1    ; Limit
    dq gdt64                     ; Base

section .text
global _start
extern kernel_main

_start:
    ; FIRST: Save multiboot info to memory before ANY register clobber
    mov [saved_magic], eax
    mov [saved_mbi], ebx
    
    ; Initialize serial for debug output (COM1 @ 0x3F8)
    mov dx, 0x3F8 + 1
    xor al, al
    out dx, al              ; Disable interrupts
    mov dx, 0x3F8 + 3
    mov al, 0x80
    out dx, al              ; DLAB on
    mov dx, 0x3F8
    mov al, 1
    out dx, al              ; Divisor = 1 (115200 baud)
    mov dx, 0x3F8 + 1
    xor al, al
    out dx, al
    mov dx, 0x3F8 + 3
    mov al, 0x03
    out dx, al              ; 8N1
    mov dx, 0x3F8 + 2
    mov al, 0xC7
    out dx, al              ; FIFO
    mov dx, 0x3F8 + 4
    mov al, 0x03
    out dx, al              ; RTS/DTR
    
    ; Print boot message
    mov esi, msg_boot
    call serial_print32
    
    ; Disable interrupts
    cli
    
    ; Set up page tables
    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, PAGE_PRESENT | PAGE_WRITE
    mov [pml4], eax
    
    ; PDPT[0] -> PD
    mov eax, pd
    or eax, PAGE_PRESENT | PAGE_WRITE
    mov [pdpt], eax
    
    ; PD entries: identity map first 4GB using 2MB pages
    mov ecx, 0              ; Counter
    mov eax, PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE
.fill_pd:
    mov [pd + ecx * 8], eax
    add eax, 0x200000       ; 2MB
    inc ecx
    cmp ecx, 512            ; 512 entries = 1GB
    jne .fill_pd
    
    ; Also map at higher half (optional for future)
    ; For now, just identity mapping
    
    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5          ; PAE bit
    mov cr4, eax
    
    ; Load PML4
    mov eax, pml4
    mov cr3, eax
    
    ; Enable long mode
    mov ecx, 0xC0000080     ; EFER MSR
    rdmsr
    or eax, 1 << 8          ; LM bit
    wrmsr
    
    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31         ; PG bit
    mov cr0, eax
    
    ; Load 64-bit GDT
    lgdt [gdt64_pointer]
    
    ; Far jump to 64-bit code
    jmp gdt64_code:long_mode_start

; 32-bit serial print (esi = string ptr)
serial_print32:
    lodsb
    test al, al
    jz .done
    mov ah, al
    mov dx, 0x3F8 + 5
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    jmp serial_print32
.done:
    ret

[BITS 64]
long_mode_start:
    ; Print 64-bit mode message
    mov rsi, msg_long
    call serial_print64
    
    ; Set up segment registers
    mov ax, gdt64_data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Set up stack
    mov rsp, stack_top
    
    ; Clear direction flag
    cld
    
    ; Print segments ok
    mov rsi, msg_seg
    call serial_print64
    
    ; Skip BSS zeroing for now - it may be corrupting our data
    ; The C++ static init guards will handle uninitialized data
    
    ; Print before calling kernel
    mov rsi, msg_call
    call serial_print64
    
    ; Prepare arguments for kernel_main
    mov rdi, [rel saved_magic]
    mov rsi, [rel saved_mbi]
    
    ; Call kernel main
    call kernel_main
    
    ; Print if kernel returns
    mov rsi, msg_return
    call serial_print64
    
    ; If kernel returns, halt
.hang:
    cli
    hlt
    jmp .hang

; 64-bit serial print (rsi = string ptr)
serial_print64:
    lodsb
    test al, al
    jz .done
    mov ah, al
    mov dx, 0x3F8 + 5
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    jmp serial_print64
.done:
    ret

section .data
saved_magic: dq 0
saved_mbi: dq 0
msg_boot: db "[BOOT] ARQON 32-bit entry", 13, 10, 0
msg_long: db "[BOOT] 64-bit long mode active", 13, 10, 0
msg_seg: db "[BOOT] Segments/stack ready", 13, 10, 0
msg_call: db "[BOOT] Calling kernel_main...", 13, 10, 0
msg_return: db "[BOOT] kernel_main returned!", 13, 10, 0

section .note.GNU-stack noalloc noexec nowrite progbits
