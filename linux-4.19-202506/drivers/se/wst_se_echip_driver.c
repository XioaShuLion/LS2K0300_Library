/*
* SE ENCRYPT CHIP Driver
*
* Written By: dcm, WESTONE Corporation
*
* Copyright (C) 2021 10 WESTONE Corp
*
* All rights reserved.
*/
#include <linux/semaphore.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/crypto.h>
#include <linux/pci.h>
#include <crypto/internal/skcipher.h>
#include <loongson.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include "wst_se_echip_driver.h"

#define LIOINTC_DEFAULT_PHYS_BASE      (0x3ff00000 + 0x1400)
#define LS_CRYPTO_SE_ADDR1 TO_UNCAC(LOONGSON_REG_BASE+0x0400)
#define LS_CRYPTO_SE_ADDR2 TO_UNCAC(0xc0010200000)
#define DRIVER_VERSION "01.10.221201"
DEFINE_SPINLOCK(g_reclistlock);
DEFINE_SPINLOCK(g_getdmabuflock);
void __iomem *g_viraddr = NULL;
//
static struct class *g_psecclass;
static struct device *g_psecdev;
static SECHIPDRV_CTRL *g_psechipdrvctl;
static atomic_t g_sendtotallen;
static struct semaphore g_lowsema, g_lowirqsema;
static struct tag_Queue_container g_RecQueueContainer;
static struct tag_Queue_container g_DmaBQueueContainer;
static struct tag_dma_buf_ctl *g_pdmadatabuf=NULL;
static int g_iDmaBufNum;
static struct tasklet_struct g_rectasklet;
static int g_isechip_Major = -1;
static unsigned char *g_pCacheInBuf = NULL, *g_pCacheOutBuf = NULL;
static int g_suspend = 0;
static struct semaphore g_dmabufsem;
struct loongson_sedriver_irq se_irq[] = {
	{
		.pat = "se-irq",
	},
	{
		.pat = "se-lirq",
	},
};
//
static irqreturn_t se_interrupt(int irq, void *p);
static irqreturn_t wst_low_channel_status(int irq, void *p);
static int globalmem_do_send_op(SEND_PACKAGE *psendpackage) ;
static void globalmem_do_rec_op(unsigned long p);
static int g_iuseintr = 1;
module_param(g_iuseintr, int, 0644);
#ifndef CONFIG_MIPS	
static int manual = 0;
module_param(manual, int, 0644);
#endif
//atomic_t g_malloc,g_free,g_intr;
/**
 *malloc dma memory
 */
#define Cache_LEAF0			0x00
#define Cache_LEAF1			0x01
#define Cache_LEAF2			0x02
#define Cache_LEAF3			0x03
#define Cache_LEAF4			0x04
#define Cache_LEAF5			0x05

#define Index_Invalidate		0x08
#define Index_Writeback_Inv		0x08
#define Hit_Invalidate			0x10
#define Hit_Writeback_Inv		0x10
#define CacheOp_User_Defined		0x18

#define Index_Writeback_Inv_LEAF0	(Cache_LEAF0 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF1	(Cache_LEAF1 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF2	(Cache_LEAF2 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF3	(Cache_LEAF3 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF4	(Cache_LEAF4 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF5	(Cache_LEAF5 | Index_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF0		(Cache_LEAF0 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF1		(Cache_LEAF1 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF2		(Cache_LEAF2 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF3		(Cache_LEAF3 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF4		(Cache_LEAF4 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF5		(Cache_LEAF5 | Hit_Writeback_Inv)

#define cache_op(op,addr)						\
	__asm__ __volatile__(						\
	"	cacop	%0, %1					\n"	\
	:								\
	: "i" (op), "ZC" (*(unsigned char *)(addr)))



static void cache_flush(void *addr, int len)
{
#define CACHE_LINE_SIZE 64
    int i;

    for(i=0;i<len;i+=CACHE_LINE_SIZE)
	    cache_op(Hit_Writeback_Inv_LEAF3, addr+i);
}

static int se_init_dma_buf(int idatasize, int idatanum)
{
	int i;
	struct tag_dma_buf_ctl *pdmactl;

	wst_InitQueue(&g_DmaBQueueContainer,idatanum);
	g_pdmadatabuf = kmalloc((sizeof(struct tag_dma_buf_ctl)*idatanum), GFP_KERNEL);
	if (!g_pdmadatabuf)
		return WST_SE_FAILURE;
	for (i = 0; i < idatanum; i++) {
		pdmactl = &g_pdmadatabuf[i];
		pdmactl->pDmaBuf = (unsigned char *)__get_free_page(GFP_KERNEL|GFP_DMA);
		if (!pdmactl->pDmaBuf) {
			g_iDmaBufNum = i;
			return WST_SE_OK;
		}
		wst_Pushback_Que(&g_DmaBQueueContainer, pdmactl);
	}
	g_iDmaBufNum = i;
	sema_init(&g_dmabufsem, 1);
	
	return WST_SE_OK;
}
/**
 *delete the allocated memory
 */
static int se_del_dma_buf(void)
{
	int i;
	struct tag_dma_buf_ctl *pdmactl;
	for (i = 0; i < g_iDmaBufNum; i++) {
		pdmactl = &g_pdmadatabuf[i];
		if (pdmactl) {
			free_page((unsigned long)pdmactl->pDmaBuf);
			pdmactl->pDmaBuf = NULL;
		}
	}
	kfree(g_pdmadatabuf);
	g_pdmadatabuf = NULL;
	return 0;
}
/**
 *get free dma memory
 */
struct tag_dma_buf_ctl *se_get_dma_buf(int ikernel)
{
	struct tag_dma_buf_ctl *pbufctl = NULL;
	unsigned long ultimeout = 0;

	ultimeout = jiffies+20*HZ;
	while(1)
	{	
		spin_lock(&g_getdmabuflock);
		pbufctl = (struct tag_dma_buf_ctl *)wst_Popfront_Que(&g_DmaBQueueContainer);
		spin_unlock(&g_getdmabuflock);
		if (pbufctl) {
			return pbufctl;
		}
		else
		{
			if(down_timeout(&g_dmabufsem, ultimeout))
			{
				return NULL;
			}
		}
	}
	return pbufctl;
}
/**
 *release used dma memory
 */
int se_free_dma_buf(struct tag_dma_buf_ctl *pdmabufctl)
{
	spin_lock(&g_getdmabuflock);
	wst_Pushback_Que(&g_DmaBQueueContainer, pdmabufctl);
	spin_unlock(&g_getdmabuflock);
	if(g_dmabufsem.count <= 0)
		up(&g_dmabufsem);
	return 0;
}
#ifndef ALIGN(x, a)
#define ALIGN(x, a)	(((x) + ((a) - 1)) & (~((a) - 1)))
#endif

static unsigned long  bytes_align(struct device *pdev, unsigned long ulviraddr)
{
	ulviraddr = ALIGN(ulviraddr,64);
	return ulviraddr;
}
static unsigned long  descri_bytes_align(unsigned long ulviraddr)
{
	ulviraddr = ALIGN(ulviraddr,32);
	return ulviraddr;
}

void se_chip_unload(void);

static int wst_init(void);
int se_chip_load(void);

/**
 *chip register initialize 
 */
static int se_chipInit(SECHIPDRV_CTRL *pdrvctl)
{
	dma_addr_t ulbusaddr;
	unsigned long ulviraddr;
	int i = 0, j = 0;
	unsigned int dmaoldmask;
	
	for (i = 0; i < SWCHANNELNUM; i++) {
		ulviraddr = (unsigned long)dma_alloc_coherent(
			pdrvctl->pdev,
			(SE_BDQUEUE_LEN * SE_BD_LENGTH+32),
			&ulbusaddr, GFP_KERNEL
			);
		if (ulviraddr == 0 || ulbusaddr == 0) {
			return -EFAULT;
		}
		memset((void *)ulviraddr, 0, (SE_BDQUEUE_LEN*SE_BD_LENGTH));
		
		pdrvctl->ulBDMemBasePhy_ori[i]   = ulbusaddr;
		pdrvctl->ulBDMemBase_ori[i]      = ulviraddr;
		pdrvctl->ulBDMemBasePhy[i]   = descri_bytes_align(pdrvctl->ulBDMemBasePhy_ori[i]);
		pdrvctl->ulBDMemBase[i]      = (unsigned long)__va(pdrvctl->ulBDMemBasePhy[i]);
		pdrvctl->ulCurrBdReadPtr[i]  = 0;
		pdrvctl->ulCurrBdWritePtr[i] = 0;
		pdrvctl->ulCurrReadPtr[i]  = 0;
		pdrvctl->ulCurrWritePtr[i] = 0;
	}
	for (i = 0; i < SE_BDQUEUE_LEN; i++) {
		for (j = 0; j < SWCHANNELNUM; j++)
			(&((SE_BASIC_BD *)(pdrvctl->ulBDMemBase[j]))[i])->ucRetCode = 0x0f;
	}
	ulbusaddr = descri_bytes_align(pdrvctl->ulBDMemBasePhy[0]);
	printk("ready to write bqba0\n");
	HandleWrite32(pdrvctl, SE_HREG_BQBA0, HIULONG(ulbusaddr));
	HandleWrite32(pdrvctl, SE_LREG_BQBA0, LOULONG(ulbusaddr));
	HandleWrite32(pdrvctl, SE_REG_BQS0,  SE_BDQUEUE_LEN - 1);
	HandleWrite32(pdrvctl, SE_REG_RQRP0, pdrvctl->ulCurrBdReadPtr[0]);
	HandleWrite32(pdrvctl, SE_REG_BQWP0, pdrvctl->ulCurrBdWritePtr[0]);
	
	ulbusaddr = descri_bytes_align(pdrvctl->ulBDMemBasePhy[1]);
	HandleWrite32(pdrvctl, SE_HREG_BQBA1, HIULONG(ulbusaddr));
	HandleWrite32(pdrvctl, SE_LREG_BQBA1, LOULONG(ulbusaddr));
	HandleWrite32(pdrvctl, SE_REG_BQS1,  SE_BDQUEUE_LEN - 1);
	HandleWrite32(pdrvctl, SE_REG_RQRP1, pdrvctl->ulCurrBdReadPtr[1]);
	HandleWrite32(pdrvctl, SE_REG_BQWP1, pdrvctl->ulCurrBdWritePtr[1]);
	HandleRead32(pdrvctl, SE_REG_MSK, &dmaoldmask);
	HandleWrite32(pdrvctl, SE_REG_MSK, (dmaoldmask | DMA0_CTRL_CHANNEL_ENABLE | DMA1_CTRL_CHANNEL_ENABLE));
	if (g_iuseintr != 0)
		HandleWrite32(pdrvctl, SE_LOWREG_INQ, 1);
	else
		HandleWrite32(pdrvctl, SE_LOWREG_INQ, 0);

	return WST_SE_OK;
}
/**
 *release register resources 
 */
static void  se_chiprelease(SECHIPDRV_CTRL *pdrvctl)
{
	int i;

	for (i = 0; i < SWCHANNELNUM; i++) {
		if (pdrvctl->ulBDMemBase_ori[i]) {
			dma_free_coherent(
				pdrvctl->pdev,
				(SE_BDQUEUE_LEN * SE_BD_LENGTH),
				(void *)pdrvctl->ulBDMemBase_ori[i],
				pdrvctl->ulBDMemBasePhy_ori[i]
				);
			pdrvctl->ulBDMemBase_ori[i] = 0;
			pdrvctl->ulBDMemBasePhy_ori[i] = 0;
			
		}
	}
}
/**
 *se chip reset
 */
static void se_reset(SECHIPDRV_CTRL *pdrvctl)
{
	unsigned int reg;
	unsigned long   ulreg64;
	void __iomem  *addr;

	addr = /*LS_CRYPTO_SE_ADDR1*/(g_viraddr+0x0400);
	
	HandleRead32(pdrvctl, SE_REG_RESET, &reg);
	HandleWrite32(pdrvctl, SE_REG_RESET, reg|SE_DMA_CONTROL_RESET);
	HandleWrite32(pdrvctl, SE_REG_RESET, (reg&(~SE_DMA_CONTROL_RESET))|SE_DMA_CONTROL_SET);
	ulreg64 = readq((volatile void __iomem*)addr);
	if ((ulreg64 & 0xf0000000000000) != 0xf0000000000000) {
		writeq(ulreg64|0xf0000000000000, (volatile void __iomem*)addr);
	}
	HandleWrite32(pdrvctl, SE_INT_CLR, 0xf);
}

/**
 *se interrupt request
 and register initialize
 */
static int wst_init(void)
{
	int ires = WST_SE_OK;
	static u64 wst_dma_mask = DMA_BIT_MASK(64);
	char cName[256];
	SECHIPDRV_CTRL *pdrvctl = NULL;

	pdrvctl = kmalloc(sizeof(SECHIPDRV_CTRL), GFP_KERNEL);
	if (pdrvctl == NULL) {
		return -ENOMEM;
	}

	memset(pdrvctl, 0, sizeof(SECHIPDRV_CTRL));
	pdrvctl->ulMemBase = /*LS_CRYPTO_SE_ADDR2*/(unsigned long)g_viraddr;
	memset(cName, 0, 256);
	sema_init(&(pdrvctl->sema), 0);
	spin_lock_init(&(pdrvctl->mr_lock));
	rwlock_init(&(pdrvctl->mr_lowlock));
	g_psechipdrvctl = pdrvctl;
	g_psechipdrvctl->pdev = g_psecdev; 
	g_psechipdrvctl->pdev->dma_mask = &wst_dma_mask;
	g_psechipdrvctl->pdev->coherent_dma_mask = (long long unsigned int)&wst_dma_mask;
	wst_InitQueue(&g_RecQueueContainer,65535);
	se_reset(pdrvctl);
	pdrvctl->ilowIrq = 0;
	pdrvctl->iIrq = se_irq[0].irq;
	manual = 1;
#ifndef CONFIG_MIPS	
	if(manual == 1)
	{
		//inquire interrupt route
		printk("high irq route status is 0x%x\n",readb((volatile void *)TO_UNCAC(LIOINTC_DEFAULT_PHYS_BASE + 0x14)));
		printk("low irq route status is 0x%x\n",readb((volatile void *)TO_UNCAC(LIOINTC_DEFAULT_PHYS_BASE + 0x11)));
	}
	else if(manual == 2)
	{	  

		u64 irqpin = 0;
		struct irq_data *irqdata;
		irqpin = readl((volatile void *)TO_UNCAC(LOONGSON_REG_BASE + 0x420));
		if(test_bit(49,(const volatile long unsigned*)&irqpin) == 1)
		{
			irqdata = irq_get_irq_data(pdrvctl->iIrq);
			writeb(0x01, (volatile void *)TO_UNCAC(LIOINTC_DEFAULT_PHYS_BASE + irqdata->hwirq));
			irqdata = irq_get_irq_data(pdrvctl->ilowIrq);
			writeb(0x01, (volatile void *)TO_UNCAC(LIOINTC_DEFAULT_PHYS_BASE + irqdata->hwirq));
		}
		else
		{
			irqdata = irq_get_irq_data(pdrvctl->iIrq);
			writeb(0x11, (volatile void *)TO_UNCAC(LIOINTC_DEFAULT_PHYS_BASE + irqdata->hwirq));
			irqdata = irq_get_irq_data(pdrvctl->ilowIrq);
			writeb(0x11, (volatile void *)TO_UNCAC(LIOINTC_DEFAULT_PHYS_BASE + irqdata->hwirq));
		}
	}
	else
	   ;
#endif

	pdrvctl->ilowIrq = 0;
	pdrvctl->iIrq = se_irq[0].irq;
	ires = request_irq(pdrvctl->iIrq, &se_interrupt, IRQF_SHARED, "wst-se-hirq", pdrvctl);
	if(ires)
	{
		printk("\nrequest_irq err");
		pdrvctl->iIrq = 0;
		goto err;
	}
	if(g_iuseintr == 1)
	{
		pdrvctl->ilowIrq = se_irq[1].irq;
		ires = request_irq(pdrvctl->ilowIrq, &wst_low_channel_status, IRQF_SHARED, "wst-se-lirq", pdrvctl);
		if(ires)	
		{
			printk("\nrequest_lowirq err,ires=0x%x\n",ires);
			pdrvctl->ilowIrq = 0;
			goto err;
		}
	}

	if (WST_SE_OK != se_chipInit(pdrvctl)) {
		ires = -ENODEV;
		goto err;
	}
	return WST_SE_OK;
err:
	if (pdrvctl != NULL) {
		if (pdrvctl->iIrq) {
			free_irq(pdrvctl->iIrq, pdrvctl);
			pdrvctl->iIrq = 0;
		}
		if (pdrvctl->ilowIrq) {
			free_irq(pdrvctl->ilowIrq, pdrvctl);
			pdrvctl->ilowIrq = 0;
		}
		se_chiprelease(pdrvctl);
		kfree (pdrvctl);
		g_psechipdrvctl = NULL;
	}
	return ires;
}

static void wst_clear(void)
{
	SECHIPDRV_CTRL  *pdrvctl = NULL;
	
	pdrvctl = g_psechipdrvctl;
	
	if (pdrvctl) {
		if (pdrvctl->iIrq) {
			free_irq(pdrvctl->iIrq, pdrvctl);
			pdrvctl->iIrq = 0;
		}
		if (pdrvctl->ilowIrq) {
			free_irq(pdrvctl->ilowIrq, pdrvctl);
			pdrvctl->ilowIrq = 0;
		}
		se_chiprelease(pdrvctl);
		kfree (pdrvctl);
		g_psechipdrvctl = NULL;
	}
	return;
}
/**
 *data send function
 high speed data transmissionn channel
 */
static int globalmem_do_send_op(SEND_PACKAGE *psendpackage) 
{
	SE_BASIC_BD *pCurBD,CurBD;
	unsigned int ulCurrWritePtr, ulWritePtr;
	unsigned short len = 0;
	unsigned long ulCurrAddrInput = 0, ulCurrAddrOutput = 0, tmpaddr;
	SECHIPDRV_CTRL  *pdrvctl;
	unsigned char *pInPtr;
	unsigned short usInlen;
	unsigned char *pOutPtr;
	unsigned short *pusOutlen;
	int iChannel;
	unsigned char ucFlag;
	unsigned char ucOpCode;
	unsigned char *pucRetCode;
	PSECallBackfn pcallback;
	void *pParma;
	int iKernel;
	struct completion *mycomplete;
	unsigned long ulflag;
	unsigned long ultimeout;
	int rv = 0;
	
	pdrvctl = psendpackage->pdrvctl;
	pInPtr = psendpackage->pInPtr;
	usInlen = psendpackage->usInlen;
	pOutPtr = psendpackage->pOutPtr;
	pusOutlen = psendpackage->pusOutlen;
	iChannel = psendpackage->iChannel;
	ucFlag = psendpackage->ucFlag;
	ucOpCode = psendpackage->ucOpCode;
	pucRetCode = psendpackage->pucRetCode;
	pcallback = psendpackage->pcallback;
	pParma = psendpackage->pParma;
	iKernel = psendpackage->iKernel;
	mycomplete = psendpackage->mycomplete;
	ultimeout = psendpackage->ulendtime;
	
	if (iKernel == 0) {
		//while(time_before(jiffies,ultimeout))
		{
#ifdef CONFIG_MIPS	
			if ((pdrvctl->ulCurrBdReadPtr[iChannel] == ((pdrvctl->ulCurrBdWritePtr[iChannel] + 1) & (SE_BDQUEUE_LEN-1)))
				||((atomic_read(&g_sendtotallen)+*pusOutlen+SE_FILL_LEN)>SE_MAX_SEND_LEN))
#else		
				if (pdrvctl->ulCurrBdReadPtr[iChannel] == ((pdrvctl->ulCurrBdWritePtr[iChannel] + 1) & (SE_BDQUEUE_LEN-1)))
#endif				
				{
                  printk("fff\n");
					down_timeout(&(pdrvctl->sema), 1*HZ);
					rv = WST_SE_ERROR_FULL;
				}
				else
				{
					rv = 0;
					//break;
				}
		}
		if(rv != 0x0)
		{
            printk("ttt\n");
			complete(mycomplete);
			return rv;
		}
	}
	else {
		ultimeout = jiffies+1*HZ;
		while(time_before(jiffies,ultimeout))
		{
#ifdef CONFIG_MIPS	
			if ((pdrvctl->ulCurrBdReadPtr[iChannel] == ((pdrvctl->ulCurrBdWritePtr[iChannel] + 1) & (SE_BDQUEUE_LEN-1)))
				||((atomic_read(&g_sendtotallen)+*pusOutlen+SE_FILL_LEN)>SE_MAX_SEND_LEN))
#else		
				if (pdrvctl->ulCurrBdReadPtr[iChannel] == ((pdrvctl->ulCurrBdWritePtr[iChannel] + 1) & (SE_BDQUEUE_LEN-1)))
#endif	
				{

					rv = WST_SE_ERROR_FULL;
				}
				else
				{
					rv = 0;
					break;
				}
		}
		if(rv != 0x0){
                    printk("rvv\n");
			return rv;
		}
	}
	pCurBD = &CurBD;
	if (pInPtr == pOutPtr) {
		if (pOutPtr) {
			len = usInlen >= *pusOutlen ? usInlen : *pusOutlen;
			if (len) {
				ulCurrAddrOutput = dma_map_single(pdrvctl->pdev, pOutPtr, len, DMA_BIDIRECTIONAL);
#if 1
				spin_lock_irqsave(&(pdrvctl->mr_lock), ulflag);
				cache_flush(pOutPtr, len);
				spin_unlock_irqrestore(&(pdrvctl->mr_lock), ulflag);
#endif

				mb();
				if (ulCurrAddrOutput == 0) {
					printk("map ulCurrAddrOutput error 111 %p\n", pOutPtr);
					rv = WST_SE_FAILURE;
					return rv;
				}
				pCurBD->ulOutputLPtr = LOULONG(ulCurrAddrOutput);
				pCurBD->ulOutputHPtr = HIULONG(ulCurrAddrOutput);
				pCurBD->ulInputLPtr = pCurBD->ulOutputLPtr;
				pCurBD->ulInputHPtr = pCurBD->ulOutputHPtr;
			}
		}
	} else {
		if (pOutPtr && (*pusOutlen)) {
			ulCurrAddrOutput = dma_map_single(pdrvctl->pdev, pOutPtr, *pusOutlen, DMA_FROM_DEVICE);
			mb();
			if (ulCurrAddrOutput == 0) {
				printk("map ulCurrAddrOutput error 222 %p\n", pOutPtr);
				rv = WST_SE_FAILURE;
				return rv;
			}
			pCurBD->ulOutputLPtr = LOULONG(ulCurrAddrOutput);
			pCurBD->ulOutputHPtr = HIULONG(ulCurrAddrOutput);
		}
		if (usInlen && pInPtr) {
			ulCurrAddrInput = dma_map_single(pdrvctl->pdev, pInPtr, usInlen, DMA_TO_DEVICE);
            tmpaddr= 0x8000000000000000 | ulCurrAddrInput; //zzz
            memcpy((void   *)tmpaddr, (void *)(pInPtr), usInlen); //zzz
        	printk(">>>>>> hi debug2 \n");
			mb();
			if (ulCurrAddrInput == 0) {
				if (ulCurrAddrOutput) {
					dma_unmap_single(pdrvctl->pdev, ulCurrAddrOutput, *pusOutlen, DMA_FROM_DEVICE);
					mb();
					pCurBD->ulOutputLPtr = 0;
					pCurBD->ulOutputHPtr = 0;
				}
				rv = WST_SE_FAILURE;
				return rv;
			}
			pCurBD->ulInputLPtr = LOULONG(ulCurrAddrInput);
			pCurBD->ulInputHPtr = HIULONG(ulCurrAddrInput);
		}
	}
	pCurBD->ucOpCode = ucOpCode & 0x0f;
	pCurBD->ucFlag = ucFlag & 0x7;
	pCurBD->usInputLength = usInlen;
	if (pusOutlen) {
		pCurBD->usOutputLength = *pusOutlen;
	}
	pCurBD->ucRetCode	= 0x0f;
	
	spin_lock_irqsave(&(pdrvctl->mr_lock), ulflag);
	ulCurrWritePtr = pdrvctl->ulCurrBdWritePtr[iChannel];
	ulWritePtr = (ulCurrWritePtr + 1) & (SE_BDQUEUE_LEN-1);
	pCurBD = &((SE_BASIC_BD *)(pdrvctl->ulBDMemBase[iChannel]))[ulCurrWritePtr];
#if 1 // must
	pCurBD = ((u64)pCurBD & ~0xf000000000000000) | 0x8000000000000000;//zzzzzz
	memcpy(pCurBD, &CurBD , sizeof(SE_BASIC_BD));
#endif
	if (pcallback != NULL) {
		(pdrvctl->pcallback)[iChannel][ulCurrWritePtr] = pcallback;
		pdrvctl->pParma[iChannel][ulCurrWritePtr] = pParma;
	} else {
		(pdrvctl->pcallback)[iChannel][ulCurrWritePtr] = NULL;
		pdrvctl->pParma[iChannel][ulCurrWritePtr] = NULL;
	}
	
	pdrvctl->ikernel[iChannel][ulCurrWritePtr] = iKernel;
	pdrvctl->stsemphore[iChannel][ulCurrWritePtr] = mycomplete;
		
	pdrvctl->pusOutlen[iChannel][ulCurrWritePtr] = pusOutlen;
	pdrvctl->usInlen[iChannel][ulCurrWritePtr] = usInlen&0xffff;
	if (ulCurrAddrOutput) {
		pdrvctl->ulOutputPtr[iChannel][ulCurrWritePtr] = (unsigned long *)ulCurrAddrOutput;
pdrvctl->pOutPtrAim[iChannel][ulCurrWritePtr] = pOutPtr;
	} else
		pdrvctl->ulOutputPtr[iChannel][ulCurrWritePtr] = 0;
	if (ulCurrAddrInput)
		pdrvctl->ulInputPtr[iChannel][ulCurrWritePtr] = (unsigned long *)ulCurrAddrInput;
	else
		pdrvctl->ulInputPtr[iChannel][ulCurrWritePtr] = 0;
	pdrvctl->pucRetCode[iChannel][ulCurrWritePtr] = pucRetCode;
#ifdef CONFIG_MIPS	
	atomic_add((*(pdrvctl->pusOutlen[iChannel][ulCurrWritePtr])+SE_FILL_LEN),&g_sendtotallen);
#endif	
	if (iChannel == 0) {
		wmb();
		HandleWrite32(pdrvctl, SE_REG_BQWP0, ulWritePtr);
	} else {
		HandleWrite32(pdrvctl, SE_REG_BQWP1, ulWritePtr);
	}
	pdrvctl->ulCurrBdWritePtr[iChannel] = ulWritePtr;	
	spin_unlock_irqrestore(&(pdrvctl->mr_lock), ulflag);

	return rv;
}

static int se_hardtrans(
						SECHIPDRV_CTRL  *pdrvctl,
						unsigned char *pInPtr,
						unsigned short usInlen,
						unsigned char *pOutPtr,
						unsigned short *pusOutlen,
						int iChannel,
						unsigned char ucFlag,
						unsigned char ucOpCode,
						unsigned char *pucRetCode,
						PSECallBackfn pcallback,
						void *pParma,
						int iKernel,
						struct completion *mycomplete
					)
{
	SEND_PACKAGE sendpackage;
	if(g_suspend == 1) {
    printk(">>> g_suspend\n");
		return WST_SE_FAILURE;
    }
	sendpackage.pdrvctl = pdrvctl;
	sendpackage.pInPtr = pInPtr;
	sendpackage.usInlen = usInlen;
	sendpackage.pOutPtr = pOutPtr;
	sendpackage.pusOutlen = pusOutlen;
	sendpackage.iChannel = iChannel;
	sendpackage.ucFlag = ucFlag;
	sendpackage.ucOpCode = ucOpCode;
	sendpackage.pucRetCode = pucRetCode;
	sendpackage.pcallback = pcallback;
	sendpackage.pParma = pParma;
	sendpackage.iKernel = iKernel;
	sendpackage.mycomplete = mycomplete;
	sendpackage.ulendtime = jiffies+30*HZ;
	return globalmem_do_send_op(&sendpackage);
}


static irqreturn_t wst_low_channel_status(int irq, void *p)
{
	SECHIPDRV_CTRL *pdrvctl = (SECHIPDRV_CTRL *)p;
	int64_t  ulIntStat = 0;
	unsigned long ulflag;
	read_lock_irqsave(&(pdrvctl->mr_lowlock), ulflag);
	HandleRead64(pdrvctl, SE_LOWREG_STS, &ulIntStat);

	if (ulIntStat == 2) {
		HandleWrite64(pdrvctl, SE_LOWINT_CLEAR, 2);

		up(&g_lowirqsema);
	}
	read_unlock_irqrestore(&(pdrvctl->mr_lowlock), ulflag);
	//printk(">>> ulIntStat %d\n", ulIntStat);
	return IRQ_HANDLED;
}


static int se_useropen (struct inode *inode, struct file *file)
{
	if (MINOR(inode->i_rdev) != 0) {
		printk("inode->i_rdev=%d\n",inode->i_rdev);
		return -ENODEV;
	} else		
		return WST_SE_OK;
}

/**
 *data send function
 low speed data transmissionn channel
 */
static ssize_t wst_low_channel_userwrite_op(
											SECHIPDRV_CTRL  *pdrvctl,
											SWCommuData *UserCommuData,
											int iskernel
											)
{
	unsigned long long addr = 0, outaddr = 0;
	int ilen;
	int count = WST_SE_OK;
	unsigned long long ulsendlen;
	unsigned char *m_pCacheInBuf;
	unsigned char *m_pCacheOutBuf;
	unsigned long ulflag;
	if ((g_pCacheInBuf == NULL) || (g_pCacheOutBuf == NULL))
		return -EFAULT;

	m_pCacheInBuf = (unsigned char *)bytes_align(0, (unsigned long)g_pCacheInBuf);
	m_pCacheOutBuf = (unsigned char *)bytes_align(0, (unsigned long)g_pCacheOutBuf);
	if (iskernel == 0) {
		if (wst_cpyusrbuf((void   *)(UserCommuData->pucInbuf), (void *)m_pCacheInBuf, UserCommuData->usInputLen, READUBUF)) {
			TRACEERR("copy user data error\n");
			return -EFAULT;
		}
	} 
	else
		memcpy((void   *)m_pCacheInBuf, (void *)(UserCommuData->pucInbuf), UserCommuData->usInputLen);

	ilen = UserCommuData->usInputLen >= UserCommuData->usOutputLen ? UserCommuData->usInputLen:UserCommuData->usOutputLen;

	addr = dma_map_single(pdrvctl->pdev, m_pCacheInBuf, ilen, DMA_TO_DEVICE);

	mb();
	if (addr == 0) {
		TRACEERR("transfer buffer is err\n");
		return -EFAULT;
	}
    
	outaddr = dma_map_single(pdrvctl->pdev, m_pCacheOutBuf, ilen, DMA_FROM_DEVICE);

	mb();
	if (outaddr == 0) {
		TRACEERR("transfer buffer is err\n");
		dma_unmap_single(pdrvctl->pdev, addr, ilen, DMA_TO_DEVICE);
		mb();
		return -EFAULT;
	}
	ulsendlen = (UserCommuData->usInputLen/8);
	ulsendlen = (ulsendlen & 0x00000000ffffffff) << 32;
	write_lock_irqsave(&(pdrvctl->mr_lowlock), ulflag);
	HandleWrite64(pdrvctl, SE_WRITE_REG1, ulsendlen);
	HandleWrite64(pdrvctl, SE_WRITE_REG2, addr);
	HandleWrite64(pdrvctl, SE_WRITE_REG3, outaddr); 
	write_unlock_irqrestore(&(pdrvctl->mr_lowlock), ulflag);
	if (g_iuseintr != 0) {
		unsigned long ultimeout = 0;
		ultimeout = jiffies+90*HZ;
		if (down_timeout(&g_lowirqsema,ultimeout)) {
			count = -EINTR;
			goto EXIT;
		}
	} else {
		unsigned long start_jiffies = 0, end_jiffies = 0;
		int64_t  ulIntStat = 0;
		start_jiffies = jiffies;
		end_jiffies = jiffies;
		while (1) {
			write_lock_irqsave(&(pdrvctl->mr_lowlock), ulflag);
			HandleRead64(pdrvctl, SE_LOWREG_SR, &ulIntStat);
			end_jiffies = jiffies;
			if (ulIntStat == 1) {
				HandleWrite64(pdrvctl, SE_LOWREG_SR, 0);
				write_unlock_irqrestore(&(pdrvctl->mr_lowlock), ulflag);
				break;
			}
			write_unlock_irqrestore(&(pdrvctl->mr_lowlock), ulflag);
			if (jiffies_to_msecs(end_jiffies-start_jiffies)/1000 >= 90) {
				count = -EFAULT;
				TRACEERR("time is out\n");
				goto EXIT;
			}
		}
	}
	dma_unmap_single(pdrvctl->pdev, addr, ilen, DMA_TO_DEVICE);
	dma_unmap_single(pdrvctl->pdev, outaddr, ilen, DMA_FROM_DEVICE);
	mb();

	if (UserCommuData->usOutputLen) {
		if (iskernel == 0) {
			if (wst_cpyusrbuf(UserCommuData->pucOutbuf, m_pCacheOutBuf, UserCommuData->usOutputLen, WRITEUBUF)) {
				TRACEERR("cpyusruf is error\n");
				return -EFAULT;
			}
		} else			
			memcpy(UserCommuData->pucOutbuf, m_pCacheOutBuf, UserCommuData->usOutputLen);
	}

	return count;

EXIT:
	dma_unmap_single(pdrvctl->pdev, addr, ilen, DMA_TO_DEVICE);
	dma_unmap_single(pdrvctl->pdev, outaddr, ilen, DMA_FROM_DEVICE);
	mb();
	
	return count;
}
/**
 *data send function comes from the user layer 
 high speed data transmissionn channel
 */
static atomic_t op;
static ssize_t se_userwrite(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	unsigned char *pCacheBuf = NULL, *pCacheOutBuf = NULL, *pCacheBufalign = NULL, *pCacheOutBufalign = NULL;
	SECHIPDRV_CTRL  *pdrvctl = NULL;
	SWCommuData *pCommuData = NULL;
	int iCommuDatalen = 0;
	int pucRetCode = 0;
	unsigned short iChannel = 0;
	unsigned char ucFlag = 0, ucOpCode = 0;
	int *ppucRetCode;
	struct completion mycomplete;
	struct tag_dma_buf_ctl *pbufinctl = NULL;
	int iret = 0;
	
	if (count == 0) {
		TRACEERR("count=0\n");
		return WST_SE_OK;
	}
	if (MINOR(file->f_path.dentry->d_inode->i_rdev) != 0) {
		return -ENODEV;
	}
	iCommuDatalen = sizeof(SWCommuData);
	if (count != iCommuDatalen) {
		return -EINVAL;
	}
	pdrvctl = g_psechipdrvctl;
	pCommuData = kmalloc(iCommuDatalen, GFP_KERNEL);
	if (!pCommuData) {
		TRACEERR("pCommuData NULL\n");
		return -ENOMEM;
	}
	if (wst_cpyusrbuf((void   *)buf, (void *)pCommuData, iCommuDatalen, READUBUF)) {
		TRACEERR("copy user data error\n");
		count = -EFAULT;
		goto EXIT;
	}
	
	switch ((pCommuData->usFlags)&0x000f) {
	case WST_GO_CHANNEL2:
		if ((pCommuData->usInputLen > DMA_BUFSIZE) || (pCommuData->usOutputLen > DMA_BUFSIZE)) {
			TRACEERR("len is error\n");
			count = -EINVAL;
			goto EXIT;
		}
		if (down_killable(&g_lowsema) == -EINTR) {
			count = -EINTR;
			goto EXIT;
		}
		count = wst_low_channel_userwrite_op(pdrvctl, pCommuData, 0);
        
		up(&g_lowsema);
		goto EXIT;
	
	case WST_GO_CHANNEL0:
		if (pCommuData->usInputLen == 0) {
			count = -EINVAL;
			goto EXIT;
		}
		if (pCommuData->usInputLen != 0) {
			if (pCommuData->usInputLen > DMA_BUFSIZE) {
				count = -EINVAL;
				goto EXIT;
			}
			ucFlag = INPUT_VALID;
			if (pCommuData->usOutputLen)
				ucFlag |= OUTPUT_VALID;
		}
     	
		iChannel = 0;
		ucOpCode = 0x0;
		break;
	
	case WST_GO_CHANNEL1:
        printk("nnn\n");
		if (pCommuData->usInputLen == 0) {
			count = -EINVAL;
			goto EXIT;
		}
		if (pCommuData->usInputLen != 0) {
			if (pCommuData->usInputLen > DMA_BUFSIZE) {
				count = -EINVAL;
				goto EXIT;
			}
			ucFlag = INPUT_VALID;
			if (pCommuData->usOutputLen)
				ucFlag |= OUTPUT_VALID;
		}
		iChannel = 1;
		ucOpCode = 0x0;
		break;

	default:
		{
			printk(">>> pCommuData->usFlags %d\n", pCommuData->usFlags);
			count = -EINVAL;
			goto EXIT;
		}
	}

	pbufinctl = se_get_dma_buf(0);
	if (pbufinctl == NULL) {
		printk("kmalloc pCacheBuf error\n");
		count = -ENOMEM;
		goto EXIT;
	}
	pCacheBuf = pbufinctl->pDmaBuf;
	pCacheBufalign = pCacheBuf;
	
	if (wst_cpyusrbuf((void   *)(pCommuData->pucInbuf), (void *)pCacheBufalign, pCommuData->usInputLen, READUBUF)) {
		printk("cpyuserbuf pCacheBufalign error\n");
		count = -ENOMEM;
		goto EXIT;
	}

	pCacheOutBuf = pbufinctl->pDmaBuf;
	pCacheOutBufalign = pCacheOutBuf;

	ppucRetCode = &pucRetCode;
	
	count = WST_SE_OK;
	init_completion(&mycomplete);
	
	iret = se_hardtrans(
		pdrvctl,
		pCacheBufalign,
		pCommuData->usInputLen,
		pCacheOutBufalign,
		&(pCommuData->usOutputLen),
		iChannel,
		ucFlag,
		ucOpCode,
		(unsigned char *)ppucRetCode,
		0,
		0,
		0,
		&mycomplete
	);
	if (iret == WST_SE_FAILURE) {
		printk(">>> se_hardtrans failed\n");
		count = -EIO;
		goto EXIT;
	}

    wait_for_completion(&mycomplete);
	if (pucRetCode != WST_SE_OK) {
		count = -(SE_BASEERR+pucRetCode);
		goto EXIT;
	}
	
	if (pCommuData->pucOutbuf) {

		if (wst_cpyusrbuf(pCommuData->pucOutbuf, pCacheOutBufalign, pCommuData->usOutputLen, WRITEUBUF)) {
			count = -EFAULT;
			TRACEERR("wst_cpyusrbuf is error\n");
			goto EXIT;
		}
	}

EXIT:
	if (pbufinctl) {
		se_free_dma_buf(pbufinctl);
	}
	if (pCommuData)
		kfree(pCommuData);
	return count;
}

static void globalmem_do_rec_op(unsigned long p)
{
	INT_MESSAGE *intmessage;
	unsigned long ulflags1;
	for(;;)	
	{
		spin_lock_irqsave(&g_reclistlock, ulflags1);
		intmessage = (INT_MESSAGE *)wst_Popfront_Que(&g_RecQueueContainer);
		spin_unlock_irqrestore(&g_reclistlock, ulflags1);
		if (!intmessage) {
			return;
		}
		intmessage->pcallback(intmessage->pParma);
		kfree(intmessage);
	}
	return;
}

/**
 *interrupt processing function
 */
static irqreturn_t se_interrupt(int irq, void *p)//zzz
{
	SECHIPDRV_CTRL *pdrvctl;
	SE_BASIC_BD *pCurBD;
	SE_BASIC_BD *pCurBD_phy;
	unsigned int 	ulCurrReadPtr, ulReadPtr;
	int iChannel;
	int len = 0;
	int i;
	unsigned char ucMyRetCode = 0;
	unsigned long ulIntStat;
	int istatus[2] = {1, 2};
	unsigned long ulflags;
    int loop = 100;

	pdrvctl = (SECHIPDRV_CTRL *)p;
	if (!pdrvctl) {
		return IRQ_HANDLED;
	}
	spin_lock_irqsave(&(pdrvctl->mr_lock), ulflags);
	HandleRead32(pdrvctl, SE_REG_STS, &ulIntStat);
	spin_unlock_irqrestore(&(pdrvctl->mr_lock), ulflags);
	if ((!(ulIntStat & INT_STAT_DMA_MASK)) || (ulIntStat == 0xffffffff)) {
		return IRQ_HANDLED;
	}
	for (i = 0; i <= 1; i++) {
		if (ulIntStat & istatus[i]) {
			if (i == 0) {
				spin_lock_irqsave(&(pdrvctl->mr_lock), ulflags);
				HandleWrite32(pdrvctl, SE_INT_CLR, 1);
				
				HandleRead32(pdrvctl, SE_REG_RQRP0, &ulReadPtr);
				spin_unlock_irqrestore(&(pdrvctl->mr_lock), ulflags);
				//printk(">>> se_interrupt ccc 1 %x\n", ulReadPtr);//高速1通道

				iChannel = 0;
			} else {
				spin_lock_irqsave(&(pdrvctl->mr_lock), ulflags);
				HandleWrite32(pdrvctl, SE_INT_CLR, 2);
				
				HandleRead32(pdrvctl, SE_REG_RQRP1, &ulReadPtr);
				spin_unlock_irqrestore(&(pdrvctl->mr_lock), ulflags);
				printk(">>> se_interrupt ccc 2 %x\n", ulReadPtr);
				iChannel = 1;
			}
		} else
			
			continue;
		ulCurrReadPtr = pdrvctl->ulCurrReadPtr[iChannel];
		while (loop--) { 
			if (ulCurrReadPtr != ulReadPtr) {
				pCurBD = &((SE_BASIC_BD *)(pdrvctl->ulBDMemBase[iChannel]))[ulCurrReadPtr];
				pCurBD_phy = ((u64)pCurBD & ~0xf000000000000000) | 0x8000000000000000;

				if ((pCurBD_phy->ucRetCode == 0x0f) || ((pCurBD_phy->ucFlag & 0x8) != 0x8)) {  
					printk(">>> pCurBD_phy->ucRetCode %x, pCurBD_phy->ucFlag %x\n", pCurBD_phy->ucRetCode, pCurBD_phy->ucFlag); 
					continue;
				} else {
					if (pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr] == pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr]) {
						if (pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr]) {
							len = (*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr])) >= pdrvctl->usInlen[iChannel][ulCurrReadPtr] ?
								(*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr])):pdrvctl->usInlen[iChannel][ulCurrReadPtr];

							dma_unmap_single(
								pdrvctl->pdev,
								(unsigned long)(pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr]),
								len,
								DMA_BIDIRECTIONAL
								);
							mb();
							pCurBD_phy->ulOutputLPtr = 0;
							pCurBD_phy->ulOutputHPtr = 0;
							pCurBD_phy->ulInputHPtr = 0;
							pCurBD_phy->ulInputLPtr = 0;
							pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr] = 0;
						}
					} else {
						if (pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr]) {
							dma_unmap_single(
								pdrvctl->pdev,
								(unsigned long)(pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr]),
								*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr]), DMA_FROM_DEVICE
							);
							smp_wmb();
							pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr] = 0;
						}
						if (pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr]) {

							dma_unmap_single(
								pdrvctl->pdev,
								(unsigned long)(pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr]),
								pdrvctl->usInlen[iChannel][ulCurrReadPtr],
								DMA_TO_DEVICE
								);
							mb();
							pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr] = 0;
						}
					}
					ucMyRetCode = pCurBD_phy->ucRetCode;//zzzzzz
					memcpy(pdrvctl->pucRetCode[iChannel][ulCurrReadPtr], &ucMyRetCode, 1);
					if (pCurBD_phy->ucRetCode != WST_SE_OK) {//zzzzzz
						printk("\nstatus %x\n", pCurBD_phy->ucRetCode);//zzzzzz
					}
					#ifdef CONFIG_MIPS		
						atomic_sub(((*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr]))+SE_FILL_LEN),&g_sendtotallen);
					#endif	
					if ((pdrvctl->ikernel)[iChannel][ulCurrReadPtr] != 0) {
						if (pdrvctl->pcallback[iChannel][ulCurrReadPtr]) {
							INT_MESSAGE *intmessage = NULL;
							unsigned long ulflags1;
							intmessage = (INT_MESSAGE   *)kmalloc(sizeof(INT_MESSAGE), GFP_ATOMIC);
							if (!intmessage)
								return IRQ_HANDLED;
							//atomic_inc(&g_intr);
							intmessage->pcallback = pdrvctl->pcallback[iChannel][ulCurrReadPtr];
							intmessage->pParma = pdrvctl->pParma[iChannel][ulCurrReadPtr];
							spin_lock_irqsave(&g_reclistlock, ulflags1);
							wst_Pushback_Que(&g_RecQueueContainer, intmessage);
							spin_unlock_irqrestore(&g_reclistlock, ulflags1);
							tasklet_schedule(&g_rectasklet);
						}
					} else {
						complete(pdrvctl->stsemphore[iChannel][ulCurrReadPtr]);
                   	}
					ulCurrReadPtr = ((ulCurrReadPtr + 1)&(SE_BDQUEUE_LEN - 1));
					pdrvctl->ulCurrReadPtr[iChannel] = ulCurrReadPtr;
					pdrvctl->ulCurrBdReadPtr[iChannel] = ulCurrReadPtr;
					if (pdrvctl->sema.count <= 0)
						up(&(pdrvctl->sema));
				}
			} else
				
				break;
		}
	}
	return IRQ_HANDLED;
}

static int se_userrelease(struct inode *inode, struct file *file)
{
	return WST_SE_OK;
}

ssize_t se_kernelwrite(
					   unsigned char *pInPtr,
					   unsigned short usInlen,
					   unsigned char *pOutPtr,
					   unsigned short *pusOutlen,
					   unsigned char ucFlag,
					   unsigned char *pucRetCode,
					   PSECallBackfn pcallback,
					   void *pParma
					   )
{
	int iret;
	SECHIPDRV_CTRL  *pdrvctl;
	int iChannel;
	unsigned char ucOpCode;
	SWCommuData CommuData;
	
	pdrvctl = g_psechipdrvctl;
	
	switch (ucFlag) {
	case WST_GO_CHANNEL2:
		{
			CommuData.pucInbuf = pInPtr;
			CommuData.pucOutbuf = pOutPtr;
			CommuData.usFlags = 0;
			CommuData.usInputLen = usInlen;
			CommuData.usOutputLen = *pusOutlen;
			CommuData.usReserve = 0;
			if (down_killable(&g_lowsema) == -EINTR)
				return -EINTR;
			iret = wst_low_channel_userwrite_op(pdrvctl, &CommuData, 1);
			up(&g_lowsema);
			return iret;
		}
	case WST_GO_CHANNEL0:
		if (pcallback == NULL)
			return WST_SE_PARAM_ERROR;
		if (usInlen == 0) {
			return -EINVAL;
		}
		ucFlag = 0;
		if (usInlen != 0) {
			if (usInlen > DMA_BUFSIZE) {
				return -EINVAL;
			}
			ucFlag = INPUT_VALID;
			if (*pusOutlen)
				ucFlag |= OUTPUT_VALID;
		}
		iChannel = 0;
		ucOpCode = 0x0;
		break;

	case WST_GO_CHANNEL1:
		if (pcallback == NULL)
			return WST_SE_PARAM_ERROR;
		if (usInlen == 0) {
			return -EINVAL;
		}
		ucFlag = 0;
		if (usInlen != 0) {
			if (usInlen > DMA_BUFSIZE) {
				return -EINVAL;
			}
			ucFlag = INPUT_VALID;
			if (*pusOutlen)
				ucFlag |= OUTPUT_VALID;
		}
		iChannel = 1;
		ucOpCode = 0x0;
		break;
	
	default:
		return -EINVAL;
	}
	iret = se_hardtrans(
		pdrvctl,
		pInPtr,
		usInlen,
		pOutPtr,
		pusOutlen,
		iChannel,
		ucFlag,
		ucOpCode,
		pucRetCode,
		pcallback,
		pParma,
		1,
		NULL
		);
	if (iret == WST_SE_FAILURE) {
		return -EIO;
	} else	
		return WST_SE_OK;
}

static long se_userioctl(struct file *filp, u_int cmd, u_long arg)
{
	long iret = WST_SE_OK;

	SECHIPDRV_CTRL *pdrvctl = g_psechipdrvctl;
	unsigned long ulvalue;
	HandleRead64(pdrvctl, 0x120, &ulvalue);
	printk("read reg value is 0x%lx in offset 120\n",ulvalue);
	HandleRead64(pdrvctl, 0x118, &ulvalue);
	printk("read reg value is 0x%lx in offset 118\n",ulvalue);
	return iret;
}


static struct file_operations SE_fops = {
		.owner = THIS_MODULE,
		.write = se_userwrite,
		.open = se_useropen,
		.release = se_userrelease,
		.unlocked_ioctl = se_userioctl,
		.compat_ioctl = se_userioctl
};

int  se_chip_load(void)
{
	int ires = WST_SE_OK;

	if (g_isechip_Major >= 0)
		return WST_SE_HAS_OPEN;
	g_psechipdrvctl = NULL;
	ires = se_init_dma_buf(DMA_BUFSIZE, CTL_DMABUFNUM);
	if (ires != WST_SE_OK)
		return WST_SE_ERROR_MALLOC;
	ires = register_chrdev(0, CRYNAME, &SE_fops);
	if (ires < 0) {
		se_del_dma_buf();
		return -1;
	}
	g_isechip_Major = ires;
	ires = 0;
	g_psecclass = class_create(THIS_MODULE, CRYNAME);
	if (IS_ERR(g_psecclass)) {
		ires = PTR_ERR(g_psecclass);
		goto EXIT;
	}
	g_psecdev = device_create(g_psecclass, NULL, MKDEV(g_isechip_Major, 0), NULL, CRYNAME);
	if (IS_ERR(g_psecdev)) {
		ires = PTR_ERR(g_psecdev);
		goto EXIT;
	}
	//g_psecdev->archdata.cpu_device=true;/* 3a5000 server need*/

	ires = wst_init();
	if(ires != WST_SE_OK)
		goto EXIT;
	sema_init(&g_lowsema, 1);
	sema_init(&g_lowirqsema, 0);
	atomic_set(&g_sendtotallen, 0);
atomic_set(&op, 0);
	g_pCacheInBuf = (unsigned char *)__get_free_page(GFP_DMA);
	if (IS_ERR(g_pCacheInBuf)) {
		ires = PTR_ERR(g_pCacheInBuf);
		goto EXIT;
	}
	g_pCacheOutBuf = (unsigned char *)__get_free_page(GFP_DMA);
	if (IS_ERR(g_pCacheOutBuf)) {
		ires = PTR_ERR(g_pCacheOutBuf);
		goto EXIT;
	}
	tasklet_init(&g_rectasklet, globalmem_do_rec_op, 0);
	printk("this driver version is %s\n", DRIVER_VERSION);
	return WST_SE_OK;

EXIT:
	se_del_dma_buf();
	if (g_pCacheInBuf) {
		
		free_page((unsigned long)g_pCacheInBuf);
		g_pCacheInBuf = NULL;
	}
	if (g_pCacheOutBuf) {
		
		free_page((unsigned long)g_pCacheOutBuf);
		g_pCacheOutBuf = NULL;
	}
	wst_clear();
	if (g_psecdev) {
		device_destroy(g_psecclass, MKDEV(g_isechip_Major, 0));
		g_psecdev = NULL;
	}
	if (g_psecclass) {
		class_destroy(g_psecclass);
		g_psecclass = NULL;
	}
	if (g_isechip_Major >= 0)
	{
		unregister_chrdev(g_isechip_Major, CRYNAME);
		g_isechip_Major = -1;
	}
	return ires;
}

void se_chip_unload(void)
{
	SECHIPDRV_CTRL  *pdrvctl = NULL;
	pdrvctl = g_psechipdrvctl;

	up(&pdrvctl->sema);
	if (g_pCacheInBuf) {
		
		free_page((unsigned long)g_pCacheInBuf);
		g_pCacheInBuf = NULL;
	}
	if (g_pCacheOutBuf) {
		
		free_page((unsigned long)g_pCacheOutBuf);
		g_pCacheOutBuf = NULL;
	}
	wst_clear();
	if (g_psecdev) {
		device_destroy(g_psecclass, MKDEV(g_isechip_Major, 0));
		g_psecdev = NULL;
	}
	if (g_psecclass) {
		class_destroy(g_psecclass);
		g_psecclass = NULL;
	}
	if (g_isechip_Major >= 0) {
		unregister_chrdev(g_isechip_Major, CRYNAME);
		g_isechip_Major = -1;
	}
	tasklet_kill(&g_rectasklet);
	se_del_dma_buf();
}

static int ls2k_se_private_device_probe (struct platform_device *pdev)
{
	int nr_irq = 0;
	int irq0 = 0;
	int irq1 = 0;

	nr_irq = platform_irq_count(pdev);
    if (nr_irq <= 0) 
        return -ENODEV;

	irq0 = platform_get_irq(pdev, 0);
	irq1 = platform_get_irq(pdev, 1);

	se_irq[0].irq = irq0;
	se_irq[1].irq = irq1;

	printk("se_virq0=%d, se_virq1=%d\r\n", se_irq[0].irq, se_irq[1].irq);

	return  0;
}

#ifdef CONFIG_OF
static const struct of_device_id se_ls2k_dt_match[] = {
	{.compatible = "ls,ls2k-se", },
	{},
};
MODULE_DEVICE_TABLE(of, se_ls2k_dt_match);
#endif

static struct platform_driver ls2k_se_driver = {
	.probe = ls2k_se_private_device_probe,
    .driver = {
        .name   = "ls2k_se_private",
#ifdef CONFIG_OF
        .of_match_table = of_match_ptr(se_ls2k_dt_match),
#endif
    },
};

static void se_plat_remove(struct pci_dev *pdev)
{
	int i;

	se_chip_unload();

	for(i=0;i<=PCI_STD_RESOURCE_END;i++)
    {
        if(pci_resource_len(pdev,i) == 0)
            continue;
        pcim_iounmap_regions(pdev,BIT(i));
    }
    pci_disable_device(pdev);
}

static int se_plat_probe(struct pci_dev *pdev,
               const struct pci_device_id *id)
{
	int         			ret;
	int  i;

    ret = pci_enable_device(pdev);

	if (pci_set_dma_mask(pdev, 0xffffffffffffffff))
    {
            TRACEERR("pci_set_dma_mask error\n");
        return -ENODEV;
    };

	for(i=0;i<=PCI_STD_RESOURCE_END;i++)
    {
        if(pci_resource_len(pdev,i) == 0)
            continue;
        ret = pcim_iomap_regions(pdev,BIT(i),pci_name(pdev));
        if(ret)
            return ret;
        break;
    }
    
	g_viraddr = pcim_iomap_table(pdev)[i];
    printk("se curaddr=0x%llx,ilen=%d\n",pci_resource_start(pdev,i),pci_resource_len(pdev,i));
    printk("se g_viraddr=0x%llx\n",pcim_iomap_table(pdev)[i]);

    se_chip_load();
    
	return 0;
}

static const struct pci_device_id ls2k_se_pci_ids[] = {
     {
         PCI_DEVICE(0x0014, 0x7a8e)
     },
 
     { /* end: all zeroes */ }
 };
MODULE_DEVICE_TABLE(pci, ls2k_se_pci_ids);

static struct  pci_driver  ls2k_se_pci_driver = {
    .name = "ls2k-se-pci",
   	.id_table = ls2k_se_pci_ids,
	.probe = se_plat_probe,
	.remove = se_plat_remove, 
};

static int __init ls2k_se_init(void)
{
    int ret;

	ret = platform_driver_register(&ls2k_se_driver);
	if (!ret)
	{
		ret = pci_register_driver(&ls2k_se_pci_driver);
	}

	return  ret;
}

static void __exit ls2k_se_exit(void)
{
	platform_driver_unregister(&ls2k_se_driver);
	
	pci_unregister_driver(&ls2k_se_pci_driver);
}
module_init(ls2k_se_init);
module_exit(ls2k_se_exit);

MODULE_AUTHOR("Loongson ls2k SE Interface driver");
MODULE_DESCRIPTION("se encryption chip driver Co westone");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

