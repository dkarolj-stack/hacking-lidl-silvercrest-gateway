# Bootloader-UART-Xmodem for Lidl Gateway

UART XMODEM bootloader for EFR32MG1B232F256GM48 (Lidl Silvercrest Gateway).

This bootloader enables firmware updates via UART using the XMODEM-CRC protocol, without requiring SWD/JTAG access.

## Quick Start

```bash
# Build the bootloader
./build_bootloader.sh

# Flash via J-Link (combined = first stage + main stage)
commander flash firmware/bootloader-uart-xmodem-2.4.2-combined.s37 --device EFR32MG1B232F256GM48
```

## Prerequisites

- **slc** (Silicon Labs CLI) in PATH
- **arm-none-eabi-gcc** in PATH
- **commander** in PATH
- **GECKO_SDK** environment variable set

Or use Docker from the `2-Zigbee-Radio-Silabs-EFR32` directory:

```bash
docker run -it --rm -v $(pwd)/..:/workspace lidl-gateway-builder \
    /workspace/2-Zigbee-Radio-Silabs-EFR32/23-Bootloader-UART-Xmodem/build_bootloader.sh
```

## Build Process

```
1. Copy slcp + slpb from patches/
        ↓
2. slc generate
        ↓
3. Copy config headers from patches/
        ↓
4. make -Oz
        ↓
5. Post-build (commander convert/gbl create)
```

## Hardware Configuration

### UART Pinout (USART0)

| Signal | Port | Pin | Description |
|--------|------|-----|-------------|
| TX | PA0 | 0 | Transmit to RTL8196E |
| RX | PA1 | 1 | Receive from RTL8196E |

No hardware flow control (standard Simplicity Studio configuration).

## Output Files

After running `./build_bootloader.sh`, files are in `firmware/`:

| File | Description |
|------|-------------|
| `bootloader-uart-xmodem-X.Y.Z.s37` | Main stage bootloader |
| `bootloader-uart-xmodem-X.Y.Z-crc.s37` | Main stage with CRC |
| `bootloader-uart-xmodem-X.Y.Z-combined.s37` | First stage + Main stage (for J-Link) |
| `bootloader-uart-xmodem-X.Y.Z.gbl` | GBL image (for XMODEM/UART upload) |
| `first_stage.s37` | First stage only |

## Using the Bootloader

Once flashed, the bootloader responds to serial commands at 115200 baud:

| Command | Action |
|---------|--------|
| `1` | Start XMODEM transfer (upload new firmware) |
| `2` | Start application |

To enter the bootloader:
- Send serial break, OR
- No valid application present

______________________________________________________________________

## Understanding the 2-Stage Bootloader Architecture (Series 1)

EFR32MG1B (Gecko Series 1) devices use a **two-stage bootloader system**:

### Stage 1 – First-stage bootloader (BSL)

- Resides in main flash memory starting at address **0x0000**
- Minimal: verifies and launches Stage 2
- Cannot be updated via UART or OTA
- Can only be overwritten using **SWD and a debugger**

### Stage 2 – Main bootloader

- Resides in main flash memory starting at address **0x0800**
- Contains UART XMODEM functionality
- Can be updated in the field via `.gbl` packages

### Application

- Resides in flash memory starting at address **0x4000**
- Updated via XMODEM using `.gbl` files

### Memory Map

```
0x00000000 ┌─────────────────────────┐
           │  First Stage (2 KB)     │ ← Can only be updated via SWD
0x00000800 ├─────────────────────────┤
           │  Main Bootloader (14 KB)│ ← UART XMODEM logic
0x00004000 ├─────────────────────────┤
           │  Application            │ ← NCP-UART-HW or Router firmware
           │  (~200 KB)              │
0x0003E000 ├─────────────────────────┤
           │  NVM3 Storage (36 KB)   │ ← Network keys, tokens
0x00040000 └─────────────────────────┘
```

______________________________________________________________________

## Flashing the Bootloader

> **Warning**: Bootloader firmware flashing always carries some risk. If the process is interrupted or fails, the device may become unresponsive and require a J-Link/SWD debugger to recover. **Having a debugger available is strongly recommended** before attempting any bootloader update.

### Option 1: Flash via J-Link/SWD (Recommended)

The safest method. Flash the combined image (first stage + main stage):

```bash
commander flash firmware/bootloader-uart-xmodem-2.4.2-combined.s37 --device EFR32MG1B232F256GM48
```

Or flash stages separately:

```bash
# First stage (only if missing/corrupted)
commander flash firmware/first_stage.s37 --device EFR32MG1B232F256GM48

# Main stage with CRC
commander flash firmware/bootloader-uart-xmodem-2.4.2-crc.s37 --device EFR32MG1B232F256GM48
```

### Option 2: Flash via universal-silabs-flasher (Remote, Stage 2 only)

You can update the **Stage 2 bootloader** remotely using `universal-silabs-flasher` if you already have a working bootloader installed.

> **Note**: This only updates Stage 2. Stage 1 cannot be updated via UART.

#### Prerequisites

1. **Install universal-silabs-flasher** (see [22-Backup-Flash-Restore](../22-Backup-Flash-Restore/) for details)

2. **Restart serialgateway with `-f` flag:**

   On the gateway via SSH:
   ```bash
   killall serialgateway && serialgateway -f
   ```

   The `-f` flag disables hardware flow control, allowing the flasher to communicate with the bootloader.

   **Important:** Close all SSH sessions connected to the gateway before flashing.

#### Flash the bootloader

```bash
universal-silabs-flasher \
    --device socket://192.168.1.X:8888 \
    flash --firmware firmware/bootloader-uart-xmodem-2.4.2.gbl
```

#### Expected error at the end

After flashing the bootloader, `universal-silabs-flasher` will show an **error** because it cannot find a valid application:

```
...
Firmware uploaded successfully
Running firmware...
Error: Failed to detect firmware type
```

**This error is expected and can be ignored.** The bootloader was flashed successfully, but there is no application yet. The bootloader will now display its menu waiting for a firmware upload.

#### After flashing: upload an application

Immediately after updating the bootloader, upload an application (NCP, RCP, or Router):

```bash
universal-silabs-flasher \
    --device socket://192.168.1.X:8888 \
    flash --firmware ../24-NCP-UART-HW/firmware/ncp-uart-hw-7.5.1.gbl
```

Then reboot the gateway:
```bash
reboot
```

______________________________________________________________________

## Creating Combined Images (Bootloader + Application)

To update both bootloader (stage 2) and application in one UART transfer:

```bash
commander gbl create upgrade.gbl \
    --app ncp-uart-hw.s37 \
    --bootloader bootloader-uart-xmodem-2.4.2-crc.s37
```

> **Note**: This only updates the main bootloader (stage 2), not the first stage. First stage always requires SWD access.

______________________________________________________________________

## patches/ Directory

| File | Purpose |
|------|---------|
| `bootloader-uart-xmodem.slcp` | Project config with components |
| `bootloader-uart-xmodem.slpb` | Post-build config (generates .s37, -crc.s37, -combined.s37, .gbl) |
| `btl_uart_driver_cfg.h` | UART pin configuration (USART0 PA0/PA1, no flow control) |
