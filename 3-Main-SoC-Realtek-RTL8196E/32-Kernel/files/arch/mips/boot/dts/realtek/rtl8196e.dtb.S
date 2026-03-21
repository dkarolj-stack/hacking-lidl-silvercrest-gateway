#include <asm-generic/vmlinux.lds.h>
.section .dtb.init.rodata,"a"
.balign STRUCT_ALIGNMENT
.global __dtb_rtl8196e_begin
__dtb_rtl8196e_begin:
.incbin "arch/mips/boot/dts/realtek/rtl8196e.dtb" 
__dtb_rtl8196e_end:
.global __dtb_rtl8196e_end
.balign STRUCT_ALIGNMENT
