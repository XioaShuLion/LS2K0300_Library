/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LOONGSON LSCANFD controller
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef __LSCANFD_H__
#define __LSCANFD_H__

#include <linux/netdevice.h>
#include <linux/can/dev.h>
#include <linux/list.h>

enum lscanfd_can_registers;

typedef struct ls_frame {
	uint8_t  skb_id;
#define FRAME_EMPTY		(0xff)
	uint32_t meta0;
	uint32_t meta1;
	uint8_t  data[64];
	uint8_t  len;
	uint8_t  can_id;
} ls_frame_t;

typedef struct ls_frame_buf {
	uint64_t in;
	uint64_t out;
	size_t   buf_size;
#define ls_frame_buf_reset(buf)		((buf)->in = (buf)->out = 0)
#define ls_frame_buf_len(buf)		((buf)->in - (buf)->out)
#define ls_frame_buf_is_empty(buf)	((buf)->in == (buf)->out)
#define ls_frame_buf_is_full(buf)	(ls_frame_buf_len(buf) > ((buf)->buf_size - 1))

	ls_frame_t *frame_bufs;
} ls_frame_buf_t;

struct lscanfd_priv {
	struct can_priv can; /* must be first member! */
	void __iomem *mem_base;
	volatile u8 read_idx;
	u16 last_res;
	volatile u8 txtb_flags;
	bool canfd_dmarx;
	uint32_t ntxbufs;
	ls_frame_buf_t tx_frame_buf;
	spinlock_t tx_lock; /* spinlock to serialize allocation and processing of TX buffers */

	struct napi_struct napi;
	struct device *dev;
	struct clk *can_clk;

	int irq_flags;
	struct dma_chan *rx_ch;  /* dma rx channel            */
	dma_addr_t rx_dma_buf;   /* dma rx buffer bus address */
	unsigned int *rx_buf;   /* dma rx buffer cpu address */
	resource_size_t	 mapbase;		/* for ioremap */
	resource_size_t	 mapsize;

#define HW_IFFO_NUM		(8)
	u8 hwfifo_skb_id[HW_IFFO_NUM];
};

int lscanfd_probe_common(struct device *dev, void __iomem *addr, resource_size_t mapbase,
			int irq, unsigned int ntxbufs,
			unsigned long can_clk_rate, bool canfd_dmarx,
			void (*set_drvdata_fnc)(struct device *dev,
						struct net_device *ndev));
#endif /*__LSCANFD__*/
