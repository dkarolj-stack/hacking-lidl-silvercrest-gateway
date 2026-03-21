# zigbeed - Zigbee Stack Daemon (EmberZNet 7.5.1)

> **Note**: This builds EmberZNet 7.5.1 (EZSP 13). For EmberZNet 8.2.2 (EZSP 18),
> use `../zigbeed-8.2.2/` instead (recommended).

Build script for zigbeed from Gecko SDK 4.5.0.
Portable: works on x86_64, ARM64 (Raspberry Pi 4/5), ARM32.

## Prerequisites

1. **Gecko SDK 4.5.0** installed via `1-Build-Environment/`
2. **cpcd** installed (provides libcpc)

```bash
# Install cpcd first
cd ../cpcd && ./build_cpcd.sh
```

## Build and Install

```bash
./build_zigbeed.sh         # Build and install to /usr/local/bin
./build_zigbeed.sh clean   # Clean build directory
```

## Architecture Support

| Architecture | `uname -m` | SDK libs |
|--------------|------------|----------|
| PC 64-bit | x86_64 | x86-64 |
| Raspberry Pi 4/5 64-bit | aarch64 | arm64v8 |
| Raspberry Pi 32-bit | armv7l | arm32v7 |

The script auto-detects your architecture.

## Usage

```bash
zigbeed -p 9999    # Listen on TCP port 9999
```

## Zigbee2MQTT Configuration

```yaml
serial:
  port: tcp://localhost:9999
  adapter: ember
```
