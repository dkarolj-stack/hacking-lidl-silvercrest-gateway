#!/bin/sh
#
# measure_uart_overruns.sh — sample /proc/tty/driver/serial for the EFR32 link
#
# Runs on the gateway (busybox /bin/sh). Samples ttyS1 counters at a fixed
# interval and emits CSV on stdout. Prints delta-oe live to stderr so you can
# tell at a glance whether overruns are accumulating during a Zigbee stress.
#
# Usage:
#   measure_uart_overruns.sh [-i INTERVAL] [-d DURATION] [-p PORT] [-o CSV]
#     -i INTERVAL   seconds between samples (default 2)
#     -d DURATION   total seconds to run; 0 = until Ctrl-C (default 0)
#     -p PORT       tty line number in /proc/tty/driver/serial (default 1)
#     -o CSV        write CSV to this file (default stdout)
#
# Example (niveau 0 du plan de mesure — §7 du MEMO uart-bridge-kernel):
#   ./measure_uart_overruns.sh -i 2 -d 120 -o /tmp/baseline.csv
#   # pendant ce temps: pairer 3-4 devices, envoyer des commandes de groupe
#
# Decision rule:
#   delta_oe > 0 at 230400 under load  ->  userspace bridge already saturates.
#                                          460800 is lost. Build the kernel bridge.
#   delta_oe == 0                      ->  escalate to niveau 1 (rebuild RCP@460800).

set -u

INTERVAL=2
DURATION=0
PORT=1
OUT=-

while [ $# -gt 0 ]; do
    case "$1" in
        -i) INTERVAL=$2; shift 2 ;;
        -d) DURATION=$2; shift 2 ;;
        -p) PORT=$2; shift 2 ;;
        -o) OUT=$2; shift 2 ;;
        -h|--help)
            sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

PROC=/proc/tty/driver/serial
[ -r "$PROC" ] || { echo "cannot read $PROC (need root?)" >&2; exit 1; }

# Parse one line of /proc/tty/driver/serial for the requested port.
# Typical line:
#   1: uart:16550A mmio:0x18002100 irq:31 tx:12345 rx:6789 fe:0 pe:0 brk:0 oe:0 RTS|CTS
# Extract tx rx fe pe brk oe. Busybox awk is fine.
sample() {
    awk -v p="$PORT" '
        $1 == p":" {
            tx=rx=fe=pe=brk=oe=0
            for (i=2; i<=NF; i++) {
                n = index($i, ":")
                if (n == 0) continue
                k = substr($i, 1, n-1)
                v = substr($i, n+1)
                if (k == "tx")  tx  = v
                if (k == "rx")  rx  = v
                if (k == "fe")  fe  = v
                if (k == "pe")  pe  = v
                if (k == "brk") brk = v
                if (k == "oe")  oe  = v
            }
            print tx, rx, fe, pe, brk, oe
            exit 0
        }
    ' "$PROC"
}

first=$(sample)
if [ -z "$first" ]; then
    echo "port $PORT not found in $PROC" >&2
    exit 1
fi

write() {
    if [ "$OUT" = "-" ]; then
        printf '%s\n' "$1"
    else
        printf '%s\n' "$1" >> "$OUT"
    fi
}

[ "$OUT" != "-" ] && : > "$OUT"
write "timestamp,elapsed_s,tx,rx,fe,pe,brk,oe,d_tx,d_rx,d_oe"

t0=$(date +%s)
prev_tx=0; prev_rx=0; prev_oe=0
first_iter=1

trap 'echo >&2; echo "stopped." >&2; exit 0' INT TERM

while :; do
    now=$(date +%s)
    elapsed=$((now - t0))

    set -- $(sample)
    tx=$1; rx=$2; fe=$3; pe=$4; brk=$5; oe=$6

    if [ $first_iter -eq 1 ]; then
        d_tx=0; d_rx=0; d_oe=0
        first_iter=0
    else
        d_tx=$((tx - prev_tx))
        d_rx=$((rx - prev_rx))
        d_oe=$((oe - prev_oe))
    fi
    prev_tx=$tx; prev_rx=$rx; prev_oe=$oe

    ts=$(date '+%Y-%m-%dT%H:%M:%S')
    write "$ts,$elapsed,$tx,$rx,$fe,$pe,$brk,$oe,$d_tx,$d_rx,$d_oe"

    # Live feedback to stderr — highlight when overruns appear.
    if [ "$d_oe" -gt 0 ] 2>/dev/null; then
        printf '[%s] t=%4ds  rx=+%-6d tx=+%-6d  OE=+%d  (total oe=%d)\n' \
            "$ts" "$elapsed" "$d_rx" "$d_tx" "$d_oe" "$oe" >&2
    else
        printf '[%s] t=%4ds  rx=+%-6d tx=+%-6d  oe=%d\n' \
            "$ts" "$elapsed" "$d_rx" "$d_tx" "$oe" >&2
    fi

    if [ "$DURATION" -gt 0 ] && [ "$elapsed" -ge "$DURATION" ]; then
        break
    fi
    sleep "$INTERVAL"
done
