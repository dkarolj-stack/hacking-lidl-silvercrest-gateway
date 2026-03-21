#!/bin/bash
# build_kernel.sh — Build Linux 5.10.246 for Realtek RTL8196E (Lexra MIPS)
#
# Usage: ./build_kernel.sh [clean|menuconfig]
#
# Options:
#   (none)      Normal build (download if needed, patch, compile)
#   clean       Remove kernel source directory and rebuild from scratch
#   menuconfig  Run kernel menuconfig, optionally save config to files/
#
# This script:
#   - Downloads Linux 5.10.246 kernel sources
#   - Applies Realtek RTL8196E patches
#   - Copies new Realtek-specific files
#   - Compiles the kernel with Lexra toolchain
#   - Creates compressed kernel image with lzma-loader
#   - Packages final image with cvimg
#
# Output: kernel.img (ready to flash via TFTP)
#
# J. Nilo - November 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Project root is 3 levels up: 32-Kernel -> 3-Main-SoC -> project root
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Parse command line options
DO_CLEAN=false
DO_MENUCONFIG=false

case "${1:-}" in
    clean)
        DO_CLEAN=true
        ;;
    menuconfig)
        DO_MENUCONFIG=true
        ;;
    "")
        # No option, normal build
        ;;
    *)
        echo "Usage: $0 [clean|menuconfig]"
        echo ""
        echo "Options:"
        echo "  clean       Remove kernel source directory and rebuild from scratch"
        echo "  menuconfig  Run kernel menuconfig for configuration"
        echo ""
        exit 1
        ;;
esac

# Kernel configuration
KERNEL_VERSION="5.10.246"
KERNEL_MAJOR="5.x"
KERNEL_TARBALL="linux-${KERNEL_VERSION}.tar.xz"
KERNEL_URL="https://cdn.kernel.org/pub/linux/kernel/v${KERNEL_MAJOR}/${KERNEL_TARBALL}"
VANILLA_DIR="linux-${KERNEL_VERSION}"
KERNEL_DIR="linux-${KERNEL_VERSION}-rtl8196e"
KERNEL_CMDLINE="console=ttyS0,115200"

# Toolchain
TOOLCHAIN_DIR="${PROJECT_ROOT}/x-tools/mips-lexra-linux-musl"
export PATH="${TOOLCHAIN_DIR}/bin:$PATH"
export ARCH=mips
export CROSS_COMPILE=mips-lexra-linux-musl-

# Tools - check multiple locations (workspace mount or Docker image paths)
BUILD_ENV="${PROJECT_ROOT}/1-Build-Environment/11-realtek-tools"
DOCKER_TOOLS="/home/builder/realtek-tools"

# Find cvimg
if [ -x "${BUILD_ENV}/bin/cvimg" ]; then
    CVIMG="${BUILD_ENV}/bin/cvimg"
elif [ -x "${DOCKER_TOOLS}/bin/cvimg" ]; then
    CVIMG="${DOCKER_TOOLS}/bin/cvimg"
else
    CVIMG=""
fi

# Find lzma (must use LZMA SDK, not system xz-utils - different syntax)
if [ -x "${BUILD_ENV}/bin/lzma" ]; then
    LZMA="${BUILD_ENV}/bin/lzma"
elif [ -x "${DOCKER_TOOLS}/bin/lzma" ]; then
    LZMA="${DOCKER_TOOLS}/bin/lzma"
else
    LZMA=""
fi

# Find lzma-loader directory
if [ -d "${BUILD_ENV}/lzma-loader" ]; then
    LOADER_DIR="${BUILD_ENV}/lzma-loader"
elif [ -d "${DOCKER_TOOLS}/lzma-loader" ]; then
    LOADER_DIR="${DOCKER_TOOLS}/lzma-loader"
else
    LOADER_DIR=""
fi

# RTL bootloader settings
CVIMG_START_ADDR="0x80c00000"
CVIMG_BURN_ADDR="0x00020000"
SIGNATURE="cs6c"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo "==================================================================="
echo "  Linux ${KERNEL_VERSION} for Realtek RTL8196E (Lexra MIPS)"
echo "==================================================================="
echo ""

# Check toolchain
if ! command -v ${CROSS_COMPILE}gcc >/dev/null 2>&1; then
    echo -e "${RED}❌ Lexra toolchain not found${NC}"
    echo ""
    echo "Build the toolchain first:"
    echo "  cd ../../1-Build-Environment/10-lexra-toolchain"
    echo "  ./build_toolchain.sh"
    echo ""
    exit 1
fi

echo -e "${GREEN}✅ Toolchain found: $(${CROSS_COMPILE}gcc --version | head -1)${NC}"
echo ""

# Check tools
if [ -z "$CVIMG" ] || [ -z "$LZMA" ] || [ -z "$LOADER_DIR" ]; then
    echo -e "${RED}❌ Realtek tools not found${NC}"
    echo ""
    echo "Missing tools:"
    [ -z "$CVIMG" ] && echo "  - cvimg"
    [ -z "$LZMA" ] && echo "  - lzma (LZMA SDK)"
    [ -z "$LOADER_DIR" ] && echo "  - lzma-loader"
    echo ""
    echo "Build realtek-tools first:"
    echo "  cd ../../1-Build-Environment/11-realtek-tools && ./build_tools.sh"
    echo ""
    exit 1
fi

echo "Tools found:"
echo "  • cvimg: $CVIMG"
echo "  • lzma: $LZMA"
echo "  • lzma-loader: $LOADER_DIR"
echo ""

# Handle clean option
if [ "$DO_CLEAN" = true ]; then
    if [ -d "$KERNEL_DIR" ]; then
        echo -e "${YELLOW}Removing patched kernel directory...${NC}"
        rm -rf "$KERNEL_DIR"
        echo -e "${GREEN}✅ Patched kernel removed: ${KERNEL_DIR}${NC}"
        echo ""
    else
        echo -e "${GREEN}✅ Patched kernel directory does not exist${NC}"
        echo ""
    fi
fi

# Create patched kernel directory if needed
if [ ! -d "$KERNEL_DIR" ]; then
    echo -e "${YELLOW}Downloading Linux ${KERNEL_VERSION}...${NC}"
    wget -q --show-progress "$KERNEL_URL"
    echo -e "${YELLOW}Extracting vanilla kernel...${NC}"
    tar xf "$KERNEL_TARBALL"
    mv "$VANILLA_DIR" "$KERNEL_DIR"
    rm -f "$KERNEL_TARBALL"
    echo -e "${GREEN}✅ Kernel directory ready: ${KERNEL_DIR}${NC}"
    echo ""
else
    echo -e "${GREEN}✅ Patched kernel already present: ${KERNEL_DIR}${NC}"
    echo ""
fi

cd "$KERNEL_DIR"

# Apply patches
echo -e "${YELLOW}Applying RTL8196E patches...${NC}"
PATCH_COUNT=0
for patch in "${SCRIPT_DIR}/patches"/*.patch; do
    if [ -f "$patch" ]; then
        echo "  • $(basename $patch)"
        patch -p1 -N < "$patch" 2>/dev/null || echo "    (already applied)"
        PATCH_COUNT=$((PATCH_COUNT + 1))
    fi
done
echo -e "${GREEN}✅ ${PATCH_COUNT} patches processed${NC}"
echo ""

# Copy new Realtek files
echo -e "${YELLOW}Copying Realtek-specific files...${NC}"
cp -rv "${SCRIPT_DIR}/files/arch" .
cp -rv "${SCRIPT_DIR}/files/drivers" .
echo -e "${GREEN}✅ Realtek files copied${NC}"
echo ""

# Setup kernel configuration
if [ ! -f .config ]; then
    echo -e "${YELLOW}Setting up kernel configuration...${NC}"
    if [ -f "${SCRIPT_DIR}/config-5.10.246-realtek.txt" ]; then
        cp "${SCRIPT_DIR}/config-5.10.246-realtek.txt" .config
        make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE olddefconfig
        echo -e "${GREEN}✅ Configuration ready${NC}"
    else
        echo -e "${RED}❌ Configuration file not found${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✅ Configuration already present${NC}"
fi
echo ""

# Handle menuconfig option
if [ "$DO_MENUCONFIG" = true ]; then
    echo -e "${YELLOW}Running menuconfig...${NC}"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE menuconfig

    echo ""
    echo -e "${YELLOW}Configuration modified. Save to config-5.10.246-realtek.txt?${NC}"
    read -p "(y/n) " -n 1 -r
    echo ""

    if [[ $REPLY =~ ^[Yy]$ ]]; then
        cp .config "${SCRIPT_DIR}/config-5.10.246-realtek.txt"
        echo -e "${GREEN}✅ Configuration saved to config-5.10.246-realtek.txt${NC}"
    else
        echo -e "${YELLOW}Configuration NOT saved (changes remain in ${KERNEL_DIR}/.config)${NC}"
    fi
    echo ""

    read -p "Continue with kernel build? (y/n) " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Build cancelled."
        exit 0
    fi
    echo ""
fi

# Build kernel
JOBS=$(nproc)
echo -e "${YELLOW}Building kernel with $JOBS parallel jobs...${NC}"
echo ""

if ! make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$JOBS; then
    echo ""
    echo "==================================================================="
    echo -e "${RED}❌ KERNEL BUILD FAILED${NC}"
    echo "==================================================================="
    exit 1
fi

echo ""
echo "==================================================================="
echo -e "${GREEN}✅ KERNEL COMPILATION SUCCESSFUL${NC}"
echo "==================================================================="
echo ""

# Show compiled kernel info
if [ -f vmlinux ]; then
    echo "vmlinux: $(ls -lh vmlinux | awk '{print $5}')"
fi

echo ""
echo "==================================================================="
echo "  PACKAGING KERNEL IMAGE"
echo "==================================================================="
echo ""

# Clean old images
rm -f "${SCRIPT_DIR}/kernel.img"

# Step 1: Convert ELF to raw binary
echo -e "${YELLOW}Step 1/4: Converting vmlinux to raw binary...${NC}"
${CROSS_COMPILE}objcopy -O binary \
    -R .reginfo -R .note -R .comment \
    -R .mdebug -R .MIPS.abiflags -S \
    vmlinux vmlinux.bin

elf_size=$(stat -c%s vmlinux)
raw_size=$(stat -c%s vmlinux.bin)
echo "  • ELF size: $(numfmt --to=iec-i --suffix=B $elf_size)"
echo "  • Raw size: $(numfmt --to=iec-i --suffix=B $raw_size)"
echo "  • Stripped: $(numfmt --to=iec-i --suffix=B $((elf_size - raw_size)))"
echo ""

# Step 2: Compress with LZMA
echo -e "${YELLOW}Step 2/4: Compressing with LZMA...${NC}"
$LZMA e vmlinux.bin vmlinux.bin.lzma -lc1 -lp2 -pb2 >/dev/null 2>&1

compressed_size=$(stat -c%s vmlinux.bin.lzma)
compression_ratio=$(awk "BEGIN {printf \"%.1f:1\", $raw_size*1.0/$compressed_size}")
echo "  • Compressed size: $(numfmt --to=iec-i --suffix=B $compressed_size)"
echo "  • Compression ratio: $compression_ratio"
echo ""

# Step 3: Build lzma-loader (standalone)
echo -e "${YELLOW}Step 3/4: Building lzma-loader...${NC}"

if [ ! -d "$LOADER_DIR" ]; then
    echo -e "${RED}❌ lzma-loader not found in $LOADER_DIR${NC}"
    exit 1
fi

PATH="${TOOLCHAIN_DIR}/bin:$PATH" make -C "$LOADER_DIR" \
    CROSS_COMPILE=$CROSS_COMPILE \
    LOADER_DATA="${SCRIPT_DIR}/${KERNEL_DIR}/vmlinux.bin.lzma" \
    KERNEL_DIR="${SCRIPT_DIR}/${KERNEL_DIR}" \
    KERNEL_CMDLINE="$KERNEL_CMDLINE" \
    clean all

if [ ! -f "$LOADER_DIR/loader.bin" ]; then
    echo -e "${RED}❌ lzma-loader build failed${NC}"
    exit 1
fi

loader_size=$(stat -c%s "$LOADER_DIR/loader.bin")
echo "  • Loader+kernel: $(numfmt --to=iec-i --suffix=B $loader_size)"
echo ""

# Step 4: Create final image with cvimg
echo -e "${YELLOW}Step 4/4: Creating bootable image with cvimg...${NC}"

$CVIMG \
    -i "$LOADER_DIR/loader.bin" \
    -o "${SCRIPT_DIR}/kernel.img" \
    -s "$SIGNATURE" \
    -e "$CVIMG_START_ADDR" \
    -b "$CVIMG_BURN_ADDR" \
    -a 4k >/dev/null

image_size=$(stat -c%s "${SCRIPT_DIR}/kernel.img")

echo ""
echo "==================================================================="
echo "  BUILD SUMMARY"
echo "==================================================================="
echo ""
compression_pct=$(LC_NUMERIC=C awk "BEGIN {printf \"%.1f\", ($compressed_size*100.0/$raw_size)}")
printf "  %-25s : %s\n" "Kernel (raw)" "$(numfmt --to=iec-i --suffix=B $raw_size)"
printf "  %-25s : %s (${compression_pct}%%)\n" "Kernel (LZMA)" "$(numfmt --to=iec-i --suffix=B $compressed_size)"
printf "  %-25s : %s\n" "Loader + kernel" "$(numfmt --to=iec-i --suffix=B $loader_size)"
printf "  %-25s : %s\n" "Final image (cvimg)" "$(numfmt --to=iec-i --suffix=B $image_size)"
echo ""
printf "  %-25s : %s\n" "Compression ratio" "$compression_ratio"
printf "  %-25s : %s\n" "Output file" "${SCRIPT_DIR}/kernel.img"
echo ""
echo "✅ Kernel image ready: kernel.img"
echo ""
echo "To flash: ./flash_kernel.sh"
