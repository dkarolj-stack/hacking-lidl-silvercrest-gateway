// Flash layout and scan ranges for RTL8196E bootloader
#ifndef _FLASH_LAYOUT_H_
#define _FLASH_LAYOUT_H_

// Primary kernel image slots (flash offsets)
#define CODE_IMAGE_OFFSET (64 * 1024)   // 0x10000
#define CODE_IMAGE_OFFSET2 (128 * 1024) // 0x20000
#define CODE_IMAGE_OFFSET3 (192 * 1024) // 0x30000
#define CODE_IMAGE_OFFSET4 (0x8000)

// Root filesystem offsets (flash offsets)
#define ROOT_FS_OFFSET (0xE0000)
#define ROOT_FS_OFFSET_OP1 (0x10000)
#define ROOT_FS_OFFSET_OP2 (0x40000)

// Scan ranges for kernel images (flash offsets)
#define CONFIG_LINUX_IMAGE_OFFSET_START 0x00020000
#define CONFIG_LINUX_IMAGE_OFFSET_END 0x01000000
#define CONFIG_LINUX_IMAGE_OFFSET_STEP 0x00010000

// Scan ranges for rootfs images (flash offsets)
#define CONFIG_ROOT_IMAGE_OFFSET_START 0x00200000
#define CONFIG_ROOT_IMAGE_OFFSET_END 0x01000000
#define CONFIG_ROOT_IMAGE_OFFSET_STEP 0x00010000

// Skip rootfs scan (Linux will locate rootfs)
#define SKIP_ROOTFS_SCAN 1

#endif /* _FLASH_LAYOUT_H_ */
