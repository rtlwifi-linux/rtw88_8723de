# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

CONFIG_RTW88_CORE=m
CONFIG_RTW88_PCI=m
CONFIG_RTW88_8822BE=y
CONFIG_RTW88_8822CE=y
CONFIG_RTW88_8723DE=y
ccflags-y += -DCONFIG_RTW88_8822BE=y
ccflags-y += -DCONFIG_RTW88_8822CE=y
ccflags-y += -DCONFIG_RTW88_8723DE=y
ccflags-y += -DDEBUG
ccflags-y += -DCONFIG_RTW88_DEBUG=y
ccflags-y += -DCONFIG_RTW88_DEBUGFS=y

obj-$(CONFIG_RTW88_CORE)	+= rtw88.o
rtw88-y += main.o \
	   mac80211.o \
	   util.o \
	   debug.o \
	   tx.o \
	   rx.o \
	   mac.o \
	   phy.o \
	   coex.o \
	   efuse.o \
	   fw.o \
	   ps.o \
	   sec.o \
	   bf.o \
	   wow.o \
	   sar.o \
	   regd.o

rtw88-$(CONFIG_RTW88_8822BE)	+= rtw8822b.o rtw8822b_table.o
rtw88-$(CONFIG_RTW88_8822CE)	+= rtw8822c.o rtw8822c_table.o
rtw88-$(CONFIG_RTW88_8723DE)	+= rtw8723d.o rtw8723d_table.o

obj-$(CONFIG_RTW88_PCI)		+= rtwpci.o
rtwpci-objs			:= pci.o

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)
