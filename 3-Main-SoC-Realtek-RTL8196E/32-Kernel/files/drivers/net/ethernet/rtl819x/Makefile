# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Realtek RTL819x Ethernet driver
#

obj-$(CONFIG_RTL819X) += rtl819x.o

rtl819x-objs := rtl_nic.o \
                rtl865xc_swNic.o \
                common/rtl865x_netif.o \
                common/rtl865x_vlan.o \
                common/rtl865x_eventMgr.o \
                AsicDriver/rtl865x_asicCom.o \
                AsicDriver/rtl865x_asicL2.o \
                AsicDriver/rtl865x_asicBasic.o \
                l2Driver/rtl865x_fdb.o

# Include path for driver headers
ccflags-y += -I$(src)/include

# MIPS flags for RLX4181: only strict-aliasing disabled for legacy code
subdir-ccflags-y += -fno-strict-aliasing
