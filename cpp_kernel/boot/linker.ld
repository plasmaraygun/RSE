/* ARQON RSE Kernel Linker Script */

ENTRY(_start)

SECTIONS
{
    /* Kernel loads at 1MB */
    . = 1M;
    
    _kernel_start = .;
    
    /* Multiboot header must be early */
    .multiboot ALIGN(4K) :
    {
        *(.multiboot)
    }
    
    /* Code section */
    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    }
    
    /* Read-only data */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }
    
    /* Uninitialized data */
    _bss_start = .;
    .bss ALIGN(4K) :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    _bss_end = .;
    
    _kernel_end = .;
    
    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
