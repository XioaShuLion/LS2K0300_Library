#!/bin/sh

./busybox-3a5000 devmem 0x10010430 64 0x10000C0500FCCCF2

/sbin/rmmod sw_se_echip_drv
/sbin/rmmod wst_se_echip_drv
/sbin/insmod wst_se_echip_drv.ko
