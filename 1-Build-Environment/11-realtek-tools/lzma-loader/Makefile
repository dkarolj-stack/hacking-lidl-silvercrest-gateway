##############################################################################
# LZMA Kernel Loader Makefile - Optimized & Secured
# Target: Realtek RTL8196E SoC (MIPS RLX4380)
#
# Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
# Copyright (C) 2017 Weijie Gao <hackpascal@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2.
##############################################################################

# Memory layout configuration
LOADADDR        := 0x80000000  # Kernel decompression target address
LZMA_TEXT_START := 0x81000000  # Loader execution address
LOADER_DATA     :=              # Path to compressed kernel (set by flash.sh)
KERNEL_DIR      :=              # Path to kernel source (set by flash.sh)
BOARD           := GWR300N      # Board identifier

# Toolchain configuration
CC              := $(CROSS_COMPILE)gcc
LD              := $(CROSS_COMPILE)ld
OBJCOPY         := $(CROSS_COMPILE)objcopy
OBJDUMP         := $(CROSS_COMPILE)objdump

# Binary generation flags
BIN_FLAGS       := -O binary -R .reginfo -R .note -R .comment -R .mdebug \
                   -R .MIPS.abiflags -S

# Compiler optimization flags
# -O2: Optimize for speed (better than -Os for decompression)
# -ffunction-sections: Place each function in separate section for gc-sections
# -fdata-sections: Same for data
# -flto: Link-Time Optimization for better inlining across files
OPTFLAGS        := -O2 -ffunction-sections -fdata-sections

# Architecture-specific flags
ARCHFLAGS       := -mabi=32 -Wa,-32 -march=lx4380 -Wa,-march=lx4380 \
                   -mno-abicalls -fno-pic -G 0 -mlong-calls -D__RLX__

# Security & correctness flags
# -Wall -Wextra: Enable comprehensive warnings
# -Werror: Treat warnings as errors
# -fno-strict-aliasing: Prevent type-punning issues
# -fomit-frame-pointer: Save register (safe in bootloader context)
WARNFLAGS       := -Wall -Wextra -Wstrict-prototypes -Wno-trigraphs \
                   -Wno-unused-parameter

# Kernel header includes (if kernel source is provided)
KERNEL_INCLUDES :=
ifneq ($(strip $(KERNEL_DIR)),)
KERNEL_INCLUDES := -I$(KERNEL_DIR)/arch/mips/include \
                   -I$(KERNEL_DIR)/include
endif

# Combined compiler flags
CFLAGS          := -D__KERNEL__ -ffreestanding -fno-common \
                   $(WARNFLAGS) $(OPTFLAGS) $(ARCHFLAGS) \
                   -fno-strict-aliasing -fomit-frame-pointer -pipe \
                   -D_LZMA_PROB32 $(KERNEL_INCLUDES)

# Assembly flags
ASFLAGS         = $(CFLAGS) -D__ASSEMBLY__

# Linker flags
# --gc-sections: Remove unused sections (works with -ffunction-sections)
# -static: No dynamic linking in bootloader
LDFLAGS         = -static --gc-sections -no-warn-mismatch
LDFLAGS         += -e startup -T src/loader.lds -Ttext $(LZMA_TEXT_START)

# Object format detection
O_FORMAT        = $(shell $(OBJDUMP) -i | head -2 | grep elf32)

# Object files list
OBJECTS         := src/head.o src/loader.o src/cache.o \
                   src/printf.o src/LzmaDecode.o

# Add compressed kernel data if specified
ifneq ($(strip $(LOADER_DATA)),)
OBJECTS         += data.o
CFLAGS          += -DLZMA_WRAPPER=1 -DLOADADDR=$(LOADADDR)
endif

# Add board-specific defines
BOARD_DEF := $(shell echo $(strip $(BOARD)) | tr a-z A-Z | tr - _)
ifneq ($(BOARD_DEF),)
CFLAGS          += -DCONFIG_BOARD_$(BOARD_DEF)
ifneq ($(strip $(KERNEL_CMDLINE)),)
CFLAGS          += -DCONFIG_KERNEL_CMDLINE='"$(KERNEL_CMDLINE)"'
endif
endif

##############################################################################
# Build targets
##############################################################################

.PHONY: all clean

all: loader.bin

# Compile C sources
%.o : %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Compile assembly sources
%.o : %.S
	@echo "  AS      $<"
	@$(CC) $(ASFLAGS) -c -o $@ $<

# Embed compressed kernel data
data.o: $(LOADER_DATA)
	@echo "  LD      $@ (embedding $(shell du -h $(LOADER_DATA) | cut -f1) kernel)"
	@$(LD) -r -b binary --oformat $(O_FORMAT) -T src/lzma-data.lds -o $@ $<

# Link loader
loader: $(OBJECTS)
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Generate binary
loader.bin: loader
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) $(BIN_FLAGS) $< $@
	@printf "  Built: $@ (%s)\n" "$$(du -h $@ | cut -f1)"

# Alternative ELF format (unused in current workflow)
loader2.o: loader.bin
	@$(LD) -r -b binary --oformat $(O_FORMAT) -o $@ $<

loader.elf: loader2.o
	@$(LD) -e startup -T src/loader2.lds -Ttext $(LOADADDR) -o $@ $<

# Clean build artifacts
clean:
	@echo "  CLEAN"
	@rm -f src/*.o *.o loader *.elf *.bin
