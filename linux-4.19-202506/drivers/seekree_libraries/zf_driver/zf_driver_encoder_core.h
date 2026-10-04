#ifndef _zf_driver_encoder_core_h_
#define _zf_driver_encoder_core_h_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>  // 新增：自旋锁头文件


#include "../zf_common/zf_common_typedef.h"


enum encoder_type {
    NO_FIND_DEVICE = 0,
    ZF_DEVICE_ENCODER_QUAD = 1,
    ZF_DEVICE_ENCODER_DIR = 2,
};


// encoder_dev设备结构体（新增自旋锁成员）
struct encoder_dev_struct
{
	struct miscdevice misc;			// 杂项设备
	void __iomem *mmio_base;		// 映射地址
	struct platform_device *pdev;	// 总线对象
	spinlock_t lock;				// 新增：自旋锁，保护共享资源
	uint8_t device_type;			// 设备类型 1->encoder_quad 2->encoder_dir
	int dir_pin;					// 仅方向编码器可用
};

int16 	encoder_get_count   (struct encoder_dev_struct *dev);
void  	encoder_clear_count (struct encoder_dev_struct *dev);
int 	encoder_init		(struct encoder_dev_struct *dev);


#endif