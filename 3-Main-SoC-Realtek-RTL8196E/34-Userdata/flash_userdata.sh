#!/bin/bash
# flash_userdata.sh — Flash userdata partition via TFTP
#
# Prerequisites:
#   - Gateway in bootloader mode (192.168.1.6)
#   - userdata.bin built (run ./build_userdata.sh first)
#
# J. Nilo - December 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GATEWAY_IP="${1:-192.168.1.6}"

cd "${SCRIPT_DIR}"

# Check that image exists
if [ ! -f "userdata.bin" ]; then
    echo "❌ userdata.bin not found. Build it first:"
    echo "   ./build_userdata.sh"
    exit 1
fi

echo "========================================="
echo "  FLASH USERDATA PARTITION"
echo "========================================="
echo ""
echo "Image: userdata.bin ($(ls -lh userdata.bin | awk '{print $5}'))"
echo "Target: ${GATEWAY_IP}"
echo ""

echo -n "Upload userdata.bin via TFTP to ${GATEWAY_IP}? [y/N] "
read -r UPLOAD

if [ "$UPLOAD" = "y" ] || [ "$UPLOAD" = "Y" ]; then
    echo "📤 Uploading via TFTP..."
    if tftp -m binary "${GATEWAY_IP}" -c put userdata.bin 2>&1; then
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
    echo "   tftp -m binary ${GATEWAY_IP} -c put userdata.bin"
fi
