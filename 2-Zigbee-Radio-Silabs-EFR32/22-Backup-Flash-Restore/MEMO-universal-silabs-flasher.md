# Universal Silabs Flasher — Technical Memo

How `universal-silabs-flasher` (USF) works on the Lidl Silvercrest Gateway,
why baud rate recovery is tricky over TCP, and how `flash_efr32.sh` solves it.

---

## 1. How USF Works Over TCP

USF connects to the EFR32 via `socket://192.168.1.88:8888`. This TCP socket
is bridged to the EFR32's UART by the in-kernel `rtl8196e-uart-bridge`
driver on the gateway's RTL8196E CPU (kernel 6.18; replaces the former
userspace `serialgateway` daemon from v3.0).

**Critical insight: over `socket://`, USF's baud rate parameter is ignored.**
The Python serial transport (pyserial-asyncio) discards baud rate settings for
TCP connections. The actual UART speed is controlled entirely by the bridge's
sysfs `baud` parameter on the gateway
(`/sys/module/rtl8196e_uart_bridge/parameters/baud`).

```
USF (PC)                    kernel UART bridge (gateway)      EFR32
─────────                   ────────────────────────          ─────
   │  socket://IP:8888          │                               │
   │  (baud param ignored)      │  UART @ bridge baud           │
   ├───────────────────────────>├──────────────────────────────>│
   │                            │                               │
   │  USF says "probe at 460800"│                               │
   │  but actual wire speed is  │  actual wire = 115200         │
   │  whatever the bridge's     │  (or whatever baud says)      │
   │  baud parameter says.      │                               │
```

This means:
- All probe methods reach the EFR32 at the **same** baud rate (the bridge's)
- USF **cannot** change the baud rate mid-session — only the gateway can
- The "baud" column in USF's probe table only matters for direct serial
  connections (e.g., `/dev/ttyUSB0`)

---

## 2. Default Probe Methods

USF probes firmware types in this order (from `const.py`):

| # | Protocol | Baud | Notes |
|---|----------|------|-------|
| 1 | GECKO_BOOTLOADER | 115200 | Always first |
| 2 | EZSP | 115200 | NCP firmware |
| 3 | EZSP | 460800 | |
| 4 | SPINEL | 460800 | OT-RCP firmware |
| 5 | CPC | 460800 | RCP-UART-HW |
| 6 | CPC | 115200 | |
| 7 | CPC | 230400 | |
| 8 | ROUTER | 115200 | Z3-Router |

**Over TCP, the baud column has no effect** (see section 1). The protocol
column is what matters — USF tries each protocol's handshake until one
responds.

### Patch (`silabs-flasher-probe-methods.patch`)

The patch replaces the upstream probe table: it adds EZSP@230400,
SPINEL@115200, SPINEL@230400, and **removes all 460800 entries**. The Lidl
gateway's UART does not work reliably at 460800 baud (see 25-RCP-UART-HW),
so probing at that speed only wastes ~30 seconds on timeouts.

The patch is applied automatically by `flash_efr32.sh` when it installs USF
in the venv. It uses `patch --dry-run` to verify context before applying —
if a future USF version changes `const.py`, the patch fails silently and USF
is installed unpatched.

The patch allowed **simplifying `flash_efr32.sh`**: instead of manually
probing, extracting the detected protocol type, and dispatching to a
per-protocol Python script for `enter_bootloader` (~40 lines of code), the
recovery now runs `$FLASHER flash` and lets USF handle detection +
enter_bootloader internally. The patch also benefits users who use USF on a
**direct serial connection** where USF does control the baud rate.

---

## 3. The Baud Rate Mismatch Problem

All pre-built firmware runs at **115200**, matching the Gecko Bootloader.
If you recompile firmware at a different baud (e.g., 230400), the standard
flash fails:

```
kernel bridge @ 115200  ←→  EFR32 firmware @ 230400  →  garbage  →  probe fails
```

USF tries all its probe methods, but since they all go through the bridge
at 115200, none can reach the 230400 firmware. Result:
`Error: Failed to probe running application type`.

---

## 4. Recovery: How `flash_efr32.sh` Handles It

When the standard flash fails, the script enters a recovery loop. The
in-kernel UART bridge lets us change the wire baud without any process
restart — `echo <baud> > .../baud` re-applies termios in place and
TCP:8888 never drops.

```
Step 1: Standard flash (bridge @ 115200, flow_control=0)
        → FAILS: firmware at non-standard baud

Step 2: for BAUD in 230400 460800:
          echo $BAUD > /sys/module/rtl8196e_uart_bridge/parameters/baud
          Run USF flash again
            → USF detects firmware (bridge now at matching baud)
            → USF sends enter_bootloader (protocol-specific command)
            → Gecko Bootloader starts... but at 115200
            → USF tries to probe bootloader at $BAUD
            → FAILS: baud mismatch with bootloader

Step 3: echo 115200 > /sys/module/rtl8196e_uart_bridge/parameters/baud
        → Now matches the Gecko Bootloader

Step 4: USF flash with --probe-methods "bootloader:115200"
        → Detects Gecko Bootloader
        → Flashes firmware via Xmodem
        → SUCCESS
```

The key trick: **USF sends `enter_bootloader` as a side-effect of its failed
flash attempt** in step 2. Even though the flash fails (baud mismatch with
bootloader), the EFR32 is now in bootloader mode. We just need to change the
bridge baud to 115200 to talk to it.

### Why the baud dance is still needed

1. USF cannot change the bridge's baud (no out-of-band control channel
   over `socket://`)
2. The firmware baud ≠ the bootloader baud (bootloader is always 115200)
3. After `enter_bootloader`, we MUST retune the bridge to a different
   baud — but with the kernel bridge this is one sysfs write, not a
   process respawn.

---

## 5. Protocol-Specific `enter_bootloader` Commands

USF handles all protocols transparently during its flash flow:

| Protocol | Firmware type | Method | Details |
|----------|---------------|--------|---------|
| **EZSP** | NCP-UART-HW | `launchStandaloneBootloader(0x01)` | Via bellows ASH framing |
| **Spinel** | OT-RCP | HDLC reset frame, reason=BOOTLOADER | `7E 80 01 03 63 E1 7E` — fire-and-forget |
| **CPC** | RCP-UART-HW | Two unnumbered frames: set reboot mode + reset | CPC flag `0x14` (not HDLC `0x7E`) |
| **Router** | Z3-Router | Text command `bl r` | Plain ASCII over UART |
| **Gecko Bootloader** | — | Already in bootloader | No command needed |

In `flash_efr32.sh`, we don't need to know which protocol is running — USF
detects and handles it automatically during the flash attempt.

---

## 6. The `--probe-methods` Option

A **global** option (before the subcommand), not a flash-specific one:

```bash
# Correct — --probe-methods before the subcommand:
universal-silabs-flasher --device socket://IP:8888 \
    --probe-methods "bootloader:115200" \
    flash --firmware fw.gbl

# Wrong — will fail with "unrecognized arguments":
universal-silabs-flasher --device socket://IP:8888 \
    flash --probe-methods "bootloader:115200" --firmware fw.gbl
```

Format: `"type:baud,type:baud"`, e.g., `"bootloader:115200,spinel:460800"`.

We use `--probe-methods "bootloader:115200"` in the recovery step (step 4) to
tell USF to **only** look for the Gecko Bootloader, skipping application
firmware probes that would timeout.

---

## 7. Tested Recovery Scenarios

| Firmware on EFR32 | Baud | Target firmware | Result |
|-------------------|------|-----------------|--------|
| OT-RCP (Spinel) | 230400 | OT-RCP 115200 | **OK** — Spinel enter_bootloader → flash via Gecko Bootloader |
| NCP (EZSP) | 230400 | NCP 115200 | **OK** — EZSP launchStandaloneBootloader → flash via Gecko Bootloader |

If the firmware is completely unresponsive at any baud rate (e.g., corrupted
flash), software recovery is not possible — a J-Link/SWD debugger is required.

---

## 8. Timing

- After `enter_bootloader`, the Gecko Bootloader waits for a connection
  (typically ~60 seconds before timeout). The recovery script completes well
  within this window.
- Changing the bridge baud via sysfs is a single `tty_set_termios()` —
  no `sleep` needed, the write returns only after termios is applied.
- The full recovery (standard flash fail + scan + enter_bootloader +
  baud retune + bootloader flash) takes about **90 seconds** for a
  208 KB firmware.

---

## 9. USF Internal Architecture (Key Files)

For reference, the USF Python package installed in the venv
(`silabs-flasher/lib/python3.12/site-packages/universal_silabs_flasher/`):

| File | Role |
|------|------|
| `const.py` | `DEFAULT_PROBE_METHODS` — probe order and baud rates |
| `flasher.py` | Main logic: `probe_app_type()`, `enter_bootloader()`, flash flow |
| `flash.py` | CLI entry point, `--probe-methods` option definition |
| `spinel.py` | Spinel/HDLC protocol implementation |
| `cpc.py` | CPC protocol implementation |
| `emberznet.py` | EZSP/bellows protocol (ASH framing) |
| `router.py` | Router protocol (plain text commands) |
| `common.py` | `connect_protocol()` helper, CRC16 Kermit |

---

## 10. Summary

| Scenario | Result |
|----------|--------|
| Standard flash (firmware at 115200) | `flash_efr32.sh` flashes in one shot |
| Firmware at non-standard baud (230400, 460800) | `flash_efr32.sh` recovers automatically (see section 4) |
| Firmware completely unresponsive | J-Link/SWD debugger required |
| USF patch (`silabs-flasher-probe-methods.patch`) | Simplifies recovery script; also useful for direct serial connections |
