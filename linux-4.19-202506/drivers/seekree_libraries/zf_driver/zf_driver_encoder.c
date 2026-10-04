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


#include "zf_driver_encoder_core.h"


/*
 * @description : 从设备读取数据
 * @param - filp : 要打开的设备文件(文件描述符)
 * @param - buf : 返回给用户空间的数据缓冲区
 * @param - cnt : 要读取的数据长度
 * @param - offt : 相对于文件首地址的偏移
 * @return : 读取的字节数，如果为负值，表示读取失败
 */
static ssize_t encoder_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	struct encoder_dev_struct *dev = filp->private_data;
	int ret = 0;
	int16_t count = 0;  // 修正：原代码int16未定义，应为int16_t
	unsigned long flags;  // 自旋锁标志位

	// 加自旋锁（关本地中断，避免中断打断临界区）
	spin_lock_irqsave(&dev->lock, flags);

	// 临界区：读取编码器计数（保护寄存器读写）
	count = encoder_get_count(dev);
	// printk("count = %d\r\n", count);

	// 上传数据（用户态拷贝非临界区，无需加锁）
	ret = copy_to_user(buf, (uint8_t *)&count, sizeof(count));  // 修正：uint8->uint8_t

	// 释放自旋锁（恢复中断）
	spin_unlock_irqrestore(&dev->lock, flags);

	if (ret < 0) {
		printk("err copy_to_user\r\n");
		return -EFAULT;  // 修正：返回标准错误码
	}

	return sizeof(count);  // 修正：原返回ret（拷贝失败数），应返回实际读取字节数
}

/*
 * @description : 向设备写入数据（清零计数）
 * @param - filp : 设备文件描述符
 * @param - buffer : 用户空间数据缓冲区
 * @param - size : 写入数据长度
 * @param - ppos : 文件偏移
 * @return : 写入字节数，负值表示失败
 */
static ssize_t encoder_write(struct file *filp, const char __user *buffer, size_t size, loff_t *ppos)
{
	struct encoder_dev_struct *dev = filp->private_data;
	unsigned long flags;

	// 加自旋锁（保护清零操作，避免与读操作并发）
	spin_lock_irqsave(&dev->lock, flags);

	// 临界区：清零编码器计数
	encoder_clear_count(dev);
	
	// 释放自旋锁
	spin_unlock_irqrestore(&dev->lock, flags);

	return size;  // 返回写入长度（标准操作）
}


static int encoder_release(struct inode * inode, struct file * filp)
{
	struct encoder_dev_struct *dev = filp->private_data;

	dev_err(&dev->pdev->dev, "encoder_release\n");

	return 0; // release函数固定返回0，内核会忽略非0返回值
}

static int encoder_open(struct inode *inode, struct file *filp)
{
	struct encoder_dev_struct *dev = filp->private_data;

	if (dev->device_type == NO_FIND_DEVICE) 
	{
		/* No such device */
		return -ENODEV;
	}

	return 0;
}


// 设备操作函数
static struct file_operations encoder_fops = {
	.owner = THIS_MODULE,
	.open  = encoder_open,
	.read  = encoder_read,
	.write = encoder_write,
	.release = encoder_release,
};

/*
 * @description		: platform驱动的probe函数（设备匹配后执行）
 * @param - dev 	: platform设备
 * @return 			: 0成功，负值失败
 */
static int encoder_probe(struct platform_device *pdev)
{
	struct encoder_dev_struct *dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *mem;
	int ret;
	const char *compatible_str;  // 临时存储compatible字符串
	int compatible_len;          // compatible属性长度


	printk("encoder driver and device was matched!\r\n");

	// 申请内存
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	// 设置指针
	dev->pdev = pdev;

	// 初始化自旋锁（关键：必须初始化后才能使用）
	spin_lock_init(&dev->lock);

	// 设置杂项设备参数
	dev->misc.minor = MISC_DYNAMIC_MINOR;

	// 方式1：读取compatible属性（推荐，支持多compatible值）
	compatible_str = of_get_property(np, "compatible", &compatible_len);
	if (!compatible_str) {
		dev_err(&pdev->dev, "get property <compatible> error\n");
		return -EINVAL;
	}

	if(strcmp(compatible_str, "seekfree,encoder_quad") == 0)
	{
		dev->device_type = ZF_DEVICE_ENCODER_QUAD;
	}
	else if(strcmp(compatible_str, "seekfree,encoder_dir") == 0)
	{
		dev->device_type = ZF_DEVICE_ENCODER_DIR;
	}
	

	// 获取设备名称
	if (of_property_read_string(np, "dev-name", &dev->misc.name)) {
		dev_err(&pdev->dev, "get property <dev-name> error\n");
		return -EINVAL;
	}
	dev_info(&pdev->dev, "find dev-name %s\n", dev->misc.name);

	// 设置文件操作结构体
	dev->misc.fops = &encoder_fops;
	dev->misc.parent = &pdev->dev;

	// 注册杂项设备
	ret = misc_register(&dev->misc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register misc device\n");
		return ret;
	}

	// 获取寄存器地址
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource\n");
		return -ENODEV;
	}

	// 映射寄存器
	dev->mmio_base = devm_ioremap_resource(&pdev->dev, mem);
	if (!dev->mmio_base) {
		dev_err(&pdev->dev, "mmio_base is null\n");
		return -ENOMEM;
	}

	// 加锁初始化编码器（保护初始化过程）
	unsigned long flags;
	spin_lock_irqsave(&dev->lock, flags);
	encoder_init(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	// 设置私有数据
	platform_set_drvdata(pdev, dev);

	return 0;
}

/*
 * @description		: platform驱动的remove函数（卸载驱动时执行）
 * @param - dev 	: platform设备
 * @return 			: 0成功，负值失败
 */
static int encoder_remove(struct platform_device *pdev)
{
	struct encoder_dev_struct *dev = platform_get_drvdata(pdev);

	// 释放GPIO（若启用）
	if (dev->dir_pin >= 0) {
		gpio_set_value(dev->dir_pin, 1);
		gpio_free(dev->dir_pin);
	}

	// 注销杂项设备
	misc_deregister(&dev->misc);

	return 0;
}

/* 设备树匹配列表 */
static const struct of_device_id encoder_of_match[] = {
	{ .compatible = "seekfree,encoder_quad" },
	{ .compatible = "seekfree,encoder_dir" },
	{ /* Sentinel */ }
};

/* platform驱动结构体 */
static struct platform_driver encoder_platform_driver = {
	.driver = {
		.name = "zf_driver_encoder",
		.of_match_table = encoder_of_match,
	},
	.probe = encoder_probe,
	.remove = encoder_remove,
};

/* 驱动模块加载函数 */
static int __init encoder_driver_init(void)
{
	return platform_driver_register(&encoder_platform_driver);
}

/* 驱动模块卸载函数 */
static void __exit encoder_driver_exit(void)
{
	platform_driver_unregister(&encoder_platform_driver);
}

module_init(encoder_driver_init);
module_exit(encoder_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");