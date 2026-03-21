#!/bin/bash
# flash_rtl8196e.sh — Flash partitions via TFTP
#
# Usage:
#   ./flash_rtl8196e.sh                       # Flash all partitions
#   ./flash_rtl8196e.sh all                   # Flash all partitions
#   ./flash_rtl8196e.sh kernel                # Flash kernel only
#   ./flash_rtl8196e.sh rootfs                # Flash rootfs only
#   ./flash_rtl8196e.sh userdata              # Flash userdata only
#   ./flash_rtl8196e.sh rootfs userdata       # Flash rootfs + userdata
#   ./flash_rtl8196e.sh --ip 192.168.1.10     # Flash all to different IP
#   ./flash_rtl8196e.sh rootfs --ip 192.168.1.10  # Flash rootfs to different IP
#
# Flashing order (when flashing multiple):
#   1. rootfs.bin    - Root filesystem (SquashFS)
#   2. userdata.bin  - User data partition (JFFS2)
#   3. kernel.img    - Linux kernel (triggers automatic reboot)
#
# Prerequisites:
#   - Gateway connected via serial (38400 8N1)
#   - Gateway in bootloader mode (showing "<RealTek>" prompt)
#   - Network connection to gateway (default: 192.168.1.6)
#
# J. Nilo - December 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Default values
GATEWAY_IP="192.168.1.6"
FLASH_KERNEL=0
FLASH_ROOTFS=0
FLASH_USERDATA=0

# Parse arguments
if [ $# -eq 0 ]; then
    FLASH_KERNEL=1
    FLASH_ROOTFS=1
    FLASH_USERDATA=1
else
    while [ $# -gt 0 ]; do
        case "$1" in
            all)
                FLASH_KERNEL=1
                FLASH_ROOTFS=1
                FLASH_USERDATA=1
                ;;
            kernel)
                FLASH_KERNEL=1
                ;;
            rootfs)
                FLASH_ROOTFS=1
                ;;
            userdata)
                FLASH_USERDATA=1
                ;;
            --ip)
                shift
                GATEWAY_IP="$1"
                ;;
            --help|-h)
                echo "Usage: $0 [target...] [--ip ADDRESS]"
                echo ""
                echo "Targets:"
                echo "  all        Flash everything (default)"
                echo "  kernel     Flash Linux kernel"
                echo "  rootfs     Flash root filesystem"
                echo "  userdata   Flash user partition"
                echo ""
                echo "Options:"
                echo "  --ip ADDR  Gateway IP address (default: 192.168.1.6)"
                echo ""
                echo "Examples:"
                echo "  $0                          # Flash all"
                echo "  $0 kernel                   # Flash kernel only"
                echo "  $0 rootfs userdata          # Flash rootfs + userdata"
                echo "  $0 rootfs --ip 192.168.1.10 # Flash rootfs to different IP"
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
        shift
    done
fi

# Image locations
KERNEL_IMG="${SCRIPT_DIR}/32-Kernel/kernel.img"
ROOTFS_IMG="${SCRIPT_DIR}/33-Rootfs/rootfs.bin"
USERDATA_IMG="${SCRIPT_DIR}/34-Userdata/userdata.bin"

echo "========================================="
echo "  FLASH RTL8196E PARTITIONS"
echo "========================================="
echo ""

# Show what will be flashed
echo "Targets:"
[ $FLASH_ROOTFS -eq 1 ] && echo "  • rootfs"
[ $FLASH_USERDATA -eq 1 ] && echo "  • userdata"
[ $FLASH_KERNEL -eq 1 ] && echo "  • kernel"
echo ""
echo "Gateway IP: ${GATEWAY_IP}"
echo ""

# Check required images exist
MISSING=0
echo "Checking images..."

if [ $FLASH_ROOTFS -eq 1 ]; then
    if [ -f "$ROOTFS_IMG" ]; then
        echo "  ✅ rootfs.bin   ($(ls -lh "$ROOTFS_IMG" | awk '{print $5}'))"
    else
        echo "  ❌ rootfs.bin   NOT FOUND"
        MISSING=1
    fi
fi

if [ $FLASH_USERDATA -eq 1 ]; then
    if [ -f "$USERDATA_IMG" ]; then
        echo "  ✅ userdata.bin ($(ls -lh "$USERDATA_IMG" | awk '{print $5}'))"
    else
        echo "  ❌ userdata.bin NOT FOUND"
        MISSING=1
    fi
fi

if [ $FLASH_KERNEL -eq 1 ]; then
    if [ -f "$KERNEL_IMG" ]; then
        echo "  ✅ kernel.img   ($(ls -lh "$KERNEL_IMG" | awk '{print $5}'))"
    else
        echo "  ❌ kernel.img   NOT FOUND"
        MISSING=1
    fi
fi

if [ $MISSING -eq 1 ]; then
    echo ""
    echo "❌ Required image(s) missing. Build first:"
    echo "   ./build_rtl8196e.sh"
    exit 1
fi

echo ""
echo "========================================="
echo "  ⚠️  PREREQUISITES CHECK"
echo "========================================="
echo ""
echo "Before continuing, make sure:"
echo ""
echo "  1. Serial connection established:"
echo "     • Port: /dev/ttyUSB0 or /dev/ttyS0"
echo "     • Settings: 38400 8N1"
echo ""
echo "  2. Gateway is in bootloader mode:"
echo "     • You see the \"<RealTek>\" prompt"
echo "     • If not, power cycle and press ESC during boot"
echo ""
echo "  3. Network is configured:"
echo "     • Your PC has IP on 192.168.1.x subnet"
echo "     • Gateway responds at ${GATEWAY_IP}"
echo ""

echo -n "Are you connected to the gateway and see the <RealTek> prompt? [y/N] "
read -r CONFIRM

if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
    echo ""
    echo "⏭️  Aborted. Connect to the gateway first."
    echo ""
    echo "Example with minicom:"
    echo "   minicom -D /dev/ttyUSB0 -b 38400"
    echo ""
    echo "Example with screen:"
    echo "   screen /dev/ttyUSB0 38400"
    exit 0
fi

echo ""
if [ $FLASH_KERNEL -eq 1 ]; then
    echo "⚠️  Flashing kernel will trigger automatic reboot!"
    echo ""
fi

echo -n "Start flashing? [y/N] "
read -r START

if [ "$START" != "y" ] && [ "$START" != "Y" ]; then
    echo ""
    echo "⏭️  Flashing cancelled."
    exit 0
fi

# =========================================
# Flash rootfs (if selected)
# =========================================
if [ $FLASH_ROOTFS -eq 1 ]; then
    echo ""
    echo "========================================="
    echo "  FLASHING ROOTFS"
    echo "========================================="
    echo ""
    echo "📤 Uploading rootfs.bin via TFTP..."

    cd "${SCRIPT_DIR}/33-Rootfs"
    if ! tftp -m binary "${GATEWAY_IP}" -c put rootfs.bin 2>&1; then
        echo ""
        echo "❌ TFTP upload failed for rootfs.bin"
        exit 1
    fi
    cd "${SCRIPT_DIR}"

    echo "✅ rootfs.bin uploaded successfully"
    echo ""
    echo "⏳ Waiting for flash to complete..."
    echo "   Watch serial console for: \"Flash Write Successed!\""
    echo "   Then the <RealTek> prompt will reappear."
    echo ""

    echo -n "Do you see \"Flash Write Successed!\" and <RealTek> prompt? [y/N] "
    read -r ROOTFS_DONE

    if [ "$ROOTFS_DONE" != "y" ] && [ "$ROOTFS_DONE" != "Y" ]; then
        echo ""
        echo "⏭️  Aborted. Wait for flash to complete and retry."
        exit 1
    fi
fi

# =========================================
# Flash userdata (if selected)
# =========================================
if [ $FLASH_USERDATA -eq 1 ]; then
    echo ""
    echo "========================================="
    echo "  FLASHING USERDATA (be patient: 1-2 min)"
    echo "========================================="
    echo ""
    echo "📤 Uploading userdata.bin via TFTP..."

    cd "${SCRIPT_DIR}/34-Userdata"
    if ! tftp -m binary "${GATEWAY_IP}" -c put userdata.bin 2>&1; then
        echo ""
        echo "❌ TFTP upload failed for userdata.bin"
        exit 1
    fi
    cd "${SCRIPT_DIR}"

    echo "✅ userdata.bin uploaded successfully"
    echo ""
    echo "⏳ Waiting for flash to complete..."
    echo "   Watch serial console for: \"Flash Write Successed!\""
    echo "   Then the <RealTek> prompt will reappear."
    echo ""

    echo -n "Do you see \"Flash Write Successed!\" and <RealTek> prompt? [y/N] "
    read -r USERDATA_DONE

    if [ "$USERDATA_DONE" != "y" ] && [ "$USERDATA_DONE" != "Y" ]; then
        echo ""
        echo "⏭️  Aborted. Wait for flash to complete and retry."
        exit 1
    fi
fi

# =========================================
# Flash kernel (if selected - triggers reboot)
# =========================================
if [ $FLASH_KERNEL -eq 1 ]; then
    echo ""
    echo "========================================="
    echo "  FLASHING KERNEL"
    echo "========================================="
    echo ""
    echo "⚠️  WARNING: This will trigger an automatic reboot!"
    echo ""
    echo -n "Ready to flash kernel and reboot? [y/N] "
    read -r KERNEL_CONFIRM

    if [ "$KERNEL_CONFIRM" != "y" ] && [ "$KERNEL_CONFIRM" != "Y" ]; then
        echo ""
        echo "⏭️  Kernel flash cancelled."
        echo "   To flash kernel manually: ./flash_rtl8196e.sh kernel"
        exit 0
    fi

    echo ""
    echo "📤 Uploading kernel.img via TFTP..."

    cd "${SCRIPT_DIR}/32-Kernel"
    if ! tftp -m binary "${GATEWAY_IP}" -c put kernel.img 2>&1; then
        echo ""
        echo "❌ TFTP upload failed for kernel.img"
        exit 1
    fi
    cd "${SCRIPT_DIR}"

    echo ""
    echo "========================================="
    echo "  ✅ KERNEL FLASHED - REBOOTING"
    echo "========================================="
    echo ""
    echo "🔄 Gateway is rebooting with the new system..."
    echo ""
    echo "Watch the serial console for boot messages."
    echo "The new system should start in ~30 seconds."
    echo ""
    exit 0
fi

# If we get here, we flashed userdata and/or rootfs but not kernel
echo ""
echo "========================================="
echo "  ✅ FLASH COMPLETE"
echo "========================================="
echo ""
echo "To boot the gateway:"
echo "  • Type in the <RealTek> console: J 80c00000"
echo "  • Or hard reboot: unplug/replug VCC on serial adapter"
echo ""
