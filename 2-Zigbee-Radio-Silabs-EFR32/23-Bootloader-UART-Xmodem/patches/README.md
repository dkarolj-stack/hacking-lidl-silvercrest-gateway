# Patches for Bootloader-UART-Xmodem

Project files for building the UART XMODEM bootloader for EFR32MG1B232F256GM48.
Configuration matches Simplicity Studio standard project.

## Files

| File | Purpose |
|------|---------|
| `bootloader-uart-xmodem.slcp` | Project config with components and settings |
| `bootloader-uart-xmodem.slpb` | Post-build configuration (generates .s37, -crc.s37, -combined.s37, .gbl) |
| `btl_uart_driver_cfg.h` | UART pin configuration (USART0 PA0/PA1, no flow control) |

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

## Output Files

| File | Description |
|------|-------------|
| `bootloader-uart-xmodem.s37` | Main stage bootloader |
| `bootloader-uart-xmodem-crc.s37` | Main stage with CRC |
| `bootloader-uart-xmodem-combined.s37` | First stage + Main stage (for J-Link flash) |
| `bootloader-uart-xmodem.gbl` | GBL image (for XMODEM upload) |
| `first_stage.s37` | First stage only |

---

## Pin Configuration

### UART (USART0)

| Signal | Port | Pin | Location |
|--------|------|-----|----------|
| TX | PA0 | 0 | 0 |
| RX | PA1 | 1 | 0 |

No hardware flow control (RTS/CTS disabled).

---

## Bootloader Size

The bootloader occupies the first 16 KB of flash:
- First stage: 0x0000 - 0x0800 (2 KB)
- Main stage: 0x0800 - 0x4000 (14 KB)

Applications start at 0x4000.
