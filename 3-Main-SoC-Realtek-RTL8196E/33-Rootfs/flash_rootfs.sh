#!/bin/bash
# flash_rootfs.sh — Flash rootfs partition via TFTP
#
# Prerequisites:
#   - Gateway in bootloader mode (192.168.1.6)
#   - rootfs.bin built (run ./build_rootfs.sh first)
#
# J. Nilo - December 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GATEWAY_IP="${1:-192.168.1.6}"

cd "${SCRIPT_DIR}"

# Check that image exists
if [ ! -f "rootfs.bin" ]; then
    echo "❌ rootfs.bin not found. Build it first:"
    echo "   ./build_rootfs.sh"
    exit 1
fi

echo "========================================="
echo "  FLASH ROOTFS PARTITION"
echo "========================================="
echo ""
echo "Image: rootfs.bin ($(ls -lh rootfs.bin | awk '{print $5}'))"
echo "Target: ${GATEWAY_IP}"
echo ""

echo -n "Upload rootfs.bin via TFTP to ${GATEWAY_IP}? [y/N] "
read -r UPLOAD

if [ "$UPLOAD" = "y" ] || [ "$UPLOAD" = "Y" ]; then
    echo "📤 Uploading via TFTP..."
    if tftp -m binary "${GATEWAY_IP}" -c put rootfs.bin 2>&1; then
        echo "✅ Upload completed"
        echo ""
        echo "⚠️  Gateway remains in boot mode"
        echo "   Type 'boot' in bootloader console to start"
    else
        echo "❌ TFTP upload failed"
        exit 1
    fi
else
    echo "⏭️  Upload cancelled. To flash manually:"
    echo "   tftp -m binary ${GATEWAY_IP} -c put rootfs.bin"
fi
