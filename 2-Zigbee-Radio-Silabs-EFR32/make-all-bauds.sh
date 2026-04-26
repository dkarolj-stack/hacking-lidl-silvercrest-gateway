#!/bin/bash
# make-all-bauds.sh — Build the v3.1 baud matrix of EFR32 firmwares
#
# Iterates over the firmware × baud combinations the project commits to
# supporting, calling each per-firmware build_*.sh with the chosen baud as
# a positional argument.
#
# Matrix (per CHANGELOG v3.0.0 max-tested values):
#   NCP-UART-HW : 115200, 230400, 460800, 691200, 892857
#   RCP-UART-HW : 115200, 230400, 460800           (cpcd POSIX cap)
#   OT-RCP      : 460800                            (otbr-agent ceiling)
#   Z3-Router   : 115200                            (text CLI only)
# Total: 10 GBLs.
#
# Output: <firmware-dir>/firmware/<base>-<BAUD>.gbl (and .s37)
#
# Usage:
#   ./make-all-bauds.sh              # Build everything missing (idempotent)
#   ./make-all-bauds.sh --force      # Rebuild everything from scratch
#   ./make-all-bauds.sh --list       # Print what would be built, exit
#   ./make-all-bauds.sh --help       # Show this help
#
# Power users wanting a single non-matrix variant should call the per-firmware
# build script directly:
#   ./24-NCP-UART-HW/build_ncp.sh 921600
#
# J. Nilo - April 2026

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ----- matrix -----
NCP_BAUDS="115200 230400 460800 691200 892857"
RCP_BAUDS="115200 230400 460800"
OT_RCP_BAUDS="460800"
ROUTER_BAUDS="115200"

# ----- arg parsing -----
FORCE=0
LIST_ONLY=0
case "${1:-}" in
    --force|-f) FORCE=1 ;;
    --list|-l)  LIST_ONLY=1 ;;
    --help|-h)
        sed -n '2,29p' "$0"
        exit 0
        ;;
    "")  ;;
    *)
        echo "Error: unknown argument '$1'. Use --help." >&2
        exit 1
        ;;
esac

# ----- helpers -----
# build <fw_dir> <build_script> <gbl_basename_template> <bauds...>
# gbl_basename_template uses %BAUD% as placeholder.
build_one() {
    local fw_dir="$1"
    local build_script="$2"
    local gbl_template="$3"
    shift 3
    local bauds="$*"

    for baud in $bauds; do
        local out="${SCRIPT_DIR}/${fw_dir}/firmware/${gbl_template//%BAUD%/$baud}.gbl"

        if [ "$LIST_ONLY" = "1" ]; then
            if [ -f "$out" ]; then
                echo "  [exists] $out"
            else
                echo "  [build ] $out"
            fi
            continue
        fi

        if [ "$FORCE" = "0" ] && [ -f "$out" ]; then
            echo "  [skip] $out (already built; use --force to rebuild)"
            continue
        fi

        echo
        echo "============================================================"
        echo "Building ${fw_dir} at ${baud} baud..."
        echo "============================================================"
        ( cd "${SCRIPT_DIR}/${fw_dir}" && ./"${build_script}" "${baud}" )
        if [ ! -f "$out" ]; then
            echo "ERROR: expected output $out not produced." >&2
            exit 1
        fi
        echo "  -> $out"
    done
}

# ----- detect EmberZNet version (NCP and Router filenames embed it) -----
EMBER_CONFIG=""
for c in "${PROJECT_ROOT:-${SCRIPT_DIR}/..}/silabs-tools/gecko_sdk/protocol/zigbee/stack/config/config.h" \
         "${SCRIPT_DIR}/../silabs-tools/gecko_sdk/protocol/zigbee/stack/config/config.h"; do
    [ -f "$c" ] && EMBER_CONFIG="$c" && break
done
if [ -n "$EMBER_CONFIG" ]; then
    EMBER_MAJOR=$(grep '#define EMBER_MAJOR_VERSION' "$EMBER_CONFIG" | awk '{print $3}')
    EMBER_MINOR=$(grep '#define EMBER_MINOR_VERSION' "$EMBER_CONFIG" | awk '{print $3}')
    EMBER_PATCH=$(grep '#define EMBER_PATCH_VERSION' "$EMBER_CONFIG" | awk '{print $3}')
    EMBERZNET_VERSION="${EMBER_MAJOR}.${EMBER_MINOR}.${EMBER_PATCH}"
else
    echo "WARNING: could not locate Gecko SDK to read EmberZNet version" >&2
    EMBERZNET_VERSION="unknown"
fi

# ----- run -----
if [ "$LIST_ONLY" = "1" ]; then
    echo "Matrix:"
fi

build_one "24-NCP-UART-HW"  "build_ncp.sh"     "ncp-uart-hw-${EMBERZNET_VERSION}-%BAUD%"   $NCP_BAUDS
build_one "25-RCP-UART-HW"  "build_rcp.sh"     "rcp-uart-802154-%BAUD%"                    $RCP_BAUDS
build_one "26-OT-RCP"       "build_ot_rcp.sh"  "ot-rcp-%BAUD%"                             $OT_RCP_BAUDS
build_one "27-Router"       "build_router.sh"  "z3-router-${EMBERZNET_VERSION}-%BAUD%"     $ROUTER_BAUDS

if [ "$LIST_ONLY" = "0" ]; then
    echo
    echo "============================================================"
    echo "Matrix complete. GBLs in <firmware>/firmware/."
    echo "============================================================"
fi
