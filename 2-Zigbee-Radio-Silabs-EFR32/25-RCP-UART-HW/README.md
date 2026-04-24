# RCP 802.15.4 Firmware

Radio Co-Processor (RCP) firmware for the EFR32MG1B232F256GM48 chip found in the Lidl Silvercrest Smart Home Gateway.

This firmware transforms the gateway's Zigbee chip into a **Radio Co-Processor** that handles only the 802.15.4 PHY/MAC layer. The protocol stack runs on the host side, enabling two usage modes:

| Mode | Stack | Host | Details |
|------|-------|------|---------|
| **Zigbee (cpcd + zigbeed)** | EmberZNet | External PC/RPi via the in-kernel UART bridge | See [Host Software Setup](#host-software-setup) |
| **Thread (OTBR)** | OpenThread | Natively on the RTL8196E gateway | See [`3-Main-SoC.../34-Userdata/ot-br-posix/`](../../3-Main-SoC-Realtek-RTL8196E/34-Userdata/ot-br-posix/README.md) |

## About RCP Architecture

Unlike standalone firmware (like the Router), an RCP delegates the entire Zigbee protocol stack to a host computer. The EFR32 only handles the radio PHY/MAC layer and communicates with the host via the **CPC Protocol** (Co-Processor Communication).

```
+-------------------+    UART     +-------------------+   Ethernet   +---------------------+
|  EFR32MG1B (RCP)  |   460800    |  RTL8196E         |    TCP/IP    |  Host (x86/ARM)     |
|                   |    baud     |  (Gateway SoC)    |              |                     |
|  802.15.4 PHY/MAC |<----------->|  in-kernel        |<------------>|  cpcd               |
|  + CPC Protocol   |   ttyS1     |  UART<->TCP bridge|   port 8888  |    |                |
|                   |             |  (rtl8196e-uart-  |              |    v                |
|                   |             |   bridge)         |              |                     |
|  CPC Protocol v5  |             |                   |              |  zigbeed            |
|  HW Flow Control  |             |                   |              |  (Zigbee stack)     |
|                   |             |                   |              |    |                |
+-------------------+             +-------------------+              |    v                |
                                                                     |  Zigbee2MQTT        |
                                                                     +---------------------+
```

**Why use RCP instead of NCP?**

| Aspect | NCP (24-NCP-UART-HW) | RCP (this firmware) |
|--------|----------------------|---------------------|
| Stack location | On EFR32 (limited RAM) | On host (unlimited resources) |
| Protocol | EZSP (binary) | CPC (multiplexed) |
| Multiprotocol | No | Yes (Zigbee + Thread) |
| Stack updates | Requires reflash | Just update zigbeed |
| Network size | Limited by EFR32 RAM | Host memory is the limit |

## Hardware

| Component | Specification |
|-----------|---------------|
| Zigbee SoC | EFR32MG1B232F256GM48 |
| Flash | 256KB |
| RAM | 32KB |
| Radio | 2.4GHz IEEE 802.15.4 |
| UART | PA0 (TX), PA1 (RX), PA4 (RTS), PA5 (CTS) @ 460800 baud |

---

## Option 1: Flash Pre-built Firmware (Recommended)

Pre-built firmware is available in the `firmware/` directory. From the repository root:

```bash
./flash_efr32.sh <GATEWAY_IP>
# Select [3] RCP-UART-HW
```

The script handles everything (switch the in-kernel UART bridge to flash mode, flash, reboot).

Then continue to [Host Software Setup](#host-software-setup) to configure cpcd and zigbeed on your host machine.

---

## Option 2: Build from Source

For users who want to modify the CPC configuration, change baudrate, or use a different SDK version.

### Prerequisites

Install Silicon Labs tools (see `1-Build-Environment/12-silabs-toolchain/`):

```bash
cd 1-Build-Environment/12-silabs-toolchain
./install_silabs.sh
```

This installs:
- `slc-cli` - Silicon Labs Configurator
- `arm-none-eabi-gcc` - ARM GCC toolchain
- `commander` - Simplicity Commander
- Gecko SDK with EmberZNet

### Build

```bash
cd 2-Zigbee-Radio-Silabs-EFR32/25-RCP-UART-HW
./build_rcp.sh
```

### Output

```
firmware/
└── rcp-uart-802154.gbl   # For UART/Xmodem flashing
```

### Customization

Edit `patches/rcp-uart-802154.slcp` for SDK configuration, or `patches/sl_cpc_drv_uart_usart_vcom_config.h` for UART settings (pins, baudrate).

### Flash

**Via network (same as Option 1):**
```bash
./flash_efr32.sh <GATEWAY_IP>
# Select [3] RCP-UART-HW
```

**Via J-Link/SWD** (if you have physical access to the SWD pads):
```bash
commander flash firmware/rcp-uart-802154.gbl \
    --device EFR32MG1B232F256GM48
```

> For a detailed explanation of how `universal-silabs-flasher` works (firmware detection, bootloader entry, the `-f` flag, troubleshooting), see [22-Backup-Flash-Restore](../22-Backup-Flash-Restore/README.md).

---

## Host Software Setup

After flashing the RCP firmware, you need to configure the host software chain.

### Required Components

| Component | Version | Source | Description |
|-----------|---------|--------|-------------|
| cpcd | v4.5.3 | [SiliconLabs/cpc-daemon](https://github.com/SiliconLabs/cpc-daemon) | CPC daemon |
| zigbeed | EmberZNet 8.2.2 | Simplicity SDK 2025.6.3 | Zigbee stack daemon (recommended) |
| zigbeed | EmberZNet 7.5.1 | Gecko SDK 4.5.0 | Zigbee stack daemon (legacy) |

### Build Instructions

See subdirectories for detailed build instructions:
- `cpcd/` - CPC daemon (for host)
- `zigbeed-8.2.2/` - zigbeed EmberZNet 8.2.2 (recommended)
- `zigbeed-7.5.1/` - zigbeed EmberZNet 7.5.1 (legacy)
- `rcp-stack/` - Systemd service manager for the complete chain
- `cpcd-rtl8196e/` - Cross-compile cpcd for gateway (experimental)

### Quick Start with Docker (Recommended)

A pre-built Docker image is available for **PC (amd64)** and **Raspberry Pi (arm64)**:

```bash
# Pull the image
docker pull ghcr.io/jnilo1/cpcd-zigbeed:latest

# Or use the full stack with Zigbee2MQTT
cd docker/
# Edit docker-compose-zigbee.yml: set RCP_HOST to your gateway's IP
docker compose -f docker-compose-zigbee.yml up -d
```

See `docker/README.md` for detailed instructions.

| Image | cpcd | EmberZNet | EZSP | Architectures |
|-------|------|-----------|------|---------------|
| `ghcr.io/jnilo1/cpcd-zigbeed:latest` | 4.5.3 | 8.2.2 | v18 | amd64, arm64 |

### Quick Start with rcp-stack (Native)

The `rcp-stack` tool manages the entire cpcd + zigbeed chain natively (without Docker):

```bash
# Start the stack
rcp-stack up

# Check status
rcp-stack status

# Stop the stack
rcp-stack down

# Troubleshoot
rcp-stack doctor
```

### Zigbee2MQTT Configuration

With rcp-stack (recommended):
```yaml
serial:
  port: /tmp/ttyZ2M
  adapter: ember
```

---

## Baudrate and Network Considerations

### Baudrate Options

| Baudrate | Status | Notes |
|----------|--------|-------|
| 115200 | Supported | Conservative |
| 230400 | Supported | |
| **460800** | **Default** | Max supported by cpcd (POSIX baud limit) |
| 692857 | Not usable | Non-standard — cpcd rejects it |
| 892857 | Not usable | Non-standard — cpcd rejects it |

cpcd validates baud rates against the POSIX standard list and rejects
non-standard values like 691200 or 892857 (even over a TCP/PTY bridge
where the baud is irrelevant). **460800 is the practical maximum for
RCP mode.** Higher bauds (up to 892857) are available for NCP and
OT-RCP, which don't use cpcd.

All bauds run through the in-kernel `rtl8196e-uart-bridge` driver on
kernel 6.18 — change via
`echo <baud> > /sys/module/rtl8196e_uart_bridge/parameters/baud`.

### Network Size vs Baudrate

| Liaison | Throughput |
|---------|------------|
| 802.15.4 radio | ~25 KB/s |
| UART 115200 | ~11 KB/s |
| UART 460800 | ~46 KB/s |
| UART 892857 | ~89 KB/s |

At 115200, the UART is ~2x slower than the Zigbee radio. At 460800+
it is no longer the bottleneck.

| Network size | 115200 | 230400 | 460800+ |
|--------------|--------|--------|---------|
| < 50 devices | OK | OK | OK |
| 50-100 devices | OK* | OK | Recommended |
| > 100 devices | May bottleneck | OK | Recommended |

*OK for normal use; possible latency during traffic spikes (OTA updates, large groups).

For most home installations, 115200 is sufficient.

### Maximum Baud: Why 892857 and Not 921600

The RTL8196E UART has a **fixed 16× oversampling** with integer-only
divisors and a 200 MHz bus clock. The achievable baud is
`200000000 / (16 × N)` for integer N. For 921600 the divisor falls at
13.56 — neither 13 nor 14 gives acceptable error (−3.1% / +4.3%).

**892857** = 200000000 / (16 × 14) hits an exact integer divisor,
giving **0% baud error** on the RTL side. The EFR32 reaches 893023
with its fractional divider (0.02% mismatch). This is 7.7× the
original 115200 and within 3% of 921600.

See `3-Main-SoC-Realtek-RTL8196E/32-Kernel/POST-MORTEM-6.18.md` for
the full N+1 divisor investigation.

Check UART errors on the gateway:
```bash
cat /proc/tty/driver/serial
# fe:xxx = framing errors, oe:xxx = overrun errors
```

### Changing Baudrate

1. Edit `patches/sl_cpc_drv_uart_usart_vcom_config.h` — change
   `SL_CPC_DRV_UART_VCOM_BAUDRATE`
2. Rebuild firmware and flash the EFR32
3. Set the bridge baud on the gateway:
   `echo <baud> > /sys/module/rtl8196e_uart_bridge/parameters/baud`
   (persist in `/userdata/etc/radio.conf` via `BRIDGE_BAUD=<baud>`)
4. Update `UART_BAUDRATE` in `docker-compose-zigbee.yml` (or `cpcd.conf`)

The baud must be a standard POSIX value (115200, 230400, 460800) for
cpcd to accept it.

---

## TCP Stability Requirements

The CPC protocol is sensitive to network conditions. For reliable operation:

| Requirement | Why |
|-------------|-----|
| **Hardware flow control** | Prevents buffer overruns |
| **Direct Ethernet** | Minimizes latency and jitter |
| **No WiFi bridges** | WiFi adds unpredictable latency |
| **Avoid congested switches** | Packet delays cause CPC timeouts |

**Recommended:** Connect the gateway directly to the host with an Ethernet cable.

---

## Troubleshooting

### Flashing Issues

| Problem | Solution |
|---------|----------|
| No response from RCP | Verify TCP: `nc -zv <gateway-ip> 8888` |
| Xmodem timeout | Close all SSH sessions, use `-f` flag |
| Wrong firmware flashed | Reflash - the bootloader is always preserved |

### cpcd Connection Issues

| Problem | Solution |
|---------|----------|
| cpcd won't connect | Check `tcp_client_address` in cpcd.conf |
| CPC version mismatch | Use GSDK 4.5.0 for CPC Protocol v5 |
| Frequent disconnects | Use direct Ethernet, check for WiFi bridges |

### zigbeed Issues

| Problem | Solution |
|---------|----------|
| zigbeed won't start | Check cpcd is running: `rcp-stack status` |
| Stack version mismatch | Rebuild zigbeed with matching SDK version |

---

## Memory Usage

| Resource | Used | Available |
|----------|------|-----------|
| Flash | ~116 KB | 256 KB |
| RAM | ~22 KB | 32 KB |

---

## Features

- **RTL8196E Boot Delay:** 1-second delay for host UART initialization
- **Hardware Flow Control:** RTS/CTS required for reliable TCP operation
- **CPC Security Disabled:** Saves ~45KB flash (not needed for local network)
- **Multiprotocol Ready:** Can run Zigbee + OpenThread simultaneously

---

## Project Structure

```
25-RCP-UART-HW/
├── build_rcp.sh                 # RCP firmware build script
├── README.md                    # This file
├── patches/                     # RCP firmware patches
│   ├── rcp-uart-802154.slcp                 # Project config
│   ├── main.c                               # Entry point (1s delay)
│   ├── sl_cpc_drv_uart_usart_vcom_config.h  # UART pins
│   └── sl_cpc_security_config.h             # CPC security disabled
├── firmware/                    # Output (rcp-uart-802154.gbl)
├── cpcd/                        # CPC daemon build scripts
├── zigbeed-7.5.1/               # zigbeed EmberZNet 7.5.1 (legacy)
├── zigbeed-8.2.2/               # zigbeed EmberZNet 8.2.2 (recommended)
├── docker/                      # Docker stack (cpcd + zigbeed + Z2M)
└── rcp-stack/                   # Systemd service manager
    ├── bin/rcp-stack            # Main script
    ├── scripts/                 # Helper scripts
    ├── systemd/user/            # Service units
    └── examples/                # Config examples
```

---

## Related Projects

- `24-NCP-UART-HW/` - NCP firmware (simpler, stack on EFR32)
- `27-Router/` - Router firmware (autonomous, no host needed)

## References

- [CPC Daemon](https://github.com/SiliconLabs/cpc-daemon)
- [AN1333: Multiprotocol RCP](https://www.silabs.com/documents/public/application-notes/an1333-concurrent-protocols-with-802-15-4-rcp.pdf)
- [rtl8196e-uart-bridge](https://github.com/jnilo1/hacking-lidl-silvercrest-gateway/tree/main/3-Main-SoC-Realtek-RTL8196E/32-Kernel/files-6.18/drivers/net/rtl8196e-uart-bridge)

## License

Educational and personal use. Silicon Labs SDK components under their respective licenses.
