#!/bin/bash
# build_toolchain.sh — Build Lexra MIPS toolchain with crosstool-ng and Musl 1.2.5
#
# This script automates the complete toolchain build process including:
#   - Patch deployment to ~/.crosstool-ng/
#   - Toolchain compilation
#
# Output: ${PROJECT_ROOT}/x-tools/mips-lexra-linux-musl/
#
# J. Nilo - November 2025

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Determine project root (parent of 1-Build-Environment)
if [ -n "$PROJECT_ROOT" ]; then
    # Use environment variable if set
    :
else
    # Auto-detect: go up from 10-lexra-toolchain -> 1-Build-Environment -> project root
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
fi

TOOLCHAIN_PREFIX="${PROJECT_ROOT}/x-tools/mips-lexra-linux-musl"

echo "========================================="
echo "  LEXRA MIPS TOOLCHAIN BUILD"
echo "========================================="
echo ""
echo "Project root: ${PROJECT_ROOT}"
echo "Toolchain will be installed to: ${TOOLCHAIN_PREFIX}"
echo ""

# Check if crosstool-ng is installed
if ! command -v ct-ng >/dev/null 2>&1; then
    echo "ERROR: crosstool-ng not found in PATH"
    echo ""
    echo "Run install_deps.sh first to install all dependencies"
    echo "including crosstool-ng."
    echo ""
    exit 1
fi

echo "crosstool-ng found: $(ct-ng version)"
echo ""

# Check if toolchain already exists
if [ -d "$TOOLCHAIN_PREFIX" ]; then
    echo "Toolchain already exists at: $TOOLCHAIN_PREFIX"
    echo "Skipping build."
    exit 0
fi

# Deploy Lexra patches to crosstool-ng patches directory
echo "Deploying Lexra patches to ~/.crosstool-ng/..."
mkdir -p ~/.crosstool-ng/
cp -f -a "${SCRIPT_DIR}/patches" ~/.crosstool-ng/
echo "Patches deployed"
echo ""

# Create temporary build directory
BUILD_DIR=$(mktemp -d)
trap "rm -rf $BUILD_DIR" EXIT

cd "$BUILD_DIR"
echo "Build directory: $BUILD_DIR"
echo ""

# Generate config with correct prefix path
echo "Generating configuration..."
sed "s|CT_PREFIX_DIR=.*|CT_PREFIX_DIR=\"${TOOLCHAIN_PREFIX}\"|" \
    "${SCRIPT_DIR}/crosstool-ng.config" > .config

# Disable tarball saving to avoid ~/src warning
sed -i 's/CT_SAVE_TARBALLS=y/# CT_SAVE_TARBALLS is not set/' .config
sed -i 's/CT_LOCAL_TARBALLS_DIR=.*/CT_LOCAL_TARBALLS_DIR=""/' .config

echo "Configuration loaded"
echo ""

# Show configuration summary
echo "========================================="
echo "  TOOLCHAIN CONFIGURATION"
echo "========================================="
ct-ng show-config 2>/dev/null | grep -E "(Target|Vendor|OS|Kernel|C library|GCC|Binutils)" || true
echo "========================================="
echo ""

# Build toolchain
echo ""
echo "Building toolchain..."
echo "This will take approximately 30 minutes..."
echo ""

ct-ng build

echo ""
echo "========================================="
echo "  BUILD COMPLETE!"
echo "========================================="
echo ""
echo "Toolchain installed to: $TOOLCHAIN_PREFIX"
echo ""
echo "Add to your ~/.bashrc:"
echo "  export PATH=\"$TOOLCHAIN_PREFIX/bin:\$PATH\""
echo ""
echo "Verify installation:"
echo "  mips-lexra-linux-musl-gcc --version"
echo ""
