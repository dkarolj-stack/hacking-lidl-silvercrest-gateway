OUTPUT_ARCH(mips)
ENTRY(startup)

SECTIONS {
    . = 0x81000000;
    
    _code_start = .;
    
    .text : {
        *(.text*)
        *(.rodata*)
    }
    
    _code_end = .;
    
    .data : {
        *(.data*)
        _lzma_data_start = .;
        *(.data)
        _lzma_data_end = .;
    }
    
    _bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    _bss_end = .;
    
    . = ALIGN(16);
    _stack = . + 0x4000;
    
    . = ALIGN(16);
    workspace = .;
}
