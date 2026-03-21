// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * eth.c - Ethernet interface driver
 *
 * RTL8196E stage-2 bootloader
 *
 * Copyright (c) 2009-2020 Realtek Semiconductor Corp.
 * Copyright (c) 2024-2026 J. Nilo
 */

#include <linux/interrupt.h>
#include <stdlib.h>
#include "boot_common.h"
#include "boot_soc.h"
#include "boot_net.h"
#include "nic.h"
#include "eth.h"
#include <rtl_types.h>
#include <rtl8196x/swCore.h>
#include <rtl8196x/swNic_poll.h>
#include <rtl8196x/vlanTable.h>

#define BUF_OFFSET 4  // descriptor offset of data BUFFER
#define DATA_OFFSET 2 // real data offset of Rx packet in buffer
#define NUM_DESC 2    // 16//64 //wei del
#define BUF_SIZE 1600 // Byte Counts

typedef struct {
	unsigned long StsLen;
	unsigned long DataPtr;
	unsigned long VLan;
	unsigned long Reserved;
} desc_t;

struct statistics {
	unsigned int txpkt;
	unsigned int rxpkt;
	unsigned int txerr;
	unsigned int rxerr;
	unsigned int rxffov;
};

struct eth_private {
	unsigned int nr;
	unsigned int io_addr;
	unsigned int irq;
	unsigned int num_desc;
	unsigned long rx_descaddr;
	unsigned long tx_descaddr;
	unsigned long tx_skbaddr[NUM_DESC];
	unsigned long rx_skbaddr[NUM_DESC];
	struct statistics res;
	unsigned int cur_rx;
	unsigned int cur_tx;
};

void eth_interrupt(int irq, void *dev_id, struct pt_regs *regs);
void eth_polltx(int etherport);
void SetOwnByNic(unsigned long *header, int len, int own, int index);
void prepare_txpkt(int etherport, unsigned short type, unsigned char *destaddr,
		   unsigned char *data, unsigned short len);

extern int32 swCore_init(void);
extern int flashread(unsigned long dst, unsigned int src, unsigned long length);

char eth0_mac[6] = {0x56, 0xaa, 0xa5, 0x5a, 0x7d, 0xe8};

static unsigned char ETH0_tx_buf[NUM_DESC][BUF_SIZE];
static int ETH0_IRQ = 15; // wei add for 8196 sw
static struct eth_private ETH[2];
static struct irqaction irq_eth15 = {eth_interrupt, 0, 15, "eth0", NULL, NULL};

static int checksum_ok(const unsigned char *buf, unsigned short len)
{
	unsigned char sum = 0;
	unsigned short i;

	if (!len)
		return 0;
	for (i = 0; i < len; i++)
		sum += buf[i];
	return sum == 0;
}

static int read_setting_block(unsigned int offset, unsigned short max_len,
			      unsigned char magic, unsigned char **out_buf,
			      unsigned short *out_len)
{
	unsigned char header[6];
	unsigned short len;
	unsigned char *buf;

	if (flashread((unsigned long)header, offset, 6) == 0)
		return 0;
	if (header[0] != magic)
		return 0;
	memcpy(&len, &header[4], 2);
	if (len > max_len)
		return 0;
	buf = (unsigned char *)malloc(len);
	if (!buf)
		return 0;
	flashread((unsigned long)buf, offset + 6, len);
	if (!checksum_ok(buf, len)) {
		free(buf);
		return 0;
	}
	*out_buf = buf;
	*out_len = len;
	return 1;
}

static int mac_is_valid(const unsigned char *mac)
{
	return memcmp(mac, "\x0\x0\x0\x0\x0\x0", 6) && !(mac[0] & 0x1);
}

static int ip_is_valid(const unsigned char *ip)
{
	return memcmp(ip, "\x0\x0\x0\x0", 4) &&
	       !(ip[3] == 0xFF || ip[3] == 0x0);
}

void eth_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int status = *(volatile unsigned long *)(0xb801002c);
	*(volatile unsigned long *)(0xb801002c) = status;
	nic.packetlen = 0;
	while (swNic_receive((void **)&nic.packet, &nic.packetlen) == 0) {
		{
			swNic_txDone();
			kick_tftpd();
			nic.packetlen = 0;
		}
	}
	swNic_txDone();
}
//---------------------------------------------------------------------------------------

void gethwmac(unsigned char *mac)
{
	unsigned char *buf;
	unsigned short len;

	if (!read_setting_block(HW_SETTING_OFFSET, 0x2000, 'h', &buf, &len))
		return;
	memcpy(mac, buf + HW_NIC0_MAC_OFFSET, 6);
	if (!mac_is_valid(mac))
		memset(mac, 0x0, 6);
	free(buf);
}

void getmacandip(unsigned char *mac, unsigned char *ip)
{
	unsigned char *buf;
	unsigned short len;
	int currSettingMaxLen = 0x4000;

	if (!read_setting_block(CURRENT_SETTING_OFFSET, currSettingMaxLen, '6',
				&buf, &len))
		return;

	memcpy(ip, buf + CURRENT_IP_ADDR_OFFSET, 4);
	memcpy(mac, buf + CURRENT_ELAN_MAC_OFFSET, 6);

	if (ip_is_valid(ip)) {
		if (!mac_is_valid(mac))
			gethwmac(mac);
		if (memcmp(ip, "\xC0\xA8\x0\x1", 4) != 0) {
			/* different ip with 192.168.0.1, MUST use different
			 * MAC */
			eth0_mac[0] = 0x56;
			eth0_mac[1] = 0xaa;
			eth0_mac[2] = 0xa5;
			eth0_mac[3] = 0x5a;
			eth0_mac[4] = 0x7d;
			eth0_mac[5] = 0xe8;
		} else {
			/* same ip with 192.168.0.1, so use the same mac */
			gethwmac(eth0_mac);
		}
	} else {
		/*use hard code 192.168.1.6*/
		memset(ip, 0x0, 4);
	}
	free(buf);
}

/**
 * eth_startup - Initialize the Ethernet subsystem for TFTP recovery
 * @etherport: port number (unused, always 0)
 *
 * Reads MAC/IP from flash settings, initializes the switch core and
 * NIC descriptor rings, creates the VLAN and network interface, and
 * registers the Ethernet interrupt handler.
 */
void eth_startup(int etherport)
{
	unsigned long val;
	/*read current-setting MAC/IP to update eth0_mac side-effect*/
	unsigned char tmp_mac[6] = {0};
	unsigned char tmp_ip[4] = {0};
	getmacandip(tmp_mac, tmp_ip);

	if (swCore_init()) {
		dprintf("\nSwitch core initialization failed!\n");
		return;
	}

	// avoid download bin checksum error
	uint32 rx[6] = {4, 0, 0, 0, 0, 0};
	uint32 tx[4] = {4, 2, 2, 2};

	/* Initialize NIC module */
	if (swNic_init(rx, 4, tx, MBUF_LEN)) {
		dprintf("\nSwitch nic initialization failed!\n");
		return;
	}

	rtl_vlan_param_t vp;
	int32 ret;
	rtl_netif_param_t np;
	rtl_acl_param_t ap;

	/* Create Netif */
	memset((void *)&np, 0, sizeof(rtl_netif_param_t));
	np.vid = 8;
	np.valid = 1;
	np.enableRoute = 0;
	np.inAclEnd = 0;
	np.inAclStart = 0;
	np.outAclEnd = 0;
	np.outAclStart = 0;
	memcpy(&np.gMac, &eth0_mac[0], 6);
	np.macAddrNumber = 1;
	np.mtu = 1500;
	ret = swCore_netifCreate(0, &np);
	if (ret != 0) {
		printf("Creating intif fails:%d\n", ret);
		return;
	}

	/* Create vlan */
	memset((void *)&vp, 0, sizeof(rtl_vlan_param_t));
	vp.egressUntag = ALL_PORT_MASK;
	vp.memberPort = ALL_PORT_MASK;
	ret = swCore_vlanCreate(8, &vp);
	if (ret != 0) {
		printf("Creating vlan fails:%d\n", ret);
		return;
	}

	/* Set interrupt routing register */

	REG32(IRR1_REG) |= (3 << 28);

	request_IRQ(ETH0_IRQ, &irq_eth15, &(ETH[0]));
}

//----------------------------------------------------------------------------------------
/*Just a start address, and the data length*/
void prepare_txpkt(int etherport, unsigned short type, unsigned char *destaddr,
		   unsigned char *data, unsigned short len)
{
	char *tx_buffer = &ETH0_tx_buf[0][0];
	unsigned short nstype;
	int Length = len;

	memcpy(tx_buffer, destaddr, 6);

	/*Source Address*/
	memcpy(tx_buffer + 6, eth0_mac, 6);

	/*Payload type*/
	nstype = htons(type);
	memcpy(tx_buffer + 12, (unsigned char *)&nstype, 2);

	/*Payload */
	memcpy(tx_buffer + 14, (unsigned char *)data, Length);
	Length += 14;

	swNic_send(tx_buffer, Length);
}
