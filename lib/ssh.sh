# lib/ssh.sh — common SSH helpers for the flash_*.sh scripts.
#
# Sourced by:
#   - flash_efr32.sh                                   (repo root)
#   - flash_install_rtl8196e.sh                        (repo root)
#   - 3-Main-SoC-Realtek-RTL8196E/flash_remote.sh
#
# Provides hardened SSH options that prevent the "ssh hangs forever after
# the gateway reboots" failure mode observed during v3.1 testing, plus a
# retry-on-transport-failure helper. Intentionally not executable; this
# file is only meant to be sourced.

# Hardened SSH options (bash array — splice into ssh invocation).
# Callers add their own scenario-specific options on top:
#   - StrictHostKeyChecking policy (post-install: accept-new; first-flash: no)
#   - ControlMaster / ControlPath / ControlPersist (multiplexed sessions)
#   - port, identity file, etc.
SSH_HARDEN_OPTS=(
    -o ConnectTimeout=5
    -o ServerAliveInterval=3
    -o ServerAliveCountMax=2
    -o BatchMode=yes
)

# ssh_retry: ssh wrapper that retries on transport failures only (rc=255).
# Real remote-command exit codes pass through unchanged so callers can
# branch on them.
#
# Usage:  ssh_retry [ssh-args...] target command...
#
# Example:
#   ssh_retry "${SSH_HARDEN_OPTS[@]}" -o StrictHostKeyChecking=accept-new \
#             "root@${GW_IP}" "uptime"
ssh_retry() {
    local attempt rc
    for attempt in 1 2 3; do
        ssh "$@"
        rc=$?
        # rc=255 is SSH's "transport failed" code (cannot connect, host
        # unreachable, connection dropped mid-flight). Anything else is
        # the real remote command exit code — never retry that.
        if [ $rc -ne 255 ]; then
            return $rc
        fi
        if [ $attempt -lt 3 ]; then
            echo "Warning: SSH transport failed (attempt $attempt/3); retrying in $((attempt * 2))s..." >&2
            sleep $((attempt * 2))
        fi
    done
    echo "Error: SSH failed after 3 attempts." >&2
    return 43
}

# wait_for_port: poll a TCP port until it accepts a connection or timeout.
# Useful after a kernel-bridge baud change (port closes briefly) or after
# a gateway reboot.
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
