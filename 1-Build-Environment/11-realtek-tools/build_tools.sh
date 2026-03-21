#!/bin/bash
# build_tools.sh — Build Realtek flash tools (cvimg, lzma, lzma-loader, flash_erase)
#
# Builds native tools required for creating RTL8196E flashable firmware images
# and cross-compiled MTD utilities for the target device
#
# Output: bin/cvimg, bin/lzma (native), bin/flash_erase (cross-compiled MIPS)
#
# J. Nilo - December 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Project root is 2 levels up: 11-realtek-tools -> 1-Build-Environment -> project root
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BIN_DIR="${SCRIPT_DIR}/bin"
MTD_UTILS_DIR="${SCRIPT_DIR}/mtd-utils"
MTD_UTILS_VERSION="2.2.0"
MTD_UTILS_URL="https://github.com/sigma-star/mtd-utils/archive/refs/tags/v${MTD_UTILS_VERSION}.tar.gz"

# Lexra toolchain configuration
TOOLCHAIN_PREFIX="${PROJECT_ROOT}/x-tools/mips-lexra-linux-musl"
CROSS_COMPILE="mips-lexra-linux-musl-"

echo "========================================="
echo "  REALTEK TOOLS BUILD"
echo "========================================="
echo ""

# Create bin directory
mkdir -p "${BIN_DIR}"

# Build cvimg (native x86_64, not cross-compiled!)
echo "Building cvimg..."
cd "${SCRIPT_DIR}/cvimg"
make clean
make CC=gcc
cp cvimg "${BIN_DIR}/"
echo "cvimg built (native x86_64)"
echo ""

# Build lzma
echo "Building lzma (LZMA SDK 4.65)..."
cd "${SCRIPT_DIR}/lzma-4.65/CPP/7zip/Compress/LZMA_Alone"
make -f makefile.gcc clean
make -f makefile.gcc
cp lzma "${BIN_DIR}/"
echo "lzma built"
echo ""

echo "Note: lzma-loader is built by the kernel build script"
echo ""

# Check for Lexra toolchain
echo "Checking Lexra toolchain..."
if [ ! -d "${TOOLCHAIN_PREFIX}" ]; then
    echo "ERROR: Lexra toolchain not found at ${TOOLCHAIN_PREFIX}"
    echo "Please run ../10-lexra-toolchain/build_toolchain.sh first"
    exit 1
fi
export PATH="${TOOLCHAIN_PREFIX}/bin:${PATH}"

if ! command -v ${CROSS_COMPILE}gcc &> /dev/null; then
    echo "ERROR: ${CROSS_COMPILE}gcc not found in PATH"
    exit 1
fi
echo "Toolchain found: $(${CROSS_COMPILE}gcc --version | head -1)"
echo ""

# Download and build mtd-utils (flash_erase)
echo "Building flash_erase (mtd-utils ${MTD_UTILS_VERSION})..."

if [ ! -d "${MTD_UTILS_DIR}" ]; then
    echo "Downloading mtd-utils..."
    cd "${SCRIPT_DIR}"
    wget -q "${MTD_UTILS_URL}" -O mtd-utils.tar.gz
    tar xzf mtd-utils.tar.gz
    mv "mtd-utils-${MTD_UTILS_VERSION}" mtd-utils
    rm mtd-utils.tar.gz
fi

cd "${MTD_UTILS_DIR}"

# Clean previous build if exists
make clean 2>/dev/null || true

# Configure for cross-compilation (minimal build - no UBI, no tests)
# mtd-utils requires autotools
if [ ! -f configure ]; then
    echo "Running autoreconf..."
    ./autogen.sh
fi

# Configure with cross-compilation, disable optional features for minimal build
CC="${CROSS_COMPILE}gcc" \
AR="${CROSS_COMPILE}ar" \
RANLIB="${CROSS_COMPILE}ranlib" \
STRIP="${CROSS_COMPILE}strip" \
./configure \
    --host=mips-lexra-linux-musl \
    --prefix="${SCRIPT_DIR}/mtd-install" \
    --without-ubifs \
    --without-lzo \
    --without-zstd \
    --without-xattr \
    --without-selinux \
    --without-crypto \
    CFLAGS="-Os -static" \
    LDFLAGS="-static"

# Build only flash_erase
make flash_erase

# Copy binary (built in root of mtd-utils directory)
cp flash_erase "${BIN_DIR}/"
${CROSS_COMPILE}strip "${BIN_DIR}/flash_erase"
echo "flash_erase built (cross-compiled MIPS Lexra, static)"
echo ""

echo "========================================="
echo "  BUILD COMPLETE"
echo "========================================="
echo ""
echo "Native tools (x86_64):"
ls -lh "${BIN_DIR}"/cvimg
ls -lh "${BIN_DIR}"/lzma
echo ""
echo "Cross-compiled tools (MIPS Lexra):"
ls -lh "${BIN_DIR}"/flash_erase
file "${BIN_DIR}"/flash_erase
echo ""
echo "Note: lzma-loader/loader.bin will be built during kernel compilation"
echo ""
