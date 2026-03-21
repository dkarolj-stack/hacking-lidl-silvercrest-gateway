#!/bin/bash
# clean_part3.sh — Clean build artifacts in 3-Main-SoC-Realtek-RTL8196E
#
# Usage:
#   ./clean_part3.sh                  # Clean all (default)
#   ./clean_part3.sh all              # Clean all
#   ./clean_part3.sh kernel           # Clean kernel only
#   ./clean_part3.sh rootfs           # Clean rootfs only
#   ./clean_part3.sh userdata         # Clean userdata only
#   ./clean_part3.sh rootfs userdata  # Clean rootfs + userdata
#
# This script removes:
#   - Downloaded source directories
#   - Compiled binaries
#   - Build artifacts (*.o, .libs/, etc.)
#   - Generated images
#
# This script KEEPS:
#   - Build scripts (build_*.sh, flash_*.sh)
#   - Configuration files (.config, localoptions.h, *.txt)
#   - Patches and new files
#   - Project structure
#   - Local source code
#
# J. Nilo - December 2025

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

# Default values
CLEAN_KERNEL=0
CLEAN_ROOTFS=0
CLEAN_USERDATA=0

# Parse arguments
if [ $# -eq 0 ]; then
    CLEAN_KERNEL=1
    CLEAN_ROOTFS=1
    CLEAN_USERDATA=1
else
    for arg in "$@"; do
        case "$arg" in
            all)
                CLEAN_KERNEL=1
                CLEAN_ROOTFS=1
                CLEAN_USERDATA=1
                ;;
            kernel)
                CLEAN_KERNEL=1
                ;;
            rootfs)
                CLEAN_ROOTFS=1
                ;;
            userdata)
                CLEAN_USERDATA=1
                ;;
            --help|-h)
                echo "Usage: $0 [target...]"
                echo ""
                echo "Targets:"
                echo "  all        Clean everything (default)"
                echo "  kernel     Clean kernel build artifacts"
                echo "  rootfs     Clean rootfs build artifacts"
                echo "  userdata   Clean userdata build artifacts"
                echo ""
                echo "Examples:"
                echo "  $0                     # Clean all"
                echo "  $0 kernel              # Clean kernel only"
                echo "  $0 rootfs userdata     # Clean rootfs + userdata"
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
[ $CLEAN_KERNEL -eq 1 ] && echo "  • kernel"
[ $CLEAN_ROOTFS -eq 1 ] && echo "  • rootfs"
[ $CLEAN_USERDATA -eq 1 ] && echo "  • userdata"
echo ""

# =========================================
# Clean kernel
# =========================================
if [ $CLEAN_KERNEL -eq 1 ]; then
    echo "🧹 Cleaning kernel..."
    rm -rf "${PROJECT_ROOT}/32-Kernel/linux-5.10.246-rtl8196e"
    echo "   ✓ Patched kernel sources removed (kernel.img kept)"
fi

# =========================================
# Clean rootfs
# =========================================
if [ $CLEAN_ROOTFS -eq 1 ]; then
    echo "🧹 Cleaning rootfs..."
    # Downloaded sources
    rm -rf "${PROJECT_ROOT}/33-Rootfs/busybox/busybox-"*
    rm -rf "${PROJECT_ROOT}/33-Rootfs/dropbear/DROPBEAR_"*
    # Installed binaries (ELF only, keep shell scripts)
    find "${PROJECT_ROOT}/33-Rootfs/skeleton/bin" -type f -executable -exec sh -c 'file "$1" | grep -q "ELF" && rm "$1"' _ {} \; 2>/dev/null || true
    find "${PROJECT_ROOT}/33-Rootfs/skeleton/sbin" -type f -executable -exec sh -c 'file "$1" | grep -q "ELF" && rm "$1"' _ {} \; 2>/dev/null || true
    # Images
    rm -f "${PROJECT_ROOT}/33-Rootfs/rootfs.bin"
    echo "   ✓ Rootfs sources, binaries and images removed"
fi

# =========================================
# Clean userdata
# =========================================
if [ $CLEAN_USERDATA -eq 1 ]; then
    echo "🧹 Cleaning userdata..."
    # Downloaded sources
    rm -rf "${PROJECT_ROOT}/34-Userdata/nano/nano-"*
    rm -rf "${PROJECT_ROOT}/34-Userdata/nano/ncurses-"*
    rm -rf "${PROJECT_ROOT}/34-Userdata/nano/ncursesw-install"
    rm -f "${PROJECT_ROOT}/34-Userdata/serialgateway/src/serialgateway"
    # Installed binaries
    find "${PROJECT_ROOT}/34-Userdata/skeleton/usr/bin" -type f -executable -exec sh -c 'file "$1" | grep -q "ELF" && rm "$1"' _ {} \; 2>/dev/null || true
    # Images
    rm -f "${PROJECT_ROOT}/34-Userdata/userdata.bin"
    echo "   ✓ Userdata sources, binaries and images removed"
fi

echo ""
echo "========================================="
echo "  ✅ CLEANUP COMPLETE"
echo "========================================="
echo ""
echo "Kept:"
echo "  • Build scripts (build_*.sh, flash_*.sh)"
echo "  • Configuration files (.config, localoptions.h, config-*.txt)"
echo "  • Patches and source files"
echo "  • Project structure"
echo ""
