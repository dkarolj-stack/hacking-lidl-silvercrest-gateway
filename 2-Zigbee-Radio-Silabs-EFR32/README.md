# Zigbee Radio — Silabs EFR32MG1B

This section covers the **Zigbee coprocessor** embedded in the gateway: a Silabs EFR32MG1B chip running dedicated wireless firmware.

## Overview

The EFR32MG1B handles all Zigbee radio communication. The stock firmware uses the Tuya protocol, but it can be replaced with open-source alternatives to work with **Zigbee2MQTT**, **ZHA**, or **Matter/Thread**.

## Build All Firmware

```bash
# Using Docker (from 1-Build-Environment/)
docker run -it --rm -v $(pwd)/..:/workspace lidl-gateway-builder \
    /workspace/2-Zigbee-Radio-Silabs-EFR32/build_efr32.sh

# Or native
./build_efr32.sh              # Build all 5 firmware variants
./build_efr32.sh ncp rcp      # Build specific targets
./build_efr32.sh --help       # Show available targets
```

## Contents

| Directory | Description |
|-----------|-------------|
| [20-EZSP-Reference](./20-EZSP-Reference/README.md) | Introduction to EZSP protocol and EmberZNet stack |
| [21-Simplicity-Studio](./21-Simplicity-Studio/README.md) | Build your own firmware with Silabs IDE |
| [22-Backup-Flash-Restore](./22-Backup-Flash-Restore/README.md) | Backup, flash, and restore the Zigbee chip firmware |
| [23-Bootloader-UART-Xmodem](./23-Bootloader-UART-Xmodem/README.md) | Flash firmware via UART using Gecko bootloader |
| [24-NCP-UART-HW](./24-NCP-UART-HW/README.md) | NCP firmware for Zigbee2MQTT and ZHA |
| [25-RCP-UART-HW](./25-RCP-UART-HW/README.md) | RCP firmware: EmberZNet 8.2.2 / EZSP v18 via host-side `zigbeed` |
| [26-OT-RCP](./26-OT-RCP/README.md) | OpenThread RCP firmware for zigbee-on-host or Thread/Matter |
| [27-Router](./27-Router/README.md) | Zigbee 3.0 Router SoC firmware to extend mesh network |

## Firmware: NCP (Network Co-Processor)

- The Zigbee stack runs on the EFR32
- Simple setup: just flash and connect to Zigbee2MQTT or ZHA
- Recommended for most users who want a Zigbee coordinator

## Firmware: RCP (Radio Co-Processor)

Two RCP options are available:

### RCP with cpcd + zigbeed (25-RCP-UART-HW)

- Uses Silicon Labs' CPC protocol (Co-Processor Communication)
- Runs with cpcd + zigbeed on the host
- Modern stack on Series 1 hardware: **EmberZNet 8.2.2 / EZSP v18** (zigbeed runs host-side, so the EFR32MG1B is no longer the bottleneck)
- Single-stack only — Zigbee+Thread concurrently is not supported on this gateway (Series 1 has no Concurrent Multiprotocol; reflash with OT-RCP for Thread/Matter)

### OpenThread RCP (26-OT-RCP)

- Standard OpenThread RCP firmware (Spinel/HDLC, fully open-source)
- **One firmware, three use cases:**
  - **ZoH** — Zigbee on host via [zigbee-on-host](https://github.com/Nerivec/zigbee-on-host), integrated in Zigbee2MQTT 2.x as the `zoh` adapter
  - **OTBR on host** — Thread / Matter-over-Thread, OTBR running on an external PC/Pi
  - **OTBR on gateway** — Thread / Matter-over-Thread, OTBR running natively on the RTL8196E
- Same `.gbl`, you switch use case by changing what runs host-side — no EFR32 reflash

## Firmware: Router (SoC)

- Standalone Zigbee 3.0 router, no host required
- Extends your Zigbee mesh network coverage
- Auto-joins open networks via network steering
- Transforms the gateway into a dedicated range extender
