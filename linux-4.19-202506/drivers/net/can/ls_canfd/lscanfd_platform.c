/*
 * LOONGSON LS CANFD controller
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/can/dev.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include "lscanfd.h"

#define DRV_NAME	"lscanfd"
#define SYS_CAN_CLOCK   (160000000)

static void lscanfd_platform_set_drvdata(struct device *dev,
					struct net_device *ndev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);

	platform_set_drvdata(pdev, ndev);
}

static int lscanfd_platform_probe(struct platform_device *pdev)
{
	struct resource *res; /* IO mem resources */
	struct device	*dev = &pdev->dev;
	struct device_node *of = pdev->dev.of_node;
	void __iomem *addr;
	int ret;
	unsigned int ntxbufs;
	int irq,err;
	unsigned long can_clk;
	unsigned int prop;
	bool canfd_dmarx = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	addr = devm_ioremap_resource(dev, res);

	if (IS_ERR(addr)) {
		dev_err(dev, "Cannot remap address.\n");
		ret = PTR_ERR(addr);
		goto err;
	}
	
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Cannot find interrupt.\n");
		ret = irq;
		goto err;
	}
	err = of_property_read_u32(of, "ls,clock-frequency", &prop);
	if (!err)
		can_clk = (u64)prop;
	else
		can_clk = (u64)SYS_CAN_CLOCK;
	
	if(of_property_read_bool(of, "ls,canfd-dmarx"))
		canfd_dmarx = 1;

	ntxbufs = 16;
	ret = lscanfd_probe_common(dev, addr, res->start, irq, ntxbufs, can_clk, canfd_dmarx, lscanfd_platform_set_drvdata);

	if (ret < 0)
		platform_set_drvdata(pdev, NULL);

err:
	return ret;
}

static int lscanfd_platform_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct lscanfd_priv *priv = netdev_priv(ndev);

	netdev_dbg(ndev, "lscanfd_remove");

	unregister_candev(ndev);
	netif_napi_del(&priv->napi);
	free_candev(ndev);

	return 0;
}

/* Match table for OF platform binding */
static const struct of_device_id lscanfd_of_match[] = {
	{ .compatible = "loongson,ls-canfd", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, lscanfd_of_match);

static struct platform_driver lscanfd_driver = {
	.probe	= lscanfd_platform_probe,
	.remove	= lscanfd_platform_remove,
	.driver	= {
		.name = DRV_NAME,
		.of_match_table	= lscanfd_of_match,
	},
};

module_platform_driver(lscanfd_driver);

MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_DESCRIPTION("Looongson LS CANFD Controller driver");
MODULE_LICENSE("GPL");
