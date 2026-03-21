#!/bin/bash
# clean_part1.sh — Clean build artifacts in 1-Build-Environment
#
# Usage:
#   ./clean_part1.sh                  # Clean all (default)
#   ./clean_part1.sh all              # Clean all
#   ./clean_part1.sh tools            # Clean realtek-tools only
#   ./clean_part1.sh toolchain        # Clean lexra toolchain (<project>/x-tools)
#   ./clean_part1.sh tools toolchain  # Clean both
#
# This script removes:
#   - Compiled binaries (bin/)
#   - Downloaded sources (mtd-utils/)
#   - Build artifacts (*.o, .libs/, etc.)
#
# This script KEEPS:
#   - Build scripts (build_*.sh, install_*.sh)
#   - Configuration files (.config, crosstool-ng.config)
#   - Patches and source code
#   - Project structure
#
# J. Nilo - December 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Project root is 1 level up: 1-Build-Environment -> project root
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Default values
CLEAN_TOOLS=0
CLEAN_TOOLCHAIN=0

# Parse arguments
if [ $# -eq 0 ]; then
    CLEAN_TOOLS=1
    # Don't clean toolchain by default (takes 30+ min to rebuild)
else
    for arg in "$@"; do
        case "$arg" in
            all)
                CLEAN_TOOLS=1
                CLEAN_TOOLCHAIN=1
                ;;
            tools)
                CLEAN_TOOLS=1
                ;;
            toolchain)
                CLEAN_TOOLCHAIN=1
                ;;
            --help|-h)
                echo "Usage: $0 [target...]"
                echo ""
                echo "Targets:"
                echo "  all        Clean everything including toolchain"
                echo "  tools      Clean realtek-tools build artifacts (default)"
                echo "  toolchain  Clean lexra toolchain (~30 min to rebuild!)"
                echo ""
                echo "Examples:"
                echo "  $0                     # Clean tools only"
                echo "  $0 tools               # Clean tools only"
                echo "  $0 all                 # Clean everything"
                echo "  $0 tools toolchain     # Clean both"
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
[ $CLEAN_TOOLS -eq 1 ] && echo "  - tools (realtek-tools)"
[ $CLEAN_TOOLCHAIN -eq 1 ] && echo "  - toolchain (lexra ~30 min rebuild)"
echo ""

# =========================================
# Clean realtek-tools
# =========================================
if [ $CLEAN_TOOLS -eq 1 ]; then
    echo "Cleaning realtek-tools..."

    # Remove compiled binaries
    rm -rf "${SCRIPT_DIR}/11-realtek-tools/bin"

    # Remove downloaded mtd-utils
    rm -rf "${SCRIPT_DIR}/11-realtek-tools/mtd-utils"

    # Clean cvimg build artifacts
    if [ -d "${SCRIPT_DIR}/11-realtek-tools/cvimg" ]; then
        cd "${SCRIPT_DIR}/11-realtek-tools/cvimg"
        make clean 2>/dev/null || true
    fi

    # Clean lzma build artifacts
    if [ -d "${SCRIPT_DIR}/11-realtek-tools/lzma-4.65/CPP/7zip/Compress/LZMA_Alone" ]; then
        cd "${SCRIPT_DIR}/11-realtek-tools/lzma-4.65/CPP/7zip/Compress/LZMA_Alone"
        make -f makefile.gcc clean 2>/dev/null || true
    fi

    # Clean lzma-loader build artifacts
    if [ -d "${SCRIPT_DIR}/11-realtek-tools/lzma-loader" ]; then
        cd "${SCRIPT_DIR}/11-realtek-tools/lzma-loader"
        make clean 2>/dev/null || true
    fi

    echo "   Done: binaries, mtd-utils, and build artifacts removed"
fi

# =========================================
# Clean lexra toolchain
# =========================================
if [ $CLEAN_TOOLCHAIN -eq 1 ]; then
    TOOLCHAIN_PATH="${PROJECT_ROOT}/x-tools/mips-lexra-linux-musl"

    if [ -d "$TOOLCHAIN_PATH" ]; then
        echo "Cleaning lexra toolchain..."
        echo "   WARNING: This will require ~30 minutes to rebuild!"
        read -p "   Continue? [y/N] " -n 1 -r
        echo ""
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "$TOOLCHAIN_PATH"
            echo "   Done: $TOOLCHAIN_PATH removed"
        else
            echo "   Skipped: toolchain not removed"
        fi
    else
        echo "Toolchain not found at $TOOLCHAIN_PATH (already clean)"
    fi
fi

cd "${SCRIPT_DIR}"

echo ""
echo "========================================="
echo "  CLEANUP COMPLETE"
echo "========================================="
echo ""
echo "Kept:"
echo "  - Build scripts (build_*.sh, install_*.sh)"
echo "  - Configuration files (.config, crosstool-ng.config)"
echo "  - Patches and source code"
echo "  - Project structure"
echo ""
