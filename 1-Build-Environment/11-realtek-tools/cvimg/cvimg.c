/*
 * Copyright (C) 2017 Weijie Gao <hackpascal@gmail.com>
 *
 * Based on:
 * Tool to convert ELF image to be the AP downloadable binary.
 * Copyright (C) 2009 David Hsu <davidhsu@realtek.com.tw>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define SIGNATURE_LEN 4
#define MAX_FILE_SIZE (100 * 1024 * 1024) /* 100MB max */
#define MIN_ALIGN_SIZE 16
#define MAX_SIGNATURE_STR 5 /* 4 chars + null terminator */

#define FAKE_ROOTFS_SUPER_SIZE 640
#define FAKE_ROOTFS_CHKSUM_SIZE 2
#define FAKE_ROOTFS_SIZE (FAKE_ROOTFS_SUPER_SIZE + FAKE_ROOTFS_CHKSUM_SIZE)

#define FAKE_ROOTFS_MAGIC "hsqs"
#define FAKE_ROOTFS_IDENT "FAKE"
#define FAKE_ROOTFS_ALIGNMENT 4096

#define JFFS2_END_MARKER 0xdeadc0de
#define JFFS2_MARKER_SIZE 4

/* Error codes */
#define ERR_INVALID_ARG -1
#define ERR_FILE_ACCESS -2
#define ERR_MEMORY -3
#define ERR_FILE_TOO_LARGE -4
#define ERR_INVALID_SIGNATURE -5

enum cpu_type {
  CPU_ANY = 0,
  CPU_RTL8196B,
  CPU_NEW,
  CPU_OTHERS,
  CPU_TYPE_MAX,
};

enum data_type {
  DATA_BOOT = 0,
  DATA_KERNEL,
  DATA_ROOTFS,
  DATA_FIRMWARE, /* kernel + rootfs */
  DATA_TYPE_MAX,
};

struct img_header {
  unsigned char signature[SIGNATURE_LEN];
  uint32_t start_addr;
  uint32_t burn_addr;
  uint32_t len;
} __attribute__((packed));

struct sig_info {
  enum data_type type;
  enum cpu_type cpu;
  char sig[SIGNATURE_LEN + 1]; /* +1 for null terminator */
};

/* Configuration structure for better organization */
struct config {
  char *input_file;
  char *output_file;
  uint32_t start_addr;
  uint32_t burn_addr;
  char *signature;
  enum data_type data_type;
  enum cpu_type cpu_type;
  uint32_t align_size;
  int append_fake_rootfs;
  int append_jffs2_endmarker;
};

/* Known signatures */
static const struct sig_info sig_known[] = {
    {.type = DATA_BOOT, .cpu = CPU_ANY, .sig = "boot"},
    {.type = DATA_KERNEL, .cpu = CPU_RTL8196B, .sig = "cs6b"},
    {.type = DATA_KERNEL, .cpu = CPU_NEW, .sig = "cs6c"},
    {.type = DATA_FIRMWARE, .cpu = CPU_NEW, .sig = "cr6c"},
    {.type = DATA_ROOTFS, .cpu = CPU_NEW, .sig = "sqsh"},
    {.type = DATA_ROOTFS, .cpu = CPU_OTHERS, .sig = "hsqs"},
};

static char *progname;

/* Utility functions */
static inline uint32_t size_aligned(uint32_t size, uint32_t align) {
  if (align <= 1)
    return size;

  uint32_t remainder = size % align;
  return remainder ? size + align - remainder : size;
}

static int validate_file_size(const char *filename, size_t *size) {
  struct stat status;

  if (stat(filename, &status) < 0) {
    fprintf(stderr, "Error: Can't stat file '%s': %s\n", filename,
            strerror(errno));
    return ERR_FILE_ACCESS;
  }

  if (status.st_size <= 0) {
    fprintf(stderr, "Error: File '%s' is empty\n", filename);
    return ERR_INVALID_ARG;
  }

  if (status.st_size > MAX_FILE_SIZE) {
    fprintf(stderr, "Error: File '%s' too large (%lld bytes, max %d)\n",
            filename, (long long)status.st_size, MAX_FILE_SIZE);
    return ERR_FILE_TOO_LARGE;
  }

  *size = status.st_size;
  return 0;
}

static uint16_t calculate_checksum(const uint8_t *buf, size_t len) {
  if (!buf || len == 0)
    return 0;

  uint16_t sum = 0;

  /* Process pairs of bytes */
  for (size_t i = 0; i < (len / 2) * 2; i += 2) {
    uint16_t tmp = (((uint16_t)buf[i]) << 8) | buf[i + 1];
    sum += tmp;
  }

  /* Handle odd byte */
  if (len % 2) {
    uint16_t tmp = ((uint16_t)buf[len - 1]) << 8;
    sum += tmp;
  }

  return htons(~sum + 1);
}

static enum cpu_type resolve_cpu_type(const char *cpu_str) {
  if (!cpu_str)
    return CPU_TYPE_MAX;

  if (!strcmp(cpu_str, "any"))
    return CPU_ANY;
  else if (!strcmp(cpu_str, "rtl8196b"))
    return CPU_RTL8196B;
  else if (!strcmp(cpu_str, "new"))
    return CPU_NEW;
  else if (!strcmp(cpu_str, "other"))
    return CPU_OTHERS;

  return CPU_TYPE_MAX; /* Invalid type */
}

static enum data_type resolve_data_type(const char *data_str) {
  if (!data_str)
    return DATA_TYPE_MAX;

  if (!strcmp(data_str, "boot"))
    return DATA_BOOT;
  else if (!strcmp(data_str, "kernel"))
    return DATA_KERNEL;
  else if (!strcmp(data_str, "rootfs"))
    return DATA_ROOTFS;
  else if (!strcmp(data_str, "fw"))
    return DATA_FIRMWARE;

  return DATA_TYPE_MAX; /* Invalid type */
}

static const char *get_signature(const struct config *cfg) {
  if (cfg->signature)
    return cfg->signature;

  for (size_t i = 0; i < ARRAY_SIZE(sig_known); i++) {
    if ((cfg->data_type == sig_known[i].type) &&
        (cfg->cpu_type == sig_known[i].cpu))
      return sig_known[i].sig;
  }

  return NULL;
}

static int validate_signature(const char *sig) {
  if (!sig) {
    fprintf(stderr, "Error: Signature is NULL\n");
    return ERR_INVALID_SIGNATURE;
  }

  size_t len = strlen(sig);
  if (len != SIGNATURE_LEN) {
    fprintf(stderr, "Error: Invalid signature '%s' (length should be %u)\n",
            sig, (unsigned int)SIGNATURE_LEN);
    return ERR_INVALID_SIGNATURE;
  }

  /* Check for printable characters only */
  for (size_t i = 0; i < len; i++) {
    if (!isprint((unsigned char)sig[i])) {
      fprintf(stderr, "Error: Invalid character in signature '%s'\n", sig);
      return ERR_INVALID_SIGNATURE;
    }
  }

  return 0;
}

static FILE *safe_fopen(const char *filename, const char *mode) {
  FILE *fp = fopen(filename, mode);
  if (!fp) {
    fprintf(stderr, "Error: Can't open file '%s' in mode '%s': %s\n", filename,
            mode, strerror(errno));
  }
  return fp;
}

static int safe_fread(void *ptr, size_t size, size_t nmemb, FILE *stream,
                      const char *filename) {
  size_t read_bytes = fread(ptr, size, nmemb, stream);
  if (read_bytes != nmemb) {
    if (feof(stream)) {
      fprintf(stderr, "Error: Unexpected end of file '%s'\n", filename);
    } else {
      fprintf(stderr, "Error: Read failed on file '%s': %s\n", filename,
              strerror(errno));
    }
    return ERR_FILE_ACCESS;
  }
  return 0;
}

static int safe_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream,
                       const char *filename) {
  size_t written_bytes = fwrite(ptr, size, nmemb, stream);
  if (written_bytes != nmemb) {
    fprintf(stderr, "Error: Write failed on file '%s': %s\n", filename,
            strerror(errno));
    return ERR_FILE_ACCESS;
  }
  return 0;
}

static int build_image(const struct config *cfg) {
  size_t fsize;
  uint8_t *buf = NULL;
  FILE *fp = NULL;
  int ret = ERR_INVALID_ARG;

  /* Validate input */
  ret = validate_file_size(cfg->input_file, &fsize);
  if (ret != 0)
    goto out;

  const char *sig = get_signature(cfg);
  ret = validate_signature(sig);
  if (ret != 0)
    goto out;

  /* Calculate sizes */
  uint32_t fsize_aligned = size_aligned(fsize, sizeof(uint16_t));
  uint32_t bufsize =
      size_aligned(fsize_aligned + sizeof(struct img_header) + sizeof(uint16_t),
                   cfg->align_size);

  buf = calloc(1, bufsize);
  if (!buf) {
    fprintf(stderr, "Error: Memory allocation failed for %u bytes\n", bufsize);
    ret = ERR_MEMORY;
    goto out;
  }

  /* Populate header */
  struct img_header header;
  memcpy(header.signature, sig, SIGNATURE_LEN);
  header.start_addr = htonl(cfg->start_addr);
  header.burn_addr = htonl(cfg->burn_addr);
  header.len = htonl(fsize_aligned + sizeof(uint16_t));

  /* Read payload data */
  fp = safe_fopen(cfg->input_file, "rb");
  if (!fp) {
    ret = ERR_FILE_ACCESS;
    goto out;
  }

  ret = safe_fread(buf + sizeof(header), 1, fsize, fp, cfg->input_file);
  if (ret != 0)
    goto out;

  fclose(fp);
  fp = NULL;

  /* Calculate and append checksum */
  uint16_t chksum = calculate_checksum(buf + sizeof(header), fsize_aligned);
  memcpy(buf + sizeof(header) + fsize_aligned, &chksum, sizeof(chksum));

  /* Write header */
  memcpy(buf, &header, sizeof(header));

  /* Write to output file */
  fp = safe_fopen(cfg->output_file, "wb");
  if (!fp) {
    ret = ERR_FILE_ACCESS;
    goto out;
  }

  ret = safe_fwrite(buf, 1, bufsize, fp, cfg->output_file);
  if (ret != 0)
    goto out;

  printf("Image generated successfully:\n"
         "  Input file:\t\t'%s'\n"
         "  Output file:\t\t'%s'\n"
         "  Start address:\t0x%08x\n"
         "  Burn address:\t\t0x%08x\n"
         "  Signature:\t\t'%s'\n"
         "  Payload size (raw):\t%zu bytes\n"
         "  Payload size (aligned):%u bytes\n"
         "  Image size (inc. hdr):\t%u bytes\n"
         "  Checksum:\t\t0x%04x\n",
         cfg->input_file, cfg->output_file, cfg->start_addr, cfg->burn_addr,
         sig, fsize, fsize_aligned,
         ntohl(header.len) + (uint32_t)sizeof(header), ntohs(chksum));

  ret = 0;

out:
  if (fp)
    fclose(fp);
  if (buf)
    free(buf);

  return ret;
}

static int image_append_fake_rootfs(const struct config *cfg) {
  size_t fsize;
  uint8_t *buf = NULL;
  FILE *fp = NULL;
  int ret = ERR_INVALID_ARG;

  uint32_t align_size = cfg->align_size;
  if (align_size < MIN_ALIGN_SIZE)
    align_size = FAKE_ROOTFS_ALIGNMENT;

  ret = validate_file_size(cfg->input_file, &fsize);
  if (ret != 0)
    goto out;

  uint32_t fake_rootfs_size = FAKE_ROOTFS_SIZE;
  uint32_t bufsize = size_aligned(fsize + fake_rootfs_size, align_size);

  buf = calloc(1, bufsize);
  if (!buf) {
    fprintf(stderr, "Error: Memory allocation failed for %u bytes\n", bufsize);
    ret = ERR_MEMORY;
    goto out;
  }

  /* Read input data */
  fp = safe_fopen(cfg->input_file, "rb");
  if (!fp) {
    ret = ERR_FILE_ACCESS;
    goto out;
  }

  ret = safe_fread(buf, 1, fsize, fp, cfg->input_file);
  if (ret != 0)
    goto out;

  fclose(fp);
  fp = NULL;

  /* Append fake rootfs */
  size_t offset = fsize;
  memcpy(buf + offset, FAKE_ROOTFS_MAGIC, SIGNATURE_LEN);
  offset += SIGNATURE_LEN;

  memcpy(buf + offset, FAKE_ROOTFS_IDENT, SIGNATURE_LEN);
  offset += SIGNATURE_LEN;

  /* Calculate checksum for the padding area */
  uint16_t chksum =
      calculate_checksum(buf + fsize + SIGNATURE_LEN * 2,
                         FAKE_ROOTFS_SUPER_SIZE - SIGNATURE_LEN * 2);
  memcpy(buf + fsize + FAKE_ROOTFS_SUPER_SIZE, &chksum, sizeof(chksum));

  /* Write to output file */
  fp = safe_fopen(cfg->output_file, "wb");
  if (!fp) {
    ret = ERR_FILE_ACCESS;
    goto out;
  }

  ret = safe_fwrite(buf, 1, bufsize, fp, cfg->output_file);
  if (ret != 0)
    goto out;

  printf("Fake rootfs appended successfully:\n"
         "  Input file:\t\t'%s'\n"
         "  Output file:\t\t'%s'\n"
         "  Fake rootfs size:\t%u bytes\n"
         "  Image size:\t\t%u bytes\n",
         cfg->input_file, cfg->output_file, fake_rootfs_size, bufsize);

  ret = 0;

out:
  if (fp)
    fclose(fp);
  if (buf)
    free(buf);

  return ret;
}

static int image_append_jffs2_endmarker(const struct config *cfg) {
  size_t fsize;
  uint8_t *buf = NULL;
  FILE *fp = NULL;
  int ret = ERR_INVALID_ARG;

  uint32_t align_size = cfg->align_size;
  if (align_size < MIN_ALIGN_SIZE)
    align_size = FAKE_ROOTFS_ALIGNMENT;

  ret = validate_file_size(cfg->input_file, &fsize);
  if (ret != 0)
    goto out;

  uint32_t bufsize = size_aligned(fsize + JFFS2_MARKER_SIZE, align_size);

  buf = calloc(1, bufsize);
  if (!buf) {
    fprintf(stderr, "Error: Memory allocation failed for %u bytes\n", bufsize);
    ret = ERR_MEMORY;
    goto out;
  }

  /* Read input data */
  fp = safe_fopen(cfg->input_file, "rb");
  if (!fp) {
    ret = ERR_FILE_ACCESS;
    goto out;
  }

  ret = safe_fread(buf, 1, fsize, fp, cfg->input_file);
  if (ret != 0)
    goto out;

  fclose(fp);
  fp = NULL;

  /* Append JFFS2 end marker */
  uint32_t marker = htonl(JFFS2_END_MARKER);
  memcpy(buf + fsize, &marker, JFFS2_MARKER_SIZE);

  /* Write to output file */
  fp = safe_fopen(cfg->output_file, "wb");
  if (!fp) {
    ret = ERR_FILE_ACCESS;
    goto out;
  }

  ret = safe_fwrite(buf, 1, bufsize, fp, cfg->output_file);
  if (ret != 0)
    goto out;

  printf("JFFS2 end marker appended successfully:\n"
         "  Input file:\t\t'%s'\n"
         "  Output file:\t\t'%s'\n"
         "  Image size:\t\t%u bytes\n",
         cfg->input_file, cfg->output_file, bufsize);

  ret = 0;

out:
  if (fp)
    fclose(fp);
  if (buf)
    free(buf);

  return ret;
}

static int validate_config(const struct config *cfg) {
  const char *sig = get_signature(cfg);

  if (!cfg->input_file || !cfg->output_file) {
    fprintf(stderr, "Error: Missing input/output file\n");
    return ERR_INVALID_ARG;
  }

  if (!sig) {
    fprintf(stderr, "Error: Unknown signature, please specify one with '-s' or "
                    "provide known data/cpu type\n");
    return ERR_INVALID_ARG;
  }

  if (cfg->append_fake_rootfs && cfg->append_jffs2_endmarker) {
    fprintf(stderr, "Error: '-f' and '-j' options conflict\n");
    return ERR_INVALID_ARG;
  }

  if (cfg->append_fake_rootfs || cfg->append_jffs2_endmarker) {
    if (!cfg->align_size) {
      fprintf(stderr, "Error: Please specify a block size using '-a'\n");
      return ERR_INVALID_ARG;
    }
    if (cfg->append_jffs2_endmarker && !cfg->burn_addr) {
      fprintf(stderr, "Error: Please specify firmware burn address using '-b' "
                      "for JFFS2 end marker\n");
      return ERR_INVALID_ARG;
    }
  } else {
    if (!cfg->start_addr || !cfg->burn_addr) {
      if (!(strcmp(sig, "boot") == 0 && cfg->start_addr == 0 &&
            cfg->burn_addr == 0)) {
        fprintf(stderr, "Error: Missing start/burn address\n");
        return ERR_INVALID_ARG;
      }
    }
  }

  return 0;
}

static void usage(int exit_code) {
  printf("Usage: %s [OPTIONS...]\n\n"
         "Options:\n"
         "  -i <file>         Input payload file\n"
         "  -o <file>         Output file\n"
         "  -e <addr>         Start/load address (hex)\n"
         "  -b <addr>         Flash burn address (hex)\n"
         "  -s <sig>          Custom signature (4 characters, overrides -t and "
         "-c)\n"
         "  -t <type>         Payload type: boot, kernel, rootfs, fw\n"
         "  -c <cpu>          CPU type: any, rtl8196b, new, other\n"
         "  -a <size>         Output alignment size (supports K, M suffixes)\n"
         "  -f                Append fake rootfs only (requires -a)\n"
         "  -j                Append JFFS2 end marker (requires -a and -b)\n"
         "  -h                Show this help\n\n"
         "Examples:\n"
         "  %s -i kernel.bin -o kernel.img -e 0x80000000 -b 0x20000 -t kernel "
         "-c new\n"
         "  %s -i rootfs.squashfs -o rootfs.img -e 0x80800000 -b 0x120000 -t "
         "rootfs\n"
         "  %s -i firmware.bin -o test.img -f -a 64k\n",
         progname, progname, progname, progname);
  exit(exit_code);
}

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;
  struct config cfg = {
      .data_type = DATA_FIRMWARE,
      .cpu_type = CPU_NEW,
      .align_size = 0,
      .append_fake_rootfs = 0,
      .append_jffs2_endmarker = 0,
  };

  progname = basename(argv[0]);

  if (argc < 2)
    usage(EXIT_SUCCESS);

  while (1) {
    int c = getopt(argc, argv, "a:b:c:e:i:o:s:t:fhj");
    if (c == -1)
      break;

    switch (c) {
    case 'a': {
      char *unit;
      cfg.align_size = strtoul(optarg, &unit, 0);
      if (unit && *unit) {
        switch (tolower(*unit)) {
        case 'b':
          break;
        case 'k':
          cfg.align_size <<= 10;
          break;
        case 'm':
          cfg.align_size <<= 20;
          break;
        default:
          fprintf(stderr, "Error: Invalid size unit '%c'\n", *unit);
          return EXIT_FAILURE;
        }
      }
      break;
    }
    case 'b':
      cfg.burn_addr = strtoul(optarg, NULL, 0);
      break;
    case 'c':
      cfg.cpu_type = resolve_cpu_type(optarg);
      if (cfg.cpu_type == CPU_TYPE_MAX) {
        fprintf(stderr, "Error: Unknown CPU type '%s'\n", optarg);
        return EXIT_FAILURE;
      }
      break;
    case 'e':
      cfg.start_addr = strtoul(optarg, NULL, 0);
      break;
    case 'i':
      cfg.input_file = optarg;
      break;
    case 'o':
      cfg.output_file = optarg;
      break;
    case 's':
      cfg.signature = optarg;
      break;
    case 't':
      cfg.data_type = resolve_data_type(optarg);
      if (cfg.data_type == DATA_TYPE_MAX) {
        fprintf(stderr, "Error: Unknown data type '%s'\n", optarg);
        return EXIT_FAILURE;
      }
      break;
    case 'f':
      cfg.append_fake_rootfs = 1;
      break;
    case 'j':
      cfg.append_jffs2_endmarker = 1;
      break;
    case 'h':
      usage(EXIT_SUCCESS);
      break;
    default:
      usage(EXIT_FAILURE);
      break;
    }
  }

  /* Validate configuration */
  ret = validate_config(&cfg);
  if (ret != 0)
    return EXIT_FAILURE;

  /* Execute requested operation */
  if (cfg.append_fake_rootfs)
    ret = image_append_fake_rootfs(&cfg);
  else if (cfg.append_jffs2_endmarker)
    ret = image_append_jffs2_endmarker(&cfg);
  else
    ret = build_image(&cfg);

  return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
