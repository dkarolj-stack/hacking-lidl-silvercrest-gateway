# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_DTB_RTL8196E_GEN)	+= rtl8196e.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always-y			:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
