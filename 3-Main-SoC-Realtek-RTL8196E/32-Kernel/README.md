# Linux 5.10 Kernel for RTL8196E

This directory contains everything needed to build a modern Linux kernel for the Realtek RTL8196E gateway.

**Current version**: [Linux 5.10.246](https://lwn.net/Articles/1043999/) (29 octobre 2025)

## Why Linux 5.10?

Linux 5.10 was chosen for its **[Super Long Term Support (SLTS)](https://wiki.linuxfoundation.org/civilinfrastructureplatform/start)**:
- Standard LTS: maintained until **December 2026**
- SLTS (via [Civil Infrastructure Platform](https://www.cip-project.org/)): maintained until **2030+**

This ensures the gateway will receive security updates for many years to come.

## The Porting Challenge

Porting from Linux 3.10 (Realtek SDK) to 5.10 was a **significant undertaking**:

| Challenge | Description |
|-----------|-------------|
| **Ethernet driver rewrite** | The original RTL8196E network driver used obsolete APIs. It had to be completely rewritten to comply with modern kernel networking standards (NAPI, phylib, etc.) |
| **Lexra CPU support** | The Lexra MIPS variant lacks `ll/sc` instructions. Required patches to kernel atomics and memory barriers |
| **Platform code cleanup** | Realtek SDK platform code was heavily tied to 3.10 internals. Rewrote board support using modern devicetree and platform driver APIs |
| **Build system changes** | Kernel build system changed significantly between 3.10 and 5.10, requiring Makefile and Kconfig adaptations |

The result is a clean, maintainable kernel that can be easily updated to newer 5.10.x releases.

## Contents

| Directory/File | Description |
|----------------|-------------|
| [`patches/`](patches/) | Patches to apply on vanilla Linux 5.10.246 |
| [`files/`](files/) | New files to add to the kernel tree (Realtek platform support) |
| [`config-5.10.246-realtek.txt`](files/config-5.10.246-realtek.txt) | Kernel configuration |
| [`build_kernel.sh`](build_kernel.sh) | Build script |

## Building

```bash
./build_kernel.sh [clean|menuconfig]
```

### Options

| Option | Description |
|--------|-------------|
| *(none)* | Normal build: download sources if needed, apply patches, compile |
| `clean` | Remove kernel source directory and rebuild from scratch |
| `menuconfig` | Run kernel menuconfig, with option to save config to `files/` |

### Build process

The script will:
1. Download Linux 5.10.246 source (if not present)
2. Apply all patches
3. Copy Realtek platform files
4. Compile the kernel
5. Create `kernel.img` ready to flash

**Requirements**: [Toolchain](../../1-Build-Environment/10-lexra-toolchain/) must be built first

## Output

- `kernel.img` — Flashable kernel image with Realtek header (~1.7 MB)

## Technical Details

### Build Process

1. Compile kernel → `vmlinux`
2. Strip to raw binary → `vmlinux.bin`
3. Compress with LZMA → `vmlinux.bin.lzma`
4. Prepend LZMA loader → `loader.bin + vmlinux.bin.lzma`
5. Add Realtek header → `kernel.img`

### Key Patches

- Lexra MIPS support (no ll/sc instructions)
- RTL8196E SoC and board support
- SPI flash driver
- Ethernet driver
- GPIO and LED support

## 🙏 Credits

The kernel patches and platform support were originally based on work by [Gaspare Bruno](https://github.com/ggbruno):
- [ggbruno/openwrt - Realtek branch](https://github.com/ggbruno/openwrt/tree/Realtek/target/linux/realtek)

These patches (originally developed for Linux 4.14) were significantly modified and rewritten for Linux 5.10.

## 🔗 References

- [Linux 5.10 LTS kernel](https://kernel.org/)
- [Civil Infrastructure Platform](https://www.cip-project.org/)
- [Lexra processors](https://en.wikipedia.org/wiki/Lexra)
