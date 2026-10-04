#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/of_platform.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>

#define SPI_CR1	    0x00
#define SPI_CR2     0x04
#define SPI_CR3	    0x08
#define SPI_CR4	    0x0c
#define SPI_IER     0x10
#define SPI_SR1     0x14
#define SPI_SR2     0x18
#define SPI_CFG1    0x20
#define SPI_CFG2    0x24
#define SPI_CFG3    0x28
#define SPI_CRC1    0x30
#define SPI_CRC2    0x34
#define SPI_DR      0x40

/*2P500 SPICR1 bit fields*/
#define SPI_CR1_SSREV		BIT(8)
#define	SPI_CR1_AUTOSUS		BIT(2)
#define SPI_CR1_CSTART		BIT(1)
#define SPI_CR1_SPE		BIT(0)

/*2P500 SPICR2 bit fields*/
#define SPI_CR2_TXDMAEN		BIT(15)
#define SPI_CR2_TXFTHLV_SHIFT	8
#define SPI_CR2_TXFTHLV		GENMASK(9,8)
#define SPI_CR2_RXDMAEN		BIT(7)
#define SPI_CR2_RXFTHLV_SHIFT   0
#define SPI_CR2_RXFTHLV		GENMASK(1,0)

/*2P500 SPICR3 bit fields*/
#define SPI_CR3_TSIZE_SHIFT	0
#define SPI_CR3_TSIZE		GENMASK(15,0)

/*2P500 SPICR4 bit fields*/
#define SPI_CTSIZE_SHIFT	0
#define SPI_CTSIZE		GENMASK(15,0)

/*2P500 SPIIER bit fields*/
#define SPI_IER_EOTIE		BIT(15)
#define SPI_IER_MODFIE		BIT(11)
#define SPI_IER_CRCEIE		BIT(10)
#define SPI_IER_UDRIE		BIT(9)
#define SPI_IER_OVRIE		BIT(8)
#define SPI_IER_SUSPIE		BIT(7)
#define SPI_IER_TXEIE		BIT(6)
#define SPI_IER_RXEIE		BIT(4)
#define SPI_IER_DXAIE		BIT(2)
#define SPI_IER_TXAIE		BIT(1)
#define SPI_IER_RXAIE		BIT(0)

/*2P500 SPISR1 bit fields*/
#define SPI_SR1_EOT		BIT(15)
#define SPI_SR1_MODF		BIT(11)
#define SPI_SR1_CRCE		BIT(10)
#define SPI_SR1_UDR		BIT(9)
#define SPI_SR1_OVR		BIT(8)
#define SPI_SR1_SUSP		BIT(7)
#define SPI_SR1_TXE		BIT(6)
#define SPI_SR1_RXE		BIT(4)
#define SPI_SR1_DXA		BIT(2)
#define SPI_SR1_TXA		BIT(1)
#define SPI_SR1_RXA		BIT(0)

/*2P500 SPISR2 bit fields*/
#define SPI_SR2_TXFLV_SHIFT	8
#define SPI_SR2_TXFLV		GENMASK(10,8)
#define SPI_SR2_RXFLV_SHIFT	0
#define SPI_SR2_RXFLV		GENMASK(2,0)

/*2P500 CFG1 bit fields*/
#define SPI_CFG1_DSIZE_SHIFT	8
#define SPI_CFG1_DSIZE		GENMASK(12,8)
#define SPI_CFG1_LSBFRST	BIT(7)
#define	SPI_CFG1_CPHA		BIT(1)
#define SPI_CFG1_CPOL		BIT(0)

/*2P500 CFG2 bit fields*/
#define SPI_CFG2_BRINT_SHIFT    8
#define SPI_CFG2_BRINT		GENMASK(15,8)
#define SPI_CFG2_BRDEC_SHIFT    2
#define SPI_CFG2_BRDEC		GENMASK(7,2)

/*2P500 CFG3 bit fields*/
#define SPI_CFG3_SSMODE_SHIFT   8
#define SPI_CFG3_SSMODE		GENMASK(9,8)
#define SPI_CFG3_DOE		BIT(3)
#define SPI_CFG3_DIE		BIT(2)
#define SPI_CFG3_DIOSWP		BIT(1)
#define SPI_CFG3_MSTR		BIT(0)

#define MODE (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST)

#define DEFAULT_INPUT_FREQ	(100000000)
/* Max single transfer bytes for DMA (matches CR3 frame count limit for 8-bit data). */
#define LS_SPI_DMA_POOL_SIZE	65536

struct ls_spi_info{
	unsigned int bus_num;		/*spi_master bus_num*/
	unsigned int num_chipselect;    /*spi_master num_chipselect*/
	unsigned long max_clk;		/*spi max serial clock*/
	unsigned long clk_freq;		/*spi input clock freq*/
	struct spi_board_info	*board_info;   /*link to spi device*/
};

typedef enum{
	RX,
	TX
}DATATYPE;

struct ls_spi{
	struct device	       *dev;
	struct spi_master      *master;
	void   __iomem         *base;
	int			irq;
	spinlock_t              lock;

	u8                      spi_mode;
	u8			tx_trigger;
	u8			rx_trigger;
	u32			max_clk;
	u32			cur_speed;
	u32			cur_bpw;
	u32			cur_fthlv;
	u32			cur_xferlen;
	u32			cs_change;

	const void		*tx_buf;
	void			*rx_buf;
	int			tx_len;
	int			rx_len;
	struct ls_spi_info      *pdata;

	struct dma_chan		*dma_tx;
	struct dma_chan		*dma_rx;
	dma_addr_t		dr_phys;
	void			*dummy_tx;
	void			*dummy_rx;
	dma_addr_t		dummy_tx_dma;
	dma_addr_t		dummy_rx_dma;
	bool			using_dma;
	bool			dma_tx_mapped;
	bool			dma_rx_mapped;
	dma_addr_t		dma_tx_addr;
	dma_addr_t		dma_rx_addr;
	size_t			dma_xfer_len;
	bool			dma_tx_done;
	bool			dma_rx_done;
	bool			dma_eot_seen;
	struct work_struct	dma_work;
};

/* Forward declarations (needed for DMA workqueue path). */
static void ls_spi_disable(struct ls_spi *spi);


static	inline void spi_writel(struct ls_spi *spi,unsigned short offset, unsigned int value)
{
	writel(value,spi->base+offset);
}
static inline u32 spi_readl(struct ls_spi *spi, unsigned short offset)
{
	return readl(spi->base+offset);
}

static void ls_spi_drain_rx_fifo(struct ls_spi *spi)
{
	u32 rxflv;
	u32 tries = 64;

	do {
		rxflv = (spi_readl(spi, SPI_SR2) & SPI_SR2_RXFLV) >> SPI_SR2_RXFLV_SHIFT;
		if (!rxflv)
			break;
		(void)readb_relaxed(spi->base + SPI_DR);
	} while (tries--);
}

static enum dma_slave_buswidth ls_spi_dma_width(u32 bpw)
{
	if (bpw <= 8)
		return DMA_SLAVE_BUSWIDTH_1_BYTE;
	if (bpw <= 16)
		return DMA_SLAVE_BUSWIDTH_2_BYTES;
	return DMA_SLAVE_BUSWIDTH_4_BYTES;
}

static bool ls_spi_can_dma(struct ls_spi *spi, struct spi_transfer *t)
{
	size_t len;
	enum dma_slave_buswidth w;
	u32 bpw = t->bits_per_word;

	if (!spi->dma_tx || !spi->dma_rx || !spi->dummy_tx || !spi->dummy_rx)
		return false;
	if (!bpw)
		bpw = 8;
	len = t->len;
	if (!len || len > LS_SPI_DMA_POOL_SIZE)
		return false;
	w = ls_spi_dma_width(bpw);
	if (w == DMA_SLAVE_BUSWIDTH_2_BYTES && (len & 1))
		return false;
	if (w == DMA_SLAVE_BUSWIDTH_4_BYTES && (len & 3))
		return false;
	return true;
}

static void ls_spi_dma_unmap(struct ls_spi *spi)
{
	struct device *dev = spi->dev;

	if (spi->dma_rx_mapped) {
		dma_sync_single_for_cpu(dev, spi->dma_rx_addr, spi->dma_xfer_len,
					DMA_FROM_DEVICE);
		dma_unmap_single(dev, spi->dma_rx_addr, spi->dma_xfer_len,
				 DMA_FROM_DEVICE);
		spi->dma_rx_mapped = false;
	}
	if (spi->dma_tx_mapped) {
		dma_unmap_single(dev, spi->dma_tx_addr, spi->dma_xfer_len,
				 DMA_TO_DEVICE);
		spi->dma_tx_mapped = false;
	}
}

static void ls_spi_dma_tx_callback(void *param)
{
	struct ls_spi *spi = param;

	spi->dma_tx_done = true;
	if (spi->dma_eot_seen && spi->dma_rx_done)
		schedule_work(&spi->dma_work);
}

static void ls_spi_dma_rx_callback(void *param)
{
	struct ls_spi *spi = param;

	spi->dma_rx_done = true;
	if (spi->dma_eot_seen && spi->dma_tx_done)
		schedule_work(&spi->dma_work);
}

static void ls_spi_dma_work(struct work_struct *work)
{
	struct ls_spi *spi = container_of(work, struct ls_spi, dma_work);
	unsigned long flags;

	/* Ensure DMA engine is fully quiesced before unmap. */
	dmaengine_synchronize(spi->dma_tx);
	dmaengine_synchronize(spi->dma_rx);

	spin_lock_irqsave(&spi->lock, flags);
	if (!spi->using_dma) {
		spin_unlock_irqrestore(&spi->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&spi->lock, flags);

	ls_spi_dma_unmap(spi);

	spin_lock_irqsave(&spi->lock, flags);
	spi->using_dma = false;
	spi->tx_len = 0;
	spi->rx_len = 0;
	spi->dma_tx_done = false;
	spi->dma_rx_done = false;
	spi->dma_eot_seen = false;
	spin_unlock_irqrestore(&spi->lock, flags);

	spi_finalize_current_transfer(spi->master);
}

static void ls_spi_dma_abort(struct ls_spi *spi)
{
	if (!spi->using_dma)
		return;
	dmaengine_terminate_async(spi->dma_tx);
	dmaengine_terminate_async(spi->dma_rx);
	/*
	 * NOTE: do not call dmaengine_synchronize() from hardirq context.
	 * Abort can be called from IRQ handler; terminate_async() is IRQ-safe.
	 */
	ls_spi_dma_unmap(spi);
	spi->using_dma = false;
	spi->tx_len = 0;
	spi->rx_len = 0;
	spi->dma_tx_done = false;
	spi->dma_rx_done = false;
	spi->dma_eot_seen = false;
}

static int ls_spi_get_bpw_mask(struct ls_spi *spi)
{
	/*
	 * DSIZE field is 5 bits (GENMASK(12,8)), so it encodes 0..31 => 1..32 bits.
	 * Do not probe by reading registers; return the architectural limit.
	 */
	dev_dbg(spi->dev, "%d-bit maximum data frame\n", 32);
	return SPI_BPW_RANGE_MASK(4, 32);
}

static int ls_spi_prepare_br(struct ls_spi *spi, u32 speed_hz)
{
	u32 div;
	struct ls_spi_info *spi_info = spi->pdata;

	div=DIV_ROUND_UP(spi_info->clk_freq & ~0x1, speed_hz);

	if(div < 2 || div > 255){
		return -EINVAL;
	}

	spi->cur_speed = spi_info->clk_freq / div;
	return div;
}

static u32 ls_spi_prepare_fthlv(struct ls_spi *spi,DATATYPE data_type)
{
	u32 fthlv;

	if(data_type == RX){
		if(spi->rx_len > 3)
			fthlv =4;
		else if(spi->rx_len > 1)
			fthlv =2;
		else
			fthlv =1;
	}else{
		if(spi->tx_len > 3)
			fthlv =4;
		else if(spi->tx_len > 1)
			fthlv =2;
		else
			fthlv =1;

	}
	return fthlv;

}

static void ls_spi_enable(struct ls_spi *spi)
{
	dev_dbg(spi->dev,"enable spi controller\n");

	/* Software-controlled CS: don't use controller HW CS polarity bit. */
	spi_writel(spi, SPI_CR1, SPI_CR1_SPE);
}

static void ls_spi_set_cs(struct spi_device *spi_dev, bool enable)
{
	int level;

	if (!gpio_is_valid(spi_dev->cs_gpio))
		return;

	/* enable=true means assert CS. Keep idle high for active-low devices. */
	if (spi_dev->mode & SPI_CS_HIGH)
		level = enable ? 1 : 0;
	else
		level = enable ? 0 : 1;

	gpio_set_value(spi_dev->cs_gpio, level);
}

static void ls_spi_disable(struct ls_spi *spi)
{
	unsigned long flags;
	u32 sr;

	dev_dbg(spi->dev,"disable spi controller\n");

	spin_lock_irqsave(&spi->lock,flags);

	/*
	 * Wait for end-of-transfer (EOT). Note: EOT is typically W1C (sticky),
	 * so waiting for it to *clear* can hang forever if we never clear it.
	 */
	if (spi->using_dma) {
		/*
		 * Only wait while a DMA transfer is still in-flight. Once the
		 * transfer is finalized (using_dma cleared), waiting here just
		 * adds latency (often a full timeout).
		 */
		if (readl_relaxed_poll_timeout_atomic(spi->base + SPI_SR1, sr,
						      (sr & SPI_SR1_EOT),
						      10, 1000) < 0)
			dev_dbg_ratelimited(spi->dev, "EOT timeout, forcing SPI shutdown\n");
	}

	/* Clear EOT (W1C) so it doesn't trip future disables. */
	spi_writel(spi, SPI_SR1, SPI_SR1_EOT);

	if (spi->using_dma) {
		/*
		 * ls_spi_disable() may be called from IRQ handler in this driver.
		 * dmaengine_synchronize() is not IRQ-safe (may kill tasklets),
		 * so guard it.
		 */
		if (!in_interrupt()) {
			dmaengine_synchronize(spi->dma_tx);
			dmaengine_synchronize(spi->dma_rx);
		}
		ls_spi_dma_unmap(spi);
		spi->using_dma = false;
		spi->tx_len = 0;
		spi->rx_len = 0;
	}
	ls_spi_drain_rx_fifo(spi);

	/* Stop SPI-side DMA requests and interrupts unconditionally. */
	spi_writel(spi, SPI_CR2, 0);

	/*Disable interrupt and clear status flags*/
	spi_writel(spi, SPI_IER, 0);
	spi_writel(spi, SPI_SR1, 0xffffffff);

	/* Finally disable SPI engine (do not preserve any CR1 bits). */
	if (!spi->cs_change)
		spi_writel(spi, SPI_CR1, 0);

	spin_unlock_irqrestore(&spi->lock,flags);
}

/*ls SPI irq handler for SPI controller events*/
static irqreturn_t ls_spi_irq(int irq,void *dev)
{
	struct spi_master *master = dev;
	struct ls_spi *spi =spi_master_get_devdata(master);
	u32 sr;
	unsigned long flags;
	bool end =false;

	spin_lock_irqsave(&spi->lock, flags);

	sr = spi_readl(spi, SPI_SR1);
	if (!(sr & (SPI_SR1_EOT | SPI_SR1_MODF | SPI_SR1_OVR | SPI_SR1_SUSP))) {
		spin_unlock_irqrestore(&spi->lock, flags);
		return IRQ_NONE;
	}

	if (spi->using_dma) {
		if (sr & SPI_SR1_SUSP) {
			/* Clear SUSP (W1C) to avoid IRQ storm. */
			spi_writel(spi, SPI_SR1, SPI_SR1_SUSP);
			dev_dbg_ratelimited(spi->dev, "SPI suspend (DMA)\n");
		}

		if (sr & SPI_SR1_MODF) {
			dev_warn(spi->dev, "Mode failt: transfer aborted\n");
			end = true;
			spi_writel(spi, SPI_SR1, SPI_SR1_MODF);
			ls_spi_dma_abort(spi);
		}

		if (sr & SPI_SR1_OVR) {
			dev_warn(spi->dev, "Overrun!\n");
			spi_writel(spi, SPI_SR1, SPI_SR1_OVR);
			ls_spi_dma_abort(spi);
			end = true;
		}

		if (sr & SPI_SR1_EOT) {
			/*
			 * Do not unmap/synchronize in hardirq context.
			 * Defer completion until both DMA callbacks fired.
			 */
			spi->dma_eot_seen = true;
			end = true;
			spi_writel(spi, SPI_SR1, SPI_SR1_EOT);
		}

		spin_unlock_irqrestore(&spi->lock, flags);

		if (end) {
			/*
			 * Normal completion is deferred to workqueue (DMA callbacks + EOT).
			 * For abort paths (MODF/OVR), using_dma is cleared and we can
			 * finalize immediately.
			 */
			if (spi->using_dma) {
				if (spi->dma_tx_done && spi->dma_rx_done &&
				    spi->dma_eot_seen)
					schedule_work(&spi->dma_work);
			} else {
				spi_finalize_current_transfer(master);
			}
		}

		return IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&spi->lock, flags);
	dev_warn(spi->dev, "unexpected IRQ without DMA state (sr=0x%x)\n", sr);
	return IRQ_HANDLED;
}


static int ls_spi_setup(struct spi_device *spi_dev)
{
	struct ls_spi *spi;
	int ret;
	spi= spi_master_get_devdata(spi_dev->master);

	if (gpio_is_valid(spi_dev->cs_gpio)) {
		/* Drive CS to inactive level (idle state). */
		ret = gpio_direction_output(spi_dev->cs_gpio,
					    (spi_dev->mode & SPI_CS_HIGH) ? 0 : 1);
		if (ret)
			dev_warn(&spi_dev->dev, "failed to init cs gpio%d: %d\n",
				 spi_dev->cs_gpio, ret);
		/* Extra safety: force idle level even if direction already set. */
		ls_spi_set_cs(spi_dev, false);
	}

	return 0;
}

static int ls_spi_prepare_message(struct spi_master *master, struct spi_message *msg)
{
	/*
	 * Do not touch controller registers here.
	 * Full configuration is done in ls_spi_transfer_one_setup() by direct
	 * spi_writel() without reading/preserving previous register state.
	 */
	return 0;
}

static int ls_spi_transfer_one_dma(struct ls_spi *spi)
{
	unsigned long flags;
	struct dma_slave_config tx_cfg = {0}, rx_cfg = {0};
	struct dma_async_tx_descriptor *txd, *rxd;
	dma_cookie_t c_tx, c_rx;
	enum dma_slave_buswidth width = ls_spi_dma_width(spi->cur_bpw);
	int ret;
	struct device *dev = spi->dev;
	size_t len = spi->cur_xferlen;
	const void *txb = spi->tx_buf;
	void *rxb = spi->rx_buf;

	if (!txb) {
		memset(spi->dummy_tx, 0xff, len);
		dma_sync_single_for_device(dev, spi->dummy_tx_dma, len,
					    DMA_TO_DEVICE);
		spi->dma_tx_addr = spi->dummy_tx_dma;
		spi->dma_tx_mapped = false;
	} else {
		spi->dma_tx_addr = dma_map_single(dev, (void *)txb, len,
						   DMA_TO_DEVICE);
		if (dma_mapping_error(dev, spi->dma_tx_addr))
			return -EIO;
		spi->dma_tx_mapped = true;
	}

	if (!rxb) {
		spi->dma_rx_addr = spi->dummy_rx_dma;
		spi->dma_rx_mapped = false;
	} else {
		spi->dma_rx_addr = dma_map_single(dev, rxb, len,
						  DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, spi->dma_rx_addr)) {
			if (spi->dma_tx_mapped)
				dma_unmap_single(dev, spi->dma_tx_addr, len,
						 DMA_TO_DEVICE);
			spi->dma_tx_mapped = false;
			return -EIO;
		}
		spi->dma_rx_mapped = true;
	}

	spi->dma_xfer_len = len;

	tx_cfg.dst_addr = spi->dr_phys;
	tx_cfg.dst_addr_width = width;
	tx_cfg.dst_maxburst = 1;
	ret = dmaengine_slave_config(spi->dma_tx, &tx_cfg);
	if (ret)
		goto err_unmap;

	rx_cfg.src_addr = spi->dr_phys;
	rx_cfg.src_addr_width = width;
	rx_cfg.src_maxburst = 1;
	ret = dmaengine_slave_config(spi->dma_rx, &rx_cfg);
	if (ret)
		goto err_unmap;

	rxd = dmaengine_prep_slave_single(spi->dma_rx, spi->dma_rx_addr, len,
					  DMA_DEV_TO_MEM,
					  DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	txd = dmaengine_prep_slave_single(spi->dma_tx, spi->dma_tx_addr, len,
					  DMA_MEM_TO_DEV,
					  DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	if (!rxd || !txd) {
		ret = -EIO;
		goto err_unmap;
	}

	spi->dma_tx_done = false;
	spi->dma_rx_done = false;
	spi->dma_eot_seen = false;
	txd->callback = ls_spi_dma_tx_callback;
	txd->callback_param = spi;
	rxd->callback = ls_spi_dma_rx_callback;
	rxd->callback_param = spi;

	c_rx = dmaengine_submit(rxd);
	if (dma_submit_error(c_rx)) {
		ret = -EIO;
		goto err_unmap;
	}
	c_tx = dmaengine_submit(txd);
	if (dma_submit_error(c_tx)) {
		ret = -EIO;
		goto err_unmap;
	}

	spin_lock_irqsave(&spi->lock, flags);

	/* DMA mode uses full-duplex pins: DIOSWP=0, DIE=1, DOE=1 */
	spi_writel(spi, SPI_CFG3, SPI_CFG3_MSTR | SPI_CFG3_DIE | SPI_CFG3_DOE);

	ls_spi_enable(spi);
	spi_writel(spi, SPI_IER, 0);
	spi_writel(spi, SPI_SR1, 0xffffffff);

	spi->using_dma = true;

	/*
	 * For DMA transfers, use the smallest FIFO thresholds to generate
	 * DMA requests as early/often as possible (reduces TX underflow / RX
	 * overrun risk on long bursts).
	 */
	/* Smallest FIFO thresholds + DMA requests enabled (no read/modify). */
	spi_writel(spi, SPI_CR2, SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN);

	spi_writel(spi, SPI_IER,
		   SPI_IER_EOTIE | SPI_IER_OVRIE | SPI_IER_MODFIE | SPI_IER_UDRIE);

	/* Arm DMA engines, then start the SPI transfer. */
	dma_async_issue_pending(spi->dma_rx);
	dma_async_issue_pending(spi->dma_tx);

	/* Start transfer (do not preserve any other CR1 bits). */
	spi_writel(spi, SPI_CR1, SPI_CR1_SPE | SPI_CR1_CSTART);



	spin_unlock_irqrestore(&spi->lock, flags);

	return 1;

err_unmap:
	dmaengine_terminate_async(spi->dma_tx);
	dmaengine_terminate_async(spi->dma_rx);
	dmaengine_synchronize(spi->dma_tx);
	dmaengine_synchronize(spi->dma_rx);
	ls_spi_dma_unmap(spi);
	return ret;
}

static int ls_spi_transfer_one_setup(struct ls_spi *spi,struct spi_device *spi_dev,struct spi_transfer *transfer)
{
	unsigned long flags;
	u32 cfg1_setb =0,cfg1_clrb =0,cr2_setb=0, cr2_clrb=0, cfg2_setb =0,cfg2_clrb =0;

	u32 nb_words;
	u32 bpw, fthlv;
	int ret =0;
	u32 xfer_bpw;

	spin_lock_irqsave(&spi->lock, flags);
	spi_writel(spi, SPI_CR1, SPI_CR1_SPE);

	/*
	 * spidev commonly leaves transfer->bits_per_word = 0 to mean "use
	 * device default". Treat 0 as 8-bit to avoid DSIZE underflow and
	 * unintended 16/32-bit frame configuration (which reorders bytes).
	 */
	xfer_bpw = transfer->bits_per_word;
	if (!xfer_bpw)
		xfer_bpw = spi_dev->bits_per_word;
	if (!xfer_bpw)
		xfer_bpw = 8;

	spi->cur_bpw = xfer_bpw;
	transfer->bits_per_word = xfer_bpw;
	bpw = spi->cur_bpw - 1;

	cfg1_clrb |= SPI_CFG1_DSIZE;
	cfg1_setb |= (bpw << SPI_CFG1_DSIZE_SHIFT) & SPI_CFG1_DSIZE;

	spi->cur_fthlv = ls_spi_prepare_fthlv(spi, TX);
	fthlv = spi->cur_fthlv - 1;
	cr2_clrb |= SPI_CR2_TXFTHLV;
	cr2_setb |= (fthlv << SPI_CR2_TXFTHLV_SHIFT) & SPI_CR2_TXFTHLV;

	spi->cur_fthlv = ls_spi_prepare_fthlv(spi, RX);
	fthlv = spi->cur_fthlv - 1;
	cr2_clrb |= SPI_CR2_RXFTHLV;
	cr2_setb |= (fthlv << SPI_CR2_RXFTHLV_SHIFT) & SPI_CR2_RXFTHLV;

	/* Build full CFG1 from scratch: DSIZE + mode bits (no preserve). */
	{
		u32 cfg1 = 0;

		cfg1 |= cfg1_setb; /* contains DSIZE */
		if (spi_dev->mode & SPI_CPOL)
			cfg1 |= SPI_CFG1_CPOL;
		if (spi_dev->mode & SPI_CPHA)
			cfg1 |= SPI_CFG1_CPHA;
		if (spi_dev->mode & SPI_LSB_FIRST)
			cfg1 |= SPI_CFG1_LSBFRST;

		spi_writel(spi, SPI_CFG1, cfg1);
	}

	/* CR2 contains FIFO thresholds and DMA enables; write thresholds now. */
	spi_writel(spi, SPI_CR2, cr2_setb);

	if(spi->cur_speed != transfer->speed_hz){
		int br;

		br = ls_spi_prepare_br(spi, transfer->speed_hz);
		if(br <0){
			ret =-EMSGSIZE;
			goto out;
		}

		transfer->speed_hz = spi->cur_speed;

		cfg2_clrb |=SPI_CFG2_BRINT;
		cfg2_setb |=((u32)br << SPI_CFG2_BRINT_SHIFT) &SPI_CFG2_BRINT;

	}

	/* CFG2 only programs baud divider fields here; write full value. */
	spi_writel(spi, SPI_CFG2, cfg2_setb);

	/* CFG3: build full direction bits + master from scratch. */
	{
		u32 cfg3 = SPI_CFG3_MSTR;

		if (transfer->tx_buf && transfer->rx_buf) {
			cfg3 |= SPI_CFG3_DIE | SPI_CFG3_DOE;
		} else if (transfer->tx_buf) {
			cfg3 |= SPI_CFG3_DOE;
		} else if (transfer->rx_buf) {
			cfg3 |= SPI_CFG3_DIE;
		}
		/* DIOSWP stays 0 in all cases above. */
		spi_writel(spi, SPI_CFG3, cfg3);
	}

	/* CFG3 already written above from scratch. */

	if(spi->cur_bpw <= 8)
		nb_words = transfer->len;
	else if(spi->cur_bpw <= 16)
	        nb_words = DIV_ROUND_UP(transfer->len * 8, 16);
	else
		nb_words = DIV_ROUND_UP(transfer->len * 8, 32);
	nb_words <<= SPI_CR3_TSIZE_SHIFT;

	if(nb_words <= SPI_CR3_TSIZE){
		nb_words = nb_words - 1;
		spi_writel(spi, SPI_CR3, nb_words);
		spi_writel(spi, SPI_CR4, 0);
	}else{
		ret= -EMSGSIZE;
		goto out;
	}

	spi->cur_xferlen = transfer->len;

	dev_dbg(spi->dev,"data frame of %d bit,data packet of %d data frame",spi->cur_bpw,spi->cur_fthlv);
	dev_dbg(spi->dev,"speed set to %d Hz\n",spi->cur_speed);
	dev_dbg(spi->dev,"transfer of %d bytes (%d data frames)\n",spi->cur_xferlen,nb_words);

out:
	spin_unlock_irqrestore(&spi->lock, flags);

	return ret;
}

static int ls_spi_transfer_one(struct spi_master *master,struct spi_device *spi_dev,struct spi_transfer *transfer)
{
	struct ls_spi *spi =spi_master_get_devdata(master);
	int ret;

	if(transfer->len == 0)
		return 0;

	spi->tx_buf = transfer->tx_buf;
	spi->rx_buf = transfer->rx_buf;
	spi->tx_len =spi->tx_buf ? transfer->len : 0;
	spi->rx_len =spi->rx_buf ? transfer->len : 0;
	spi->cs_change = transfer->cs_change;

	ret = ls_spi_transfer_one_setup(spi,spi_dev,transfer);
	if(ret){
		dev_err(spi->dev,"SPI transfer setup failed\n");
		return ret;
	}

	if (!ls_spi_can_dma(spi, transfer))
		return -EINVAL;

	ret = ls_spi_transfer_one_dma(spi);
	return (ret < 0) ? ret : 1;
}

static int ls_spi_unprepare_msg(struct spi_master *master, struct spi_message *msg)
{
	struct ls_spi *spi =spi_master_get_devdata(master);

	ls_spi_disable(spi);
	/* Extra safety: release CS to idle at end-of-message. */
	ls_spi_set_cs(msg->spi, false);

	return 0;
}


#ifdef CONFIG_OF
static struct ls_spi_info *ls_spi_parse_dt(struct ls_spi *ls_spi)
{
	struct ls_spi_info *lsi;
	struct device *dev =ls_spi->dev;
	unsigned int value, clk_freq;

	lsi = devm_kzalloc(dev, sizeof(*lsi), GFP_KERNEL);
	if(!lsi){
		return ERR_PTR(-ENOMEM);
	}

	if (!of_property_read_u32(dev->of_node, "clock-frequency", &clk_freq)) {
		lsi->clk_freq = clk_freq;
		printk("clock-frequency not found, using default %lu Hz\n", lsi->clk_freq);
	} else {
		lsi->clk_freq = DEFAULT_INPUT_FREQ;
		pr_warn("clock-frequency not found, using default %lu Hz\n", lsi->clk_freq);
	}

	if(of_property_read_u32(dev->of_node, "spi-max-frequency", &value)) {
		dev_warn(dev, "spi-max-frequency not specified\n");
		lsi->max_clk = 0;
	} else {
		lsi->max_clk = value;
	}

	if (of_property_read_u32(dev->of_node, "num-cs", &value)) {
		dev_warn(dev, "num_cs not specified\n");
		lsi->num_chipselect = 0;
	} else {
		lsi->num_chipselect = value;
	}

	if (of_property_read_u32(dev->of_node, "ls,bus_num", &value)) {
		dev_warn(dev, "loongson,bus_num not specified\n");
		lsi->bus_num = 0;
	} else {
		lsi->bus_num = value;
	}

	return lsi;
}
#else
static struct ls_spi_info *ls_spi_parse_dt(struct device *dev)
{
	return dev_get_platdata(dev);
}
#endif

static const struct of_device_id ls_spi_match[] = {
	{.compatible = "loongson,ls-spi-dma",},
	{},
};
MODULE_DEVICE_TABLE(of, ls_spi_match);

static int  ls_spi_probe(struct platform_device *pdev)
{
	struct ls_spi      *ls_spi;
	struct spi_master  *master;
	struct resource    *res;
	struct ls_spi_info *pdata =dev_get_platdata(&pdev->dev);
	struct device_node *np  = pdev->dev.of_node;
	int ret =0;

	master =spi_alloc_master(&pdev->dev,sizeof(struct ls_spi));
	if(!master){
		dev_err(&pdev->dev,"Unable to alloc SPI Master\n");
		return -ENOMEM;
	}

	ls_spi = spi_master_get_devdata(master);
	ls_spi->dev = &pdev->dev;
	ls_spi->master = master;

	if(!pdata && np){
		pdata =ls_spi_parse_dt(ls_spi);
		if(IS_ERR(pdata))
			return PTR_ERR(pdata);
	}
	if(!pdata){
		dev_err(&pdev->dev,"platform data missing\n");
		return -ENODEV;
	}

	ls_spi->pdata =pdata;
	master->bus_num = -1;//(unsigned int)ls_spi->pdata->bus_num;
	// if(master->bus_num <0 || master->bus_num >4){
	// 	dev_err(&pdev->dev,"No this channel, bus_num = %d\n",master->bus_num);
	// }

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ls_spi->base = devm_ioremap_resource(&pdev->dev, res);
	if(IS_ERR(ls_spi->base)){
		ret =PTR_ERR(ls_spi->base);
		goto err_no_iomap;
	}

	ls_spi->using_dma = false;
	ls_spi->dma_tx_mapped = false;
	ls_spi->dma_rx_mapped = false;
	ls_spi->dr_phys = (dma_addr_t)res->start + SPI_DR;
	/* Bring controller into a known state (direct writes, no preserve). */
	spi_writel(ls_spi, SPI_CR1, 0);
	spi_writel(ls_spi, SPI_CR2, 0);
	spi_writel(ls_spi, SPI_CFG1, 0);
	spi_writel(ls_spi, SPI_CFG2, 0);
	spi_writel(ls_spi, SPI_CFG3, 0);
	spi_writel(ls_spi, SPI_IER, 0);
	spi_writel(ls_spi, SPI_SR1, 0xffffffff);

	ls_spi->dma_tx = dma_request_slave_channel_reason(&pdev->dev, "tx");
	if (IS_ERR(ls_spi->dma_tx)) {
		ret = PTR_ERR(ls_spi->dma_tx);
		ls_spi->dma_tx = NULL;
		if (ret == -EPROBE_DEFER)
			goto err_put_master;
		goto err_dma;
	}
	ls_spi->dma_rx = dma_request_slave_channel_reason(&pdev->dev, "rx");
	if (IS_ERR(ls_spi->dma_rx)) {
		ret = PTR_ERR(ls_spi->dma_rx);
		ls_spi->dma_rx = NULL;
		if (ret == -EPROBE_DEFER)
			goto err_put_master;
		goto err_dma;
	}
	ls_spi->dummy_tx = dmam_alloc_coherent(&pdev->dev, LS_SPI_DMA_POOL_SIZE,
					      &ls_spi->dummy_tx_dma, GFP_KERNEL);
	ls_spi->dummy_rx = dmam_alloc_coherent(&pdev->dev, LS_SPI_DMA_POOL_SIZE,
					      &ls_spi->dummy_rx_dma, GFP_KERNEL);
	if (!ls_spi->dummy_tx || !ls_spi->dummy_rx) {
		ret = -ENOMEM;
		goto err_dma;
	}

	dev_info(&pdev->dev, "DMA-only mode enabled (tx+rx)\n");

	ls_spi->irq = platform_get_irq(pdev, 0);
	if(ls_spi->irq <= 0){
		dev_err(&pdev->dev,"err irq : %d\n",ls_spi->irq);
		ret = -ENOENT;
		goto err_no_irq;
	}

	ret =devm_request_irq(&pdev->dev,ls_spi->irq,ls_spi_irq,0,pdev->name,master);
	if(ret){
		dev_err(&pdev->dev, "irq%d request failed: %d\n", ls_spi->irq,ret);
		goto err_register;
	}

	ls_spi->max_clk =ls_spi->pdata->max_clk;

	platform_set_drvdata(pdev,ls_spi);
	spin_lock_init(&ls_spi->lock);
	INIT_WORK(&ls_spi->dma_work, ls_spi_dma_work);

	/* */

	master->mode_bits = MODE;
	master->num_chipselect =ls_spi->pdata->num_chipselect;

	if (master->num_chipselect == 0) {
		dev_warn(&pdev->dev, "num_chipselect not specified, defaulting to 1\n");
		master->num_chipselect = 1;
	}

#ifdef CONFIG_OF
	master->dev.of_node = pdev->dev.of_node;
#endif
	master->prepare_message = ls_spi_prepare_message;
	master->unprepare_message = ls_spi_unprepare_msg;
	master->transfer_one = ls_spi_transfer_one;
	master->setup = ls_spi_setup;
	master->set_cs = ls_spi_set_cs;
	master->bits_per_word_mask = ls_spi_get_bpw_mask(ls_spi);

	ret = spi_register_master(master);
	if(ret < 0 ){
		dev_err(&pdev->dev, "spi master registration failed: %d\n",
			ret);
		goto err_register;
	}
	dev_info(&pdev->dev,"driver initialized\n");

	return 0;

err_dma:
	if (ls_spi->dma_tx) {
		dma_release_channel(ls_spi->dma_tx);
		ls_spi->dma_tx = NULL;
	}
	if (ls_spi->dma_rx) {
		dma_release_channel(ls_spi->dma_rx);
		ls_spi->dma_rx = NULL;
	}
err_put_master:
	spi_master_put(master);
	return ret;

err_register:
	free_irq(ls_spi->irq,ls_spi);
err_no_irq:
	iounmap(ls_spi->base);
err_no_iomap:
	release_resource(ls_spi->base);
	kfree(ls_spi->base);

	spi_master_put(master);
	return 0;
}

static int ls_spi_remove(struct platform_device *dev)
{
	struct ls_spi *ls_spi =platform_get_drvdata(dev);

	spi_unregister_master(ls_spi->master);
	if (ls_spi->dma_tx) {
		dma_release_channel(ls_spi->dma_tx);
		ls_spi->dma_tx = NULL;
	}
	if (ls_spi->dma_rx) {
		dma_release_channel(ls_spi->dma_rx);
		ls_spi->dma_rx = NULL;
	}
	spi_master_put(ls_spi->master);

	platform_set_drvdata(dev,NULL);

	// free_irq(ls_spi->irq,ls_spi);
	// iounmap(ls_spi->base);
	// release_resource(ls_spi->base);
	// kfree(ls_spi->base);

	// kfree(ls_spi);

	dev_info(&dev->dev,"driver remove\n");

	return 0;
}

#ifdef CONFIG_PM

static int ls_spi_suspend(struct platform_device *pdev, pm_message_t msg)
{
        struct ls_spi *ls_spi = platform_get_drvdata(pdev);
        unsigned long flags;

        spin_lock_irqsave(&ls_spi->lock, flags);

	spin_unlock_irqrestore(&ls_spi->lock, flags);

        return 0;
}

static int ls_spi_resume(struct platform_device *pdev)
{
        struct ls_spi *ls_spi = platform_get_drvdata(pdev);
        unsigned long   flags;


        spin_lock_irqsave(&ls_spi->lock, flags);

	spin_unlock_irqrestore(&ls_spi->lock, flags);

        return 0;
}

#else
#define ls_spi_suspend NULL
#define ls_spi_resume  NULL
#endif

static struct platform_driver ls_spidrv = {
        .probe      = ls_spi_probe,
        .remove         = ls_spi_remove,
        .suspend        = ls_spi_suspend,
        .resume         = ls_spi_resume,
        .driver         = {
                .name   = "spi-ls-dma",
                .of_match_table = ls_spi_match,
                .owner  = THIS_MODULE,
        },
};

module_platform_driver(ls_spidrv);

MODULE_AUTHOR("seekfree BigW <790875685@qq.com>");
MODULE_DESCRIPTION("Loongson SPI DMA driver");
MODULE_LICENSE("GPL");

