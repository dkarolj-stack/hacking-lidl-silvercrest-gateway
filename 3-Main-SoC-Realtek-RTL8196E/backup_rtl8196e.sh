#!/bin/bash
# backup_rtl8196e.sh — Dump/backup partitions via TFTP from bootloader
#
# Usage:
#   ./backup_rtl8196e.sh                    # Dump all partitions
#   ./backup_rtl8196e.sh all                # Dump all partitions
#   ./backup_rtl8196e.sh mtd0               # Dump mtd0 (boot+cfg) only
#   ./backup_rtl8196e.sh mtd1 mtd2          # Dump mtd1 + mtd2
#   ./backup_rtl8196e.sh mtd0 mtd1 mtd2 mtd3 mtd4  # Dump specific partitions
#
# How it works:
#   Uses the FLR command in the Realtek bootloader to read flash to RAM,
#   then fetches the data via TFTP. This is faster than other dump methods.
#
#   From bootloader:
#     flr 80000000 <hex_offset> <hex_length>
#   Then on host:
#     tftp 192.168.1.6 -c get test <local_file>
#
# Network setup:
#   - Host IP: any address on 192.168.1.x subnet
#   - Gateway IP: 192.168.1.6 (default in bootloader)
#   - Connect via UART (38400 8N1) AND Ethernet
#
# Prerequisites:
#   - tftp-hpa installed (sudo apt install tftp-hpa)
#   - Gateway in bootloader mode (<RealTek> prompt)
#   - Network connection to gateway
#
# Note: Large partitions (>4MB) are automatically split into 4MB chunks
#       due to RTL8196E RAM limitations, then reassembled.
#
# J. Nilo - December 2025

# =============================================================================
# CONFIGURATION - Change LAYOUT to "new" if using the new partition scheme
# =============================================================================
LAYOUT="lidl"    # "lidl" = Lidl/Tuya original layout (5 partitions)
                 # "new"  = New layout (4 partitions)

# Maximum chunk size for FLR command (RTL8196E has 32MB RAM but limited usable space)
MAX_CHUNK_SIZE=$((4 * 1024 * 1024))  # 4MB = 0x400000

# Partition layouts:
#
#   LIDL/TUYA ORIGINAL (16MB flash):
#   mtd0: 00020000 "boot+cfg"    offset 0x000000
#   mtd1: 001e0000 "linux"       offset 0x020000
#   mtd2: 00200000 "rootfs"      offset 0x200000
#   mtd3: 00020000 "tuya-label"  offset 0x400000
#   mtd4: 00be0000 "jffs2-fs"    offset 0x420000
#
#   NEW LAYOUT (16MB flash):
#   mtd0: 00020000 "boot+cfg"    offset 0x000000
#   mtd1: 001e0000 "linux"       offset 0x020000
#   mtd2: 00200000 "rootfs"      offset 0x200000
#   mtd3: 00c00000 "jffs2-fs"    offset 0x400000
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Default values
GATEWAY_IP="192.168.1.6"
OUTPUT_DIR="${SCRIPT_DIR}/backup"

# Partition definitions - LIDL/Tuya original layout (5 partitions)
LIDL_MTD0_NAME="boot+cfg"
LIDL_MTD0_OFFSET="0"
LIDL_MTD0_SIZE="20000"

LIDL_MTD1_NAME="linux"
LIDL_MTD1_OFFSET="20000"
LIDL_MTD1_SIZE="1e0000"

LIDL_MTD2_NAME="rootfs"
LIDL_MTD2_OFFSET="200000"
LIDL_MTD2_SIZE="200000"

LIDL_MTD3_NAME="tuya-label"
LIDL_MTD3_OFFSET="400000"
LIDL_MTD3_SIZE="20000"

LIDL_MTD4_NAME="jffs2-fs"
LIDL_MTD4_OFFSET="420000"
LIDL_MTD4_SIZE="be0000"

LIDL_MTD_COUNT=5

# Partition definitions - NEW layout (4 partitions)
NEW_MTD0_NAME="boot+cfg"
NEW_MTD0_OFFSET="0"
NEW_MTD0_SIZE="20000"

NEW_MTD1_NAME="linux"
NEW_MTD1_OFFSET="20000"
NEW_MTD1_SIZE="1e0000"

NEW_MTD2_NAME="rootfs"
NEW_MTD2_OFFSET="200000"
NEW_MTD2_SIZE="200000"

NEW_MTD3_NAME="jffs2-fs"
NEW_MTD3_OFFSET="400000"
NEW_MTD3_SIZE="c00000"

NEW_MTD_COUNT=4

# Get partition info based on layout
get_mtd_info() {
    local mtd_num="$1"
    local field="$2"  # NAME, OFFSET, SIZE

    if [ "$LAYOUT" = "new" ]; then
        eval "echo \${NEW_MTD${mtd_num}_${field}}"
    else
        eval "echo \${LIDL_MTD${mtd_num}_${field}}"
    fi
}

get_mtd_count() {
    if [ "$LAYOUT" = "new" ]; then
        echo "$NEW_MTD_COUNT"
    else
        echo "$LIDL_MTD_COUNT"
    fi
}

# Parse arguments
DUMP_LIST=""

show_help() {
    local mtd_count
    mtd_count=$(get_mtd_count)
    echo "Usage: $0 [mtdX...] [options]"
    echo ""
    echo "Partitions (layout: ${LAYOUT}):"
    for i in $(seq 0 $((mtd_count - 1))); do
        local name offset size
        name=$(get_mtd_info "$i" NAME)
        offset=$(get_mtd_info "$i" OFFSET)
        size=$(get_mtd_info "$i" SIZE)
        printf "  mtd%d  %-12s  offset 0x%s  size 0x%s\n" "$i" "$name" "$offset" "$size"
    done
    echo ""
    echo "Options:"
    echo "  all              Dump all partitions (default)"
    echo "  mtd0, mtd1, ...  Dump specific partition(s)"
    echo "  --output DIR     Output directory (default: ./backup)"
    echo "  --ip ADDR        Gateway IP address (default: 192.168.1.6)"
    echo ""
    echo "Examples:"
    echo "  $0                    # Dump all partitions"
    echo "  $0 mtd1 mtd2          # Dump linux + rootfs"
    echo "  $0 all --output /tmp  # Dump all to /tmp"
    echo ""
    echo "Note: Partitions larger than 4MB are automatically split into chunks."
    echo ""
    echo "To change layout, edit LAYOUT variable at top of script."
    echo "Current layout: ${LAYOUT}"
    exit 0
}

if [ $# -eq 0 ]; then
    DUMP_LIST="all"
else
    while [ $# -gt 0 ]; do
        case "$1" in
            all)
                DUMP_LIST="all"
                ;;
            mtd[0-9])
                DUMP_LIST="${DUMP_LIST} ${1#mtd}"
                ;;
            --output|-o)
                shift
                OUTPUT_DIR="$1"
                ;;
            --ip)
                shift
                GATEWAY_IP="$1"
                ;;
            --help|-h)
                show_help
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

# If "all", build list of all mtd numbers
if [ "$DUMP_LIST" = "all" ]; then
    mtd_count=$(get_mtd_count)
    DUMP_LIST=""
    for i in $(seq 0 $((mtd_count - 1))); do
        DUMP_LIST="${DUMP_LIST} $i"
    done
fi

# Validate mtd numbers
mtd_count=$(get_mtd_count)
for mtd_num in $DUMP_LIST; do
    if [ "$mtd_num" -ge "$mtd_count" ]; then
        echo "Error: mtd${mtd_num} does not exist in '${LAYOUT}' layout (max: mtd$((mtd_count - 1)))"
        exit 1
    fi
done

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check for tftp (tftp-hpa)
if ! command -v tftp &> /dev/null; then
    echo "Error: tftp is not installed"
    echo "Install it with: sudo apt install tftp-hpa"
    exit 1
fi

echo "========================================="
echo "  BACKUP RTL8196E PARTITIONS"
echo "========================================="
echo ""
echo "Layout: ${LAYOUT}"
echo "Gateway IP: ${GATEWAY_IP}"
echo "Output: ${OUTPUT_DIR}"
echo ""

# Show what will be dumped
echo "Partitions to dump:"
for mtd_num in $DUMP_LIST; do
    part_name=$(get_mtd_info "$mtd_num" NAME)
    part_offset=$(get_mtd_info "$mtd_num" OFFSET)
    part_size=$(get_mtd_info "$mtd_num" SIZE)
    part_size_dec=$((16#${part_size}))
    if [ "$part_size_dec" -gt "$MAX_CHUNK_SIZE" ]; then
        num_chunks=$(( (part_size_dec + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE ))
        printf "  mtd%d  %-12s  (0x%s, 0x%s) [%d chunks]\n" "$mtd_num" "$part_name" "$part_offset" "$part_size" "$num_chunks"
    else
        printf "  mtd%d  %-12s  (0x%s, 0x%s)\n" "$mtd_num" "$part_name" "$part_offset" "$part_size"
    fi
done
echo ""

echo "========================================="
echo "  PREREQUISITES CHECK"
echo "========================================="
echo ""
echo "Before continuing, verify:"
echo ""
echo "  1. Your host has an IP on the 192.168.1.x subnet"
echo ""
echo "  2. Gateway connected via UART (38400 8N1) and Ethernet"
echo ""
echo "  3. Gateway in bootloader mode (<RealTek> prompt)"
echo ""

echo -n "Ready to proceed? [y/N] "
read -r CONFIRM

if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
    echo "Aborted."
    exit 0
fi

# Function to fetch a single chunk via TFTP
fetch_chunk() {
    local outfile="$1"
    local expected_size="$2"

    if tftp "${GATEWAY_IP}" -m binary -c get test "${outfile}"; then
        local actual_size
        actual_size=$(stat -c%s "${outfile}" 2>/dev/null || echo "0")

        if [ "$actual_size" -eq "$expected_size" ]; then
            echo "OK: $(basename "${outfile}") (${actual_size} bytes)"
            return 0
        else
            echo "WARNING: Size mismatch!"
            echo "  Expected: ${expected_size} bytes"
            echo "  Got:      ${actual_size} bytes"
            return 1
        fi
    else
        echo "ERROR: TFTP fetch failed"
        return 1
    fi
}

# Function to dump a partition (with chunking if needed)
dump_partition() {
    local mtd_num="$1"
    local name offset_hex size_hex
    name=$(get_mtd_info "$mtd_num" NAME)
    offset_hex=$(get_mtd_info "$mtd_num" OFFSET)
    size_hex=$(get_mtd_info "$mtd_num" SIZE)

    local offset_dec size_dec
    offset_dec=$((16#${offset_hex}))
    size_dec=$((16#${size_hex}))

    local outfile="mtd${mtd_num}_${name}.bin"

    echo ""
    echo "========================================="
    echo "  DUMPING: mtd${mtd_num} (${name})"
    echo "========================================="
    echo ""
    echo "Offset: 0x${offset_hex}"
    echo "Size:   0x${size_hex} (${size_dec} bytes)"
    echo "Output: ${outfile}"

    # Check if we need to chunk
    if [ "$size_dec" -le "$MAX_CHUNK_SIZE" ]; then
        # Single chunk - simple case
        echo ""
        echo ">>> Enter this command in the <RealTek> bootloader:"
        echo ""
        echo "    flr 80000000 ${offset_hex} ${size_hex}"
        echo ""

        read -r -p ">>> Press ENTER after running flr command... "

        echo ""
        echo "Fetching data via TFTP..."

        rm -f "${OUTPUT_DIR}/${outfile}"

        if fetch_chunk "${OUTPUT_DIR}/${outfile}" "$size_dec"; then
            return 0
        else
            return 1
        fi
    else
        # Multiple chunks needed
        local num_chunks=$(( (size_dec + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE ))
        echo ""
        echo "Partition too large for single transfer."
        echo "Will dump in ${num_chunks} chunks of max 4MB each."

        local chunk_files=""
        local current_offset=$offset_dec
        local remaining=$size_dec
        local chunk_num=1
        local chunk_errors=0

        while [ "$remaining" -gt 0 ]; do
            local chunk_size=$MAX_CHUNK_SIZE
            if [ "$remaining" -lt "$MAX_CHUNK_SIZE" ]; then
                chunk_size=$remaining
            fi

            local chunk_offset_hex=$(printf "%x" $current_offset)
            local chunk_size_hex=$(printf "%x" $chunk_size)
            local chunk_file="${OUTPUT_DIR}/mtd${mtd_num}_${name}_chunk${chunk_num}.bin"

            echo ""
            echo "--- Chunk ${chunk_num}/${num_chunks} ---"
            echo "Offset: 0x${chunk_offset_hex}"
            echo "Size:   0x${chunk_size_hex} (${chunk_size} bytes)"
            echo ""
            echo ">>> Enter this command in the <RealTek> bootloader:"
            echo ""
            echo "    flr 80000000 ${chunk_offset_hex} ${chunk_size_hex}"
            echo ""

            read -r -p ">>> Press ENTER after running flr command... "

            echo ""
            echo "Fetching chunk ${chunk_num} via TFTP..."

            rm -f "${chunk_file}"

            if fetch_chunk "${chunk_file}" "$chunk_size"; then
                chunk_files="${chunk_files} ${chunk_file}"
            else
                chunk_errors=$((chunk_errors + 1))
            fi

            current_offset=$((current_offset + chunk_size))
            remaining=$((remaining - chunk_size))
            chunk_num=$((chunk_num + 1))
        done

        # Concatenate chunks into final file
        if [ "$chunk_errors" -eq 0 ]; then
            echo ""
            echo "Concatenating ${num_chunks} chunks into ${outfile}..."
            rm -f "${OUTPUT_DIR}/${outfile}"
            cat ${chunk_files} > "${OUTPUT_DIR}/${outfile}"

            local final_size
            final_size=$(stat -c%s "${OUTPUT_DIR}/${outfile}" 2>/dev/null || echo "0")

            if [ "$final_size" -eq "$size_dec" ]; then
                echo "OK: ${outfile} (${final_size} bytes)"
                # Clean up chunk files
                rm -f ${chunk_files}
                return 0
            else
                echo "ERROR: Final size mismatch!"
                echo "  Expected: ${size_dec} bytes"
                echo "  Got:      ${final_size} bytes"
                echo "  Chunk files preserved for debugging."
                return 1
            fi
        else
            echo ""
            echo "ERROR: ${chunk_errors} chunk(s) failed to download."
            echo "  Chunk files preserved for debugging."
            return 1
        fi
    fi
}

# Dump selected partitions
ERRORS=0
TOTAL=0

for mtd_num in $DUMP_LIST; do
    TOTAL=$((TOTAL + 1))
    if ! dump_partition "$mtd_num"; then
        ERRORS=$((ERRORS + 1))
    fi
done

echo ""
echo "========================================="
if [ $ERRORS -eq 0 ]; then
    echo "  BACKUP COMPLETE (${TOTAL} partitions)"
else
    echo "  BACKUP COMPLETED WITH ${ERRORS}/${TOTAL} ERROR(S)"
fi
echo "========================================="
echo ""
echo "Files saved to: ${OUTPUT_DIR}/"
ls -lh "${OUTPUT_DIR}/" 2>/dev/null || echo "(directory empty or not accessible)"
echo ""
