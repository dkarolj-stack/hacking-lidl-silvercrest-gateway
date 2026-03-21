# Patches for NCP-UART-HW Firmware

Ready-to-use project files for building an optimized NCP firmware for the EFR32MG1B232F256GM48 (Lidl Silvercrest Gateway).

## Files

| File | Purpose |
|------|---------|
| `ncp-uart-hw.slcp` | Project config with all optimizations |
| `main.c` | Main function with 1s boot delay for RTL8196E |
| `app.c` | Minimal callbacks (radio calibration) |
| `sl_iostream_usart_vcom_config.h` | UART config (PA0/PA1, RTS/CTS PA4/PA5) |
| `sl_rail_util_pti_config.h` | PTI disabled (not connected) |

## Build Process

```
1. Copy slcp, main.c, app.c from patches/
        ↓
2. slc generate
        ↓
3. Copy config headers from patches/
        ↓
4. make -Oz
```

---

## ncp-uart-hw.slcp

**Pre-configured project** optimized for 256KB flash.

### Components Included

| Component | Purpose |
|-----------|---------|
| `iostream_usart[vcom]` | UART communication with host |
| `token_manager` | NVM token storage |
| `udelay` | Microsecond delay (for boot delay) |
| `zigbee_app_framework_common` | Zigbee application framework |
| `zigbee_gp` | Green Power support |
| `zigbee_ncp_uart_hardware` | NCP UART driver |
| `zigbee_pro_stack` | Zigbee PRO stack |
| `zigbee_r22_support` | R22 revision support |
| `zigbee_security_link_keys` | Security link keys |
| `zigbee_source_route` | Source routing for large networks |

### Components Removed (Flash Savings ~17 KB)

| Component | Savings | Reason |
|-----------|---------|--------|
| `zigbee_debug_print` | ~6 KB | NCP doesn't need debug prints |
| `zigbee_debug_basic` | ~3 KB | Debug library not needed |
| `zigbee_debug_extended` | ~3 KB | Extended debug not needed |
| `iostream_vuart` | ~1 KB | Virtual UART not needed |
| `zigbee_zll` | ~4 KB | Touchlink commissioning not needed |

### Network Parameters (Pre-configured)

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `EMBER_MAX_END_DEVICE_CHILDREN` | 32 | Support many battery devices |
| `EMBER_PACKET_BUFFER_COUNT` | 255 | Handle high traffic (max) |
| `EMBER_BINDING_TABLE_SIZE` | 32 | More automation bindings |
| `EMBER_BROADCAST_TABLE_SIZE` | 30 | Better broadcast handling |
| `EMBER_NEIGHBOR_TABLE_SIZE` | 26 | Larger mesh networks (max Series 1) |
| `EMBER_APS_UNICAST_MESSAGE_COUNT` | 32 | More concurrent messages |
| `EMBER_SOURCE_ROUTE_TABLE_SIZE` | 100 | Support 100+ devices |
| `EMBER_KEY_TABLE_SIZE` | 12 | Security keys |
| `NVM3_DEFAULT_NVM_SIZE` | 36864 | 36 KB persistent storage |

---

## main.c

Custom main function with RTL8196E boot delay:

```c
#include "sl_udelay.h"

int main(void) {
  // Wait for RTL8196E to initialize its UART
  sl_udelay_wait(1000000);  // 1 second delay

  sl_system_init();
  app_init();

  while (1) {
    sl_system_process_action();
    app_process_action();
  }
}
```

Without this delay, early EZSP messages from the EFR32 may be lost before the RTL8196E UART is ready.

---

## app.c

Minimal NCP callbacks:

```c
void emberAfRadioNeedsCalibratingCallback(void) {
  sl_mac_calibrate_current_channel();
}

void emberAfMainInitCallback(void) {
  // Empty - NCP initialization handled by framework
}
```

---

## Config Headers

### sl_iostream_usart_vcom_config.h

UART configuration for Lidl Gateway:

| Signal | Port | Pin | Location |
|--------|------|-----|----------|
| TX | PA0 | 0 | 0 |
| RX | PA1 | 1 | 0 |
| RTS | PA4 | 4 | 30 |
| CTS | PA5 | 5 | 30 |

Hardware flow control enabled (CTS/RTS).

### sl_rail_util_pti_config.h

PTI (Packet Trace Interface) disabled - not connected on gateway hardware.

---

## Firmware Size

With all optimizations applied:

| Section | Size |
|---------|------|
| .text (code) | ~201 KB |
| .data | ~1.5 KB |
| .bss | ~23 KB |
| **Total Flash** | **~206 KB** (fits in 256 KB) |
