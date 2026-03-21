#!/bin/bash
# build_rootfs.sh — Build SquashFS root filesystem for RTL8196E
#
# This script:
#   - Creates a SquashFS image from skeleton/ directory
#   - Adds device nodes (console, null, zero)
#   - Converts to RTL bootloader format with cvimg
#
# Output: rootfs.bin (ready to flash)
#
# J. Nilo - November 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/.."

# Check that fakeroot is installed
if ! command -v fakeroot >/dev/null 2>&1; then
    echo "❌ fakeroot is not installed"
    echo "   Installation: sudo apt-get install fakeroot"
    exit 1
fi

# Check that cvimg is built
BUILD_ENV="${PROJECT_ROOT}/../1-Build-Environment/11-realtek-tools"
CVIMG_TOOL="${BUILD_ENV}/bin/cvimg"
if [ ! -f "$CVIMG_TOOL" ]; then
    echo "❌ cvimg not found. Build realtek-tools first:"
    echo "   cd ${BUILD_ENV} && ./build_tools.sh"
    exit 1
fi

cd "${SCRIPT_DIR}"

echo "========================================="
echo "  BUILDING ROOT FILESYSTEM"
echo "========================================="
echo ""

# Ensure /dev directory exists in skeleton
echo "🔧 Preparing /dev structure..."
mkdir -p skeleton/dev

# Clean old images
rm -f rootfs.sqfs rootfs.bin

echo "📦 Generating SquashFS with device nodes..."
fakeroot mksquashfs skeleton rootfs.sqfs \
  -nopad -noappend -all-root \
  -comp xz -b 256k \
  -p "/dev/console c 600 0 0 5 1" \
  -p "/dev/null c 666 0 0 1 3" \
  -p "/dev/zero c 666 0 0 1 5"

echo ""
echo "🔍 Verifying device nodes in image..."
if unsquashfs -ll rootfs.sqfs 2>/dev/null | grep -q "dev/console"; then
    echo "✅ /dev/console found in rootfs"
    unsquashfs -ll rootfs.sqfs 2>/dev/null | grep "dev/" | head -10
else
    echo "⚠️  /dev/console NOT found in rootfs"
fi

echo ""
echo "🔧 Converting to RTL format..."
$CVIMG_TOOL \
    -i rootfs.sqfs \
    -o rootfs.bin \
    -e 0x80c00000 \
    -b 0x200000 \
    -s r6cr

# Remove intermediate file
rm -f rootfs.sqfs

echo ""
echo "========================================="
echo "  BUILD SUMMARY"
echo "========================================="
ls -lh rootfs.bin

echo ""
echo "Rootfs image ready: rootfs.bin ($(ls -lh rootfs.bin | awk '{print $5}'))"
echo ""
echo "To flash: ./flash_rootfs.sh"
