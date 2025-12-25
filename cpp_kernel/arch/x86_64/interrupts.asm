; Interrupt stubs for x86_64
; These save CPU state and call the C++ interrupt dispatcher

[BITS 64]

section .text

; External C++ dispatcher
extern interrupt_dispatch

; Macro for interrupts without error code
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0              ; Dummy error code
    push qword %1             ; Interrupt number
    jmp isr_common
%endmacro

; Macro for interrupts with error code
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1             ; Interrupt number (error code already pushed)
    jmp isr_common
%endmacro

; CPU Exceptions
ISR_NOERRCODE 0   ; Divide Error
ISR_NOERRCODE 1   ; Debug
ISR_NOERRCODE 2   ; NMI
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Overflow
ISR_NOERRCODE 5   ; Bound Range Exceeded
ISR_NOERRCODE 6   ; Invalid Opcode
ISR_NOERRCODE 7   ; Device Not Available
ISR_ERRCODE   8   ; Double Fault
ISR_NOERRCODE 9   ; Coprocessor Segment Overrun
ISR_ERRCODE   10  ; Invalid TSS
ISR_ERRCODE   11  ; Segment Not Present
ISR_ERRCODE   12  ; Stack-Segment Fault
ISR_ERRCODE   13  ; General Protection Fault
ISR_ERRCODE   14  ; Page Fault
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; x87 FPU Error
ISR_ERRCODE   17  ; Alignment Check
ISR_NOERRCODE 18  ; Machine Check
ISR_NOERRCODE 19  ; SIMD Floating-Point
ISR_NOERRCODE 20  ; Virtualization Exception
ISR_ERRCODE   21  ; Control Protection Exception
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30  ; Security Exception
ISR_NOERRCODE 31

; Hardware IRQs (32-47)
ISR_NOERRCODE 32  ; Timer
ISR_NOERRCODE 33  ; Keyboard
ISR_NOERRCODE 34  ; Cascade
ISR_NOERRCODE 35  ; COM2
ISR_NOERRCODE 36  ; COM1
ISR_NOERRCODE 37  ; LPT2
ISR_NOERRCODE 38  ; Floppy
ISR_NOERRCODE 39  ; LPT1
ISR_NOERRCODE 40  ; RTC
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44  ; Mouse
ISR_NOERRCODE 45  ; FPU
ISR_NOERRCODE 46  ; ATA1
ISR_NOERRCODE 47  ; ATA2

; System call
ISR_NOERRCODE 128 ; syscall (0x80)

; Common interrupt handler
isr_common:
    ; Save all general-purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Pass stack pointer as argument (pointer to InterruptFrame)
    mov rdi, rsp
    
    ; Align stack to 16 bytes
    mov rbp, rsp
    and rsp, -16
    
    ; Call C++ dispatcher
    call interrupt_dispatch
    
    ; Restore stack
    mov rsp, rbp
    
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    
    ; Remove error code and interrupt number
    add rsp, 16
    
    ; Return from interrupt
    iretq

; ISR stub table for easy registration
section .data
global isr_stub_table
isr_stub_table:
    dq isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dq isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dq isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dq isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    dq isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    dq isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
