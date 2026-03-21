# cvimg - RTL Firmware Image Tool

Version: 2.1.0
Build: 2025-07-05 11:58:49

## Usage

```
Usage: cvimg [OPTIONS...]

Options:
  -i <file>         Input payload file
  -o <file>         Output file
  -e <addr>         Start/load address (hex)
  -b <addr>         Flash burn address (hex)
  -s <sig>          Custom signature (4 characters, overrides -t and -c)
  -t <type>         Payload type: boot, kernel, rootfs, fw
  -c <cpu>          CPU type: any, rtl8196b, new, other
  -a <size>         Output alignment size (supports K, M suffixes)
  -f                Append fake rootfs only (requires -a)
  -j                Append JFFS2 end marker (requires -a and -b)
  -h                Show this help

Examples:
  cvimg -i kernel.bin -o kernel.img -e 0x80000000 -b 0x20000 -t kernel -c new
  cvimg -i rootfs.squashfs -o rootfs.img -e 0x80800000 -b 0x120000 -t rootfs
  cvimg -i firmware.bin -o test.img -f -a 64k
```
