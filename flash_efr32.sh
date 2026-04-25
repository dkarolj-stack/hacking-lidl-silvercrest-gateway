#!/bin/bash
# flash_efr32.sh — Flash firmware to the Silabs EFR32 Zigbee/Thread radio
#
# Flow (v3.0+, requires kernel 6.18 with rtl8196e-uart-bridge):
#   1. Presents a menu to select the firmware type (NCP, RCP, OT-RCP, Router)
#   2. Ensures universal-silabs-flasher is available (installs in venv if needed)
#   3. SSHes into the gateway to detect mode (Zigbee vs OTBR via radio.conf):
#      - Zigbee: bridge already armed by S50uart_bridge; drop flow_control=0.
#      - OTBR  : bridge intentionally disarmed (otbr-agent owns ttyS1). Stop
#                S70otbr cleanly, self-arm the bridge at the OT-RCP baud
#                with flow_control=1 (Spinel/HDLC at 460800 needs RTS/CTS).
#      Then stop radio daemons; the bridge stays armed on TCP:8888 throughout.
#   4. Flashes the selected firmware over socket://GW:8888. When USF
#      transitions the EFR32 into the Gecko Bootloader, drop flow_control
#      to 0 (Gecko prefers XON/XOFF) and switch baud to 115200.
#   5. Restores flow_control=1 and reboots the gateway
#
# Kernel bridge sysfs (5 writable params, all under /sys/module/
# rtl8196e_uart_bridge/parameters/):
#   baud         — UART baud rate
#   flow_control — 1 = CRTSCTS on (normal); 0 = off (Gecko Bootloader Xmodem)
#   enable       — 1 = armed, 0 = disarmed
#   (tty, port, bind_addr are set at boot and not touched here)
#
# Baud rate: Pre-built firmware runs at 115200 (NCP, RCP, Router) or 460800
# (OT-RCP). Power users may run up to 892857. This script infers the current
# baud from radio.conf and the bridge's sysfs state, then tries that baud
# first. If detection fails, it scans known bauds via sysfs (instant — no
# process restart needed).
#
# Dependencies:
#   universal-silabs-flasher 1.0.3 (pinned — patch depends on this version)
#
# Usage: ./flash_efr32.sh [GATEWAY_IP]
#   GATEWAY_IP - Gateway IP address (default: 192.168.1.88)
#
# Environment variables (optional, for non-interactive use):
#   FW_CHOICE  - Firmware to flash: 1=Bootloader, 2=NCP (default), 3=RCP,
#                4=OT-RCP, 5=Z3-Router
#   CONFIRM    - Set to "y" to skip the "Flash?" prompt
#
# Examples:
#   ./flash_efr32.sh                          # Interactive menu
#   FW_CHOICE=2 CONFIRM=y ./flash_efr32.sh    # Flash NCP non-interactively
#   FW_CHOICE=4 CONFIRM=y ./flash_efr32.sh    # Flash OT-RCP non-interactively
#
# J. Nilo - February 2026, kernel-bridge rewrite April 2026

set -euo pipefail

# Check that python3 and venv are available (needed for universal-silabs-flasher)
if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 not found." >&2
    echo "Install it with: sudo apt install python3" >&2
    exit 1
fi
if ! python3 -c "import venv" 2>/dev/null; then
    echo "Error: python3-venv not found." >&2
    echo "Install it with: sudo apt install python3-venv" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GW_IP="${1:-192.168.1.88}"
GW_PORT=8888
VENV_DIR="${SCRIPT_DIR}/silabs-flasher"

SSH_OPTS="-n -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new"
SSH="ssh $SSH_OPTS root@${GW_IP}"
SSH_RETRIES=3

BRIDGE_SYSFS="/sys/module/rtl8196e_uart_bridge/parameters"

# Wait for the bridge TCP port to accept connections.
# With the kernel bridge this should be immediate (never drops during flow
# control or baud changes), but we keep the wait as a safety net.
wait_for_port() {
    local host="$1" port="$2" timeout="${3:-5}"
    local deadline=$((SECONDS + timeout))
    while [ $SECONDS -lt $deadline ]; do
        if timeout 1 bash -c "echo >/dev/tcp/$host/$port" 2>/dev/null; then
            return 0
        fi
        sleep 0.5
    done
    return 1
}

FW_DIR="${SCRIPT_DIR}/2-Zigbee-Radio-Silabs-EFR32"

# --- Firmware table --------------------------------------------------------

FW_BTL="${FW_DIR}/23-Bootloader-UART-Xmodem/firmware/bootloader-uart-xmodem-2.4.2.gbl"
FW_NCP="${FW_DIR}/24-NCP-UART-HW/firmware/ncp-uart-hw-7.5.1.gbl"
FW_RCP="${FW_DIR}/25-RCP-UART-HW/firmware/rcp-uart-802154.gbl"
FW_OT_RCP="${FW_DIR}/26-OT-RCP/firmware/ot-rcp.gbl"
FW_ROUTER="${FW_DIR}/27-Router/firmware/z3-router-7.5.1.gbl"

# --- Firmware selection menu -----------------------------------------------

if [ -n "${FW_CHOICE:-}" ]; then
    fw_choice="$FW_CHOICE"
else
    echo "EFR32 Firmware Flasher"
    echo ""
    echo "  [1] Bootloader    — Gecko Bootloader stage 2 (UART/Xmodem)   ($(basename "$FW_BTL"))"
    echo "  [2] NCP-UART-HW   — Zigbee NCP for zigbee2mqtt / ZHA         ($(basename "$FW_NCP"))"
    echo "  [3] RCP-UART-HW   — Multi-PAN RCP for zigbee2mqtt            ($(basename "$FW_RCP"))"
    echo "  [4] OT-RCP        — OpenThread RCP for otbr-agent            ($(basename "$FW_OT_RCP"))"
    echo "  [5] Z3-Router     — Zigbee 3.0 standalone router             ($(basename "$FW_ROUTER"))"
    echo ""
    read -r -p "Firmware to flash [2]: " fw_choice
    fw_choice="${fw_choice:-2}"
fi

case "$fw_choice" in
    1) FIRMWARE="$FW_BTL" ;;
    2) FIRMWARE="$FW_NCP" ;;
    3) FIRMWARE="$FW_RCP" ;;
    4) FIRMWARE="$FW_OT_RCP" ;;
    5) FIRMWARE="$FW_ROUTER" ;;
    *) echo "Invalid choice."; exit 1 ;;
esac

# --- Preflight -------------------------------------------------------------

if [ ! -f "$FIRMWARE" ]; then
    echo "Error: firmware not found: $FIRMWARE" >&2
    exit 1
fi

echo ""
echo "Firmware: $(basename "$FIRMWARE")"
echo "Gateway:  ${GW_IP}:${GW_PORT}"
echo ""

# --- 1. Check / install universal-silabs-flasher ---------------------------

PATCH_FILE="$SCRIPT_DIR/silabs-flasher-probe-methods.patch"
PATCH_HASH_FILE="${VENV_DIR}/.patch-hash"

# Reinstall if probe-methods patch has changed since last install
if [ -x "${VENV_DIR}/bin/universal-silabs-flasher" ] && [ -f "$PATCH_FILE" ]; then
    current_hash=$(md5sum "$PATCH_FILE" 2>/dev/null | awk '{print $1}')
    applied_hash=$(cat "$PATCH_HASH_FILE" 2>/dev/null || true)
    if [ "$current_hash" != "$applied_hash" ]; then
        echo "Probe methods patch changed — reinstalling USF..."
        rm -rf "$VENV_DIR"
    fi
fi

if [ -x "${VENV_DIR}/bin/universal-silabs-flasher" ]; then
    FLASHER="${VENV_DIR}/bin/universal-silabs-flasher"
    echo "universal-silabs-flasher: venv (${VENV_DIR})"
elif command -v universal-silabs-flasher >/dev/null 2>&1; then
    FLASHER="universal-silabs-flasher"
    echo "universal-silabs-flasher: $(command -v universal-silabs-flasher)"
else
    echo "universal-silabs-flasher not found — installing in ${VENV_DIR}..."
    python3 -m venv "$VENV_DIR"
    "${VENV_DIR}/bin/pip" install --quiet universal-silabs-flasher==1.0.3
    FLASHER="${VENV_DIR}/bin/universal-silabs-flasher"
    # Patch USF to probe Spinel/EZSP at 115200/230400 (upstream only probes
    # Spinel at 460800 and EZSP at 115200/460800 — misses our common bauds)
    USF_CONST=$(find "$VENV_DIR" -path '*/universal_silabs_flasher/const.py' -print -quit)
    if [ -n "$USF_CONST" ] && [ -f "$PATCH_FILE" ] && \
       patch --dry-run -f "$USF_CONST" "$PATCH_FILE" >/dev/null 2>&1; then
        patch -f "$USF_CONST" "$PATCH_FILE" >/dev/null
        md5sum "$PATCH_FILE" | awk '{print $1}' > "$PATCH_HASH_FILE"
        echo "Installed (patched probe methods)."
    else
        echo "Installed."
    fi
fi
echo ""

# --- 2. SSH: verify bridge, detect baud, switch to flash mode --------------
# The in-kernel UART bridge exposes sysfs knobs; we change baud and
# flow_control without disarming the bridge.  TCP:8888 stays up across
# all operations below.

echo "Connecting to ${GW_IP} — detecting configuration..."
for i in $(seq 1 "$SSH_RETRIES"); do
    if DETECT_OUT=$($SSH "
        # Require the in-kernel UART bridge — this script does not support
        # the legacy userspace serialgateway (removed in v3.0).
        if [ ! -d '$BRIDGE_SYSFS' ]; then
            echo 'error no-bridge'
            exit 0
        fi

        # Mode is the source of truth for who owns ttyS1:
        #   Zigbee : S50uart_bridge arms the bridge at boot
        #   OTBR   : S50 skips, otbr-agent (started by S70otbr) holds ttyS1
        # In OTBR the bridge stays disarmed by design — we self-arm here so
        # the flash can run without the user manually wrestling with init
        # scripts (issue #86).
        MODE=zigbee
        BRIDGE_BAUD_CFG=
        if [ -f /userdata/etc/radio.conf ]; then
            grep -q '^MODE=otbr' /userdata/etc/radio.conf && MODE=otbr
            BRIDGE_BAUD_CFG=\$(grep '^BRIDGE_BAUD=' /userdata/etc/radio.conf | cut -d= -f2)
        fi

        ARMED=\$(cat '$BRIDGE_SYSFS/armed' 2>/dev/null || echo 0)

        if [ \"\$ARMED\" != '1' ]; then
            if [ \"\$MODE\" = 'otbr' ]; then
                # Stop S70otbr cleanly: the init script's stop case handles
                # the LED + 30-s sync daemon trap and final dataset flush
                # to /userdata/thread, which a bare killall otbr-agent skips.
                /userdata/etc/init.d/S70otbr stop >/dev/null 2>&1 || \
                    killall otbr-agent 2>/dev/null
                sleep 1
                # Arm the bridge at the OT-RCP firmware's baud, with HW flow
                # control on — Spinel/HDLC at 460800 needs RTS/CTS to probe
                # reliably. flow_control gets dropped to 0 only when USF
                # transitions the EFR32 into the Gecko Bootloader.
                BAUD=\${BRIDGE_BAUD_CFG:-460800}
                echo \"\$BAUD\" > '$BRIDGE_SYSFS/baud'
                echo 1 > '$BRIDGE_SYSFS/flow_control'
                echo 1 > '$BRIDGE_SYSFS/enable'
                sleep 1
                if [ \"\$(cat '$BRIDGE_SYSFS/armed' 2>/dev/null)\" != '1' ]; then
                    echo 'error self-arm-failed'
                    exit 0
                fi
                echo \"otbr \$BAUD self-armed\"
                exit 0
            fi
            echo 'error not-armed'
            exit 0
        fi

        # Already armed (Zigbee path): read current baud — authoritative for
        # what the EFR32 currently sees.
        BAUD=\$(cat '$BRIDGE_SYSFS/baud' 2>/dev/null || echo 115200)
        echo \"\$MODE \$BAUD\"
    " 2>/dev/null); then
        break
    fi
    if [ "$i" -eq "$SSH_RETRIES" ]; then
        echo "Error: cannot reach gateway after $SSH_RETRIES attempts." >&2
        exit 1
    fi
    echo "SSH timeout — retrying ($((i+1))/$SSH_RETRIES)..."
done

case "$DETECT_OUT" in
    "error no-bridge")
        echo "Error: in-kernel UART bridge not found on ${GW_IP}." >&2
        echo "This script requires kernel 6.18 with CONFIG_RTL8196E_UART_BRIDGE=y." >&2
        exit 1
        ;;
    "error not-armed")
        echo "Error: UART bridge is not armed on ${GW_IP}." >&2
        echo "Check S50uart_bridge init script; or arm manually:" >&2
        echo "  echo 1 > ${BRIDGE_SYSFS}/enable" >&2
        exit 1
        ;;
    "error self-arm-failed")
        echo "Error: failed to self-arm the bridge on ${GW_IP} (OTBR mode)." >&2
        echo "Check that S70otbr stopped cleanly; or arm manually:" >&2
        echo "  /userdata/etc/init.d/S70otbr stop" >&2
        echo "  echo 460800 > ${BRIDGE_SYSFS}/baud" >&2
        echo "  echo 1     > ${BRIDGE_SYSFS}/flow_control" >&2
        echo "  echo 1     > ${BRIDGE_SYSFS}/enable" >&2
        exit 1
        ;;
esac

# Detect whether we self-armed (OTBR path) so we can tell the user.
SELF_ARMED=
case "$DETECT_OUT" in
    *" self-armed")
        SELF_ARMED=1
        DETECT_OUT="${DETECT_OUT% self-armed}"
        ;;
esac

RADIO_MODE="${DETECT_OUT%% *}"
CURRENT_BAUD="${DETECT_OUT##* }"
CURRENT_BAUD="${CURRENT_BAUD:-115200}"
RADIO_MODE="${RADIO_MODE:-zigbee}"
if [ -n "$SELF_ARMED" ]; then
    echo "Detected: ${RADIO_MODE} @ ${CURRENT_BAUD} baud (bridge self-armed; S70otbr stopped)"
else
    echo "Detected: ${RADIO_MODE} @ ${CURRENT_BAUD} baud (bridge armed)"
fi

# Remember the original baud so we can restore it at cleanup (in case the
# flash fails halfway — the bridge would otherwise be left at 115200 and
# any zigbeed/otbr-agent trying to restart would talk at the wrong speed).
ORIG_BAUD="$CURRENT_BAUD"

# Switch bridge to flash mode: stop daemons, optionally disable RTS/CTS.
# The bridge stays armed — TCP:8888 never drops.
#
# Flow control policy:
#   Zigbee path (NCP/RCP): drop flow_control=0 right away. EZSP/CPC probes
#     succeed without RTS/CTS at the application baud, and Gecko Bootloader
#     prefers no HW flow control once we transition there.
#   OTBR path (Spinel/HDLC): keep flow_control=1 during the probe — at
#     460800 the EFR32 OT-RCP firmware demands RTS/CTS, dropping it loses
#     the probe response. flow_control=0 is set lower down, only after USF
#     has put the EFR32 into Gecko Bootloader.
if [ "$RADIO_MODE" = "otbr" ]; then
    echo "Switching bridge to flash mode (flow_control=1, OTBR path)..."
else
    echo "Switching bridge to flash mode (flow_control=0)..."
fi
$SSH "
    killall otbr-agent 2>/dev/null || true
    killall cpcd 2>/dev/null || true
    killall zigbeed 2>/dev/null || true
    # Stop LED PWM timer (interferes with UART during Xmodem transfer)
    echo 0 > /sys/class/leds/status/brightness 2>/dev/null || true
    if [ '${RADIO_MODE}' != 'otbr' ]; then
        # Disable hardware flow control — Gecko Bootloader uses XON/XOFF.
        echo 0 > ${BRIDGE_SYSFS}/flow_control
    fi
    sleep 1
"

# Restore bridge to normal mode + reboot on every exit path.
cleanup() {
    # Restore the original baud + flow_control, then reboot.
    $SSH "
        echo ${ORIG_BAUD} > ${BRIDGE_SYSFS}/baud 2>/dev/null || true
        echo 1 > ${BRIDGE_SYSFS}/flow_control 2>/dev/null || true
        reboot
    " 2>/dev/null || true
}
trap cleanup EXIT

if ! wait_for_port "$GW_IP" "$GW_PORT"; then
    echo "Error: bridge not reachable on ${GW_IP}:${GW_PORT}." >&2
    exit 1
fi
if [ "$RADIO_MODE" = "otbr" ]; then
    echo "Bridge ready at ${CURRENT_BAUD} baud, flow_control=1 (OTBR path)."
else
    echo "Bridge ready at ${CURRENT_BAUD} baud, flow_control=0."
fi
echo ""

# --- 3. Flash ---------------------------------------------------------------

if [ "${CONFIRM:-}" != "y" ]; then
    read -r -p "Flash $(basename "$FIRMWARE") to ${GW_IP}? [y/N] " confirm
    if [[ ! "$confirm" =~ ^[yY]$ ]]; then
        echo "Aborted."
        exit 0
    fi
fi

echo ""
echo "Flashing..."

# Helper: change bridge baud (no process restart needed).
set_bridge_baud() {
    local baud="$1"
    $SSH "echo ${baud} > ${BRIDGE_SYSFS}/baud"
}

if [ "$FIRMWARE" = "$FW_BTL" ]; then
    # Bootloader flash: capture output to detect NoFirmwareError.
    # USF tries run_firmware() after upload, which fails because the
    # application slot is empty — the flash itself succeeded.
    FLASH_LOG=$(mktemp)
    trap 'rm -f "$FLASH_LOG"; cleanup' EXIT
    "$FLASHER" --device "socket://${GW_IP}:${GW_PORT}" flash --firmware "$FIRMWARE" 2>&1 | tee "$FLASH_LOG" && FLASH_RC=0 || FLASH_RC=$?

    if [ $FLASH_RC -ne 0 ] && grep -q "NoFirmwareError" "$FLASH_LOG"; then
        echo ""
        echo "Bootloader flashed successfully."
        echo "The application slot is now empty — select a firmware to flash now:"
        echo ""
        echo "  [2] NCP-UART-HW   — Zigbee NCP for zigbee2mqtt / ZHA         ($(basename "$FW_NCP"))"
        echo "  [3] RCP-UART-HW   — Multi-PAN RCP for zigbee2mqtt            ($(basename "$FW_RCP"))"
        echo "  [4] OT-RCP        — OpenThread RCP for otbr-agent            ($(basename "$FW_OT_RCP"))"
        echo "  [5] Z3-Router     — Zigbee 3.0 standalone router             ($(basename "$FW_ROUTER"))"
        echo ""
        read -r -p "Firmware to flash [2]: " fw_choice2
        fw_choice2="${fw_choice2:-2}"
        case "$fw_choice2" in
            2) FIRMWARE="$FW_NCP" ;;
            3) FIRMWARE="$FW_RCP" ;;
            4) FIRMWARE="$FW_OT_RCP" ;;
            5) FIRMWARE="$FW_ROUTER" ;;
            *) echo "Invalid choice."; exit 1 ;;
        esac
        echo ""
        echo "Flashing $(basename "$FIRMWARE")..."
        "$FLASHER" --device "socket://${GW_IP}:${GW_PORT}" flash --firmware "$FIRMWARE"
    elif [ $FLASH_RC -ne 0 ]; then
        echo ""
        echo "Flash failed."
        echo ""
        echo "Check that the bridge is armed in flash mode (flow_control=0)"
        echo "and the gateway is reachable on ${GW_IP}:${GW_PORT}."
        exit 1
    fi
else
    # Normal firmware flash: targeted probe at detected baud.
    # --probe-methods avoids USF's full default scan (~30s) and targets
    # the expected protocol based on radio.conf (~1-3s).
    if [ "$RADIO_MODE" = "otbr" ]; then
        # OT-RCP → Spinel only. Use CURRENT_BAUD (not hardcoded 460800):
        # a 2.1.6 migration may run OT-RCP at 115200 or any other baud,
        # and we self-armed at the radio.conf-derived value.
        PROBE="spinel:${CURRENT_BAUD}"
    else
        # NCP (EZSP) or RCP (CPC) — try both + bootloader
        PROBE="ezsp:${CURRENT_BAUD},cpc:${CURRENT_BAUD},bootloader:${CURRENT_BAUD}"
    fi
    FLASH_LOG=$(mktemp)
    if "$FLASHER" --device "socket://${GW_IP}:${GW_PORT}" \
            --probe-methods "$PROBE" \
            flash --firmware "$FIRMWARE" 2>&1 | tee "$FLASH_LOG"; then
        rm -f "$FLASH_LOG"
    elif grep -q "FailedToEnterBootloaderError" "$FLASH_LOG"; then
        # USF detected the firmware and sent enter_bootloader, but the EFR32
        # is now in Gecko Bootloader mode at 115200 while the bridge is
        # still at the application baud. Switch bridge to 115200 + drop
        # flow_control to 0 (Gecko prefers XON/XOFF; the OTBR path also
        # arrives here with flow_control=1 from the probe phase).
        rm -f "$FLASH_LOG"
        echo ""
        echo "Firmware detected — EFR32 entered bootloader. Switching bridge to 115200..."
        set_bridge_baud 115200
        $SSH "echo 0 > ${BRIDGE_SYSFS}/flow_control" 2>/dev/null || true
        if ! "$FLASHER" --device "socket://${GW_IP}:${GW_PORT}" \
                --probe-methods "bootloader:115200" \
                flash --firmware "$FIRMWARE"; then
            echo "Flash via bootloader failed."
            exit 1
        fi
    else
        rm -f "$FLASH_LOG"
        # Targeted probe failed. The firmware may be running at a different
        # baud than inferred. Scan all known bauds by changing the bridge's
        # baud param (instant, no process restart).
        echo ""
        echo "Targeted probe at ${CURRENT_BAUD} failed. Scanning other baud rates..."

        RECOVERED=false
        for BAUD in 115200 460800 892857 691200 230400; do
            # Don't skip CURRENT_BAUD: the targeted probe may have failed for
            # non-baud reasons (e.g. wrong MODE in radio.conf → wrong
            # --probe-methods set), and the comprehensive scan-baud probe
            # (ezsp+spinel+cpc) catches those at the same baud.

            echo "  Trying ${BAUD} baud..."
            set_bridge_baud "$BAUD"

            # Probe with retry: USF can crash with AssertionError on the
            # first attempt if the TCP connection is torn down mid-probe.
            # One retry is enough.
            PROBE="ezsp:${BAUD},spinel:${BAUD},cpc:${BAUD}"
            FLASH_OUT=""
            FLASH_RC=0
            for attempt in 1 2; do
                FLASH_RC=0
                FLASH_OUT=$("$FLASHER" --device "socket://${GW_IP}:${GW_PORT}" \
                    --probe-methods "$PROBE" \
                    flash --firmware "$FIRMWARE" 2>&1) || FLASH_RC=$?
                # If USF crashed (AssertionError / transport error), retry once
                if echo "$FLASH_OUT" | grep -q "AssertionError\|_transport"; then
                    [ "$attempt" -eq 1 ] && echo "    USF transport error — retrying..." && sleep 1 && continue
                fi
                break
            done

            # First USF call already flashed successfully (probe → enter_bootloader
            # → Xmodem → verify, all in one USF invocation). Don't invoke a second
            # redundant USF: the EFR32 has rebooted to the new app by now, so a
            # bootloader-only probe would race against the Gecko Bootloader timeout
            # and report "Flash failed" on a firmware that is actually fine.
            if [ "$FLASH_RC" -eq 0 ]; then
                echo "$FLASH_OUT" | grep "Detected" || true
                RECOVERED=true
                break
            fi

            # Detection worked but flashing failed — typically FailedToEnterBootloader
            # (EFR32 is now sitting in the Gecko Bootloader at 115200). Switch the
            # bridge and retry with the bootloader-only probe.
            if echo "$FLASH_OUT" | grep -q "Detected"; then
                echo "$FLASH_OUT" | grep "Detected"
                echo ""
                echo "Switching bridge to 115200 for Gecko Bootloader..."
                set_bridge_baud 115200
                # Same rationale as the targeted-probe path: drop flow_control
                # for Gecko Bootloader regardless of the probe-phase setting.
                $SSH "echo 0 > ${BRIDGE_SYSFS}/flow_control" 2>/dev/null || true

                echo "Flashing via Gecko Bootloader..."
                if "$FLASHER" --device "socket://${GW_IP}:${GW_PORT}" \
                    --probe-methods "bootloader:115200" \
                    flash --firmware "$FIRMWARE"; then
                    RECOVERED=true
                fi
                break
            fi
        done

        if [ "$RECOVERED" != "true" ]; then
            echo ""
            echo "Flash failed."
            echo ""
            echo "Could not detect firmware at any known baud rate"
            echo "(tried: 115200, 460800, 892857, 691200, 230400)."
            echo "You may need a J-Link/SWD debugger to recover."
            exit 1
        fi
    fi
fi

# --- 4. Configure radio mode + cleanup -------------------------------------

# Ensure radio.conf matches the flashed firmware so the correct daemon
# starts on reboot (otbr-agent for OT-RCP, bridge + nothing else otherwise).
#
# radio.conf is a multi-key file:
#   MODE=otbr           (optional) — drives the OTBR vs bridge path
#   BRIDGE_BAUD=<baud>  (optional) — read by S50uart_bridge at boot
# We therefore operate on the MODE= line only and never wipe the whole
# file: deleting it would lose BRIDGE_BAUD (or any future operator key)
# and silently revert the bridge to its compile-time default post-reboot.
case "$fw_choice" in
    4)  # OT-RCP → OTBR mode, UART default 460800
        MODE_LINE=MODE=otbr
        DAEMON_MSG="otbr-agent (S70otbr) at 460800 baud"
        ORIG_BAUD=460800
        ;;
    3)  # RCP-UART-HW → Zigbee via cpcd, UART default 460800
        MODE_LINE=
        DAEMON_MSG="in-kernel UART bridge on TCP:8888 at 460800 baud"
        ORIG_BAUD=460800
        ;;
    *)  # NCP, Router → Zigbee mode, UART default 115200
        MODE_LINE=
        DAEMON_MSG="in-kernel UART bridge on TCP:8888 at 115200 baud"
        ORIG_BAUD=115200
        ;;
esac

# Persist both MODE= and BRIDGE_BAUD= to /userdata/etc/radio.conf so
# S50uart_bridge arms the bridge at the right baud on next boot (and
# S70otbr knows whether to run). Fails quietly if SSH drops — the
# cleanup trap still sets the runtime baud, but the next reboot would
# reapply the stale persisted baud without this step.
$SSH "
    mkdir -p /userdata/etc
    touch /userdata/etc/radio.conf
    sed -i '/^MODE=/d;/^BRIDGE_BAUD=/d' /userdata/etc/radio.conf
    { [ -n '${MODE_LINE}' ] && echo '${MODE_LINE}'; echo 'BRIDGE_BAUD=${ORIG_BAUD}'; } >> /userdata/etc/radio.conf
" 2>/dev/null || true

echo ""
echo "Flash complete. Rebooting gateway..."
# cleanup() in the trap will restore baud + flow_control and reboot.

echo ""
echo "Done. Gateway rebooting — ${DAEMON_MSG} will start automatically."
