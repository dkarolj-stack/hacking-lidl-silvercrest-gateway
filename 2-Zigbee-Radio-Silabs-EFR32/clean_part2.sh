#!/bin/bash
# clean_part2.sh — Clean build artifacts in 2-Zigbee-Radio-Silabs-EFR32
#
# Usage:
#   ./clean_part2.sh                      # Clean all (default)
#   ./clean_part2.sh all                  # Clean all
#   ./clean_part2.sh bootloader           # Clean bootloader only
#   ./clean_part2.sh ncp                  # Clean NCP firmware only
#   ./clean_part2.sh bootloader ncp       # Clean bootloader + NCP
#
# This script removes:
#   - Build directories (build/)
#   - Generated firmware files (firmware/*.s37, *.gbl, *.bin, *.hex)
#
# This script KEEPS:
#   - Build scripts (build_*.sh)
#   - Configuration files
#   - Patches
#   - Documentation (README.md, media/)
#   - Project structure
#
# J. Nilo - December 2025

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

# Default values
CLEAN_BOOTLOADER=0
CLEAN_NCP=0

# Parse arguments
if [ $# -eq 0 ]; then
    CLEAN_BOOTLOADER=1
    CLEAN_NCP=1
else
    for arg in "$@"; do
        case "$arg" in
            all)
                CLEAN_BOOTLOADER=1
                CLEAN_NCP=1
                ;;
            bootloader)
                CLEAN_BOOTLOADER=1
                ;;
            ncp)
                CLEAN_NCP=1
                ;;
            --help|-h)
                echo "Usage: $0 [target...]"
                echo ""
                echo "Targets:"
                echo "  all        Clean everything (default)"
                echo "  bootloader Clean bootloader build artifacts"
                echo "  ncp        Clean NCP firmware build artifacts"
                echo ""
                echo "Examples:"
                echo "  $0                     # Clean all"
                echo "  $0 bootloader          # Clean bootloader only"
                echo "  $0 ncp                 # Clean NCP only"
                exit 0
                ;;
            *)
                echo "Unknown target: $arg"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
fi

echo "========================================="
echo "  CLEANING BUILD ARTIFACTS"
echo "========================================="
echo ""

# Show what will be cleaned
echo "Targets:"
[ $CLEAN_BOOTLOADER -eq 1 ] && echo "  - bootloader (23-Bootloader-UART-Xmodem)"
[ $CLEAN_NCP -eq 1 ] && echo "  - ncp (24-NCP-UART-HW)"
echo ""

# =========================================
# Clean Bootloader
# =========================================
if [ $CLEAN_BOOTLOADER -eq 1 ]; then
    echo "Cleaning bootloader..."
    BOOTLOADER_DIR="${PROJECT_ROOT}/23-Bootloader-UART-Xmodem"

    # Remove build directory
    rm -rf "${BOOTLOADER_DIR}/build"

    # Remove generated firmware files (keep README.md)
    rm -f "${BOOTLOADER_DIR}/firmware/"*.s37
    rm -f "${BOOTLOADER_DIR}/firmware/"*.gbl
    rm -f "${BOOTLOADER_DIR}/firmware/"*.bin
    rm -f "${BOOTLOADER_DIR}/firmware/"*.hex

    echo "   Done: build/ and firmware binaries removed"
fi

# =========================================
# Clean NCP
# =========================================
if [ $CLEAN_NCP -eq 1 ]; then
    echo "Cleaning NCP..."
    NCP_DIR="${PROJECT_ROOT}/24-NCP-UART-HW"

    # Remove build directory
    rm -rf "${NCP_DIR}/build"

    # Remove generated firmware files (keep README.md)
    rm -f "${NCP_DIR}/firmware/"*.s37
    rm -f "${NCP_DIR}/firmware/"*.gbl
    rm -f "${NCP_DIR}/firmware/"*.bin
    rm -f "${NCP_DIR}/firmware/"*.hex

    echo "   Done: build/ and firmware binaries removed"
fi

echo ""
echo "========================================="
echo "  CLEANUP COMPLETE"
echo "========================================="
echo ""
echo "Kept:"
echo "  - Build scripts (build_*.sh)"
echo "  - Patches"
echo "  - Documentation (README.md, media/)"
echo "  - Project structure"
echo ""
