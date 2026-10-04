#ifndef _zf_device_dl1x_core_h_
#define _zf_device_dl1x_core_h_

#include <linux/spi/spi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/unaligned/be_byteshift.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/of_gpio.h>   
#include <linux/gpio.h>      
#include <linux/of.h>        


#include "../zf_common/zf_common_typedef.h"

enum dl1x_type {
    NO_FIND_DEVICE = 0,
    ZF_DEVICE_DL1A = 1,
    ZF_DEVICE_DL1B = 2,
};


struct dl1x_dev 
{
    struct i2c_client *client;      // I2C客户端对象
    uint8 dev_addr;                 // 设备I2C 从地址
    struct mutex i2c_lock;          // 保护I2C总线操作的互斥锁

    int xs_pin;
    int int_pin;

    uint8 inited;                   // 是否初始化
    uint8 type;                     // 设备类型
	uint16 (*dl1x_get_distance ) (struct dl1x_dev *dev);    // 距离获取回调函数
};


int iic_write_8bit_register(struct dl1x_dev *dev, uint8 iic_addr, uint8 reg, uint8 val);
int iic_write_8bit_array(struct dl1x_dev *dev, uint8 iic_addr, uint8 *buffer, uint32 len);
int iic_read_8bit_register(struct dl1x_dev *dev, uint8 iic_addr, uint8 reg, uint8 *val);
int iic_read_8bit_registers(struct dl1x_dev *dev, uint8 iic_addr, uint8 reg, uint8 *buf, int len);
int iic_transfer_8bit_array(struct dl1x_dev *dev, uint8 iic_addr, uint8 *w_buf, uint32 w_len, uint8 *r_buf, uint32 r_len);

void dl1x_type_print(struct dl1x_dev *dev);
int  dl1x_xs_gpio_init(struct dl1x_dev *dev);
int  dl1x_init(struct dl1x_dev *dev);

#endif


