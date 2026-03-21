#!/bin/bash
# flash_kernel.sh — Flash kernel partition via TFTP
#
# Prerequisites:
#   - Gateway in bootloader mode (192.168.1.6)
#   - kernel.img built (run ./build_kernel.sh first)
#
# WARNING: Flashing kernel triggers automatic reboot!
#
# J. Nilo - December 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GATEWAY_IP="${1:-192.168.1.6}"

cd "${SCRIPT_DIR}"

# Check that image exists
if [ ! -f "kernel.img" ]; then
    echo "❌ kernel.img not found. Build it first:"
    echo "   ./build_kernel.sh"
    exit 1
fi

echo "========================================="
echo "  FLASH KERNEL PARTITION"
echo "========================================="
echo ""
echo "Image: kernel.img ($(ls -lh kernel.img | awk '{print $5}'))"
echo "Target: ${GATEWAY_IP}"
echo ""
echo "⚠️  WARNING: Flashing kernel will trigger automatic reboot!"
echo ""

echo -n "Upload kernel.img via TFTP to ${GATEWAY_IP}? [y/N] "
read -r UPLOAD

if [ "$UPLOAD" = "y" ] || [ "$UPLOAD" = "Y" ]; then
    echo ""
    echo "📤 Uploading kernel.img via TFTP..."
    if tftp -m binary "${GATEWAY_IP}" -c put kernel.img 2>&1; then
        echo ""
        echo "✅ Kernel uploaded successfully!"
        echo ""
        echo "🔄 Gateway will reboot automatically with new kernel"
    else
        echo ""
        echo "❌ TFTP upload failed"
        exit 1
    fi
else
    echo ""
    echo "⏭️  Upload cancelled. To flash manually:"
    echo "   tftp -m binary ${GATEWAY_IP} -c put kernel.img"
fi
