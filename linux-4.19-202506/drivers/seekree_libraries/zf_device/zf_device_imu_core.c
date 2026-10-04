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

#include "zf_device_imu_core.h"
#include "zf_device_imu660ra.h"
#include "zf_device_imu660rb.h"
#include "zf_device_imu660rc.h"
#include "zf_device_imu963ra.h"

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     INT2中断顶半部处理函数（硬中断上下文）
// 参数说明     irq             中断号
// 参数说明     dev_id          IMU设备结构体指针
// 返回参数     irqreturn_t     IRQ_WAKE_THREAD：唤醒中断线程底半部
// 使用示例     devm_request_threaded_irq(..., imu_int2_irq_handler, imu_int2_thread, ...);
// 备注信息     运行在硬中断上下文，仅做快速标记，禁止耗时/睡眠操作；通过互斥锁保护寄存器读取资源
//-------------------------------------------------------------------------------------------------------------------
static irqreturn_t imu_int2_irq_handler(int irq, void *dev_id)
{
    struct imu_dev_struct *dev = (struct imu_dev_struct *)dev_id;

    if (dev->imu_int2_irq_callback != NULL)
    {
        dev->imu_int2_irq_callback(dev);
    }

    return IRQ_WAKE_THREAD;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     INT2中断线程底半部处理函数（进程上下文）
// 参数说明     irq             中断号
// 参数说明     dev_id          IMU设备结构体指针
// 返回参数     irqreturn_t     IRQ_HANDLED：中断已正常处理完成
// 使用示例     devm_request_threaded_irq(..., imu_int2_irq_handler, imu_int2_thread, ...);
// 备注信息     运行在线程上下文，允许睡眠、SPI/regmap寄存器读写；互斥锁保证多线程访问互斥
//-------------------------------------------------------------------------------------------------------------------
static irqreturn_t imu_int2_thread(int irq, void *dev_id)
{
    struct imu_dev_struct *dev = (struct imu_dev_struct *)dev_id;

    if (dev->imu_int2_thread_callback != NULL)
    {
        mutex_lock(&dev->read_raw_mutex_lock);
        // 调用线程回调函数
        dev->imu_int2_thread_callback(dev);
        mutex_unlock(&dev->read_raw_mutex_lock);
    }

    return IRQ_HANDLED;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     根据设备类型打印IMU初始化成功日志
// 参数说明     dev             IMU设备结构体指针
// 返回参数     void
// 使用示例     imu_type_print(dev);
// 备注信息     识别ZF全系列IMU芯片，无匹配设备则打印未找到设备提示
//-------------------------------------------------------------------------------------------------------------------
void imu_type_print(struct imu_dev_struct *dev)
{
    switch (dev->type)
    {
        case ZF_DEVICE_IMU660RA:
        {
            dev_info(&dev->spi->dev, "IMU660RA init OK\n");
            break;
        }

        case ZF_DEVICE_IMU660RB:
        {
            dev_info(&dev->spi->dev, "IMU660RB init OK\n");
            break;
        }

        case ZF_DEVICE_IMU660RC:
        {
            dev_info(&dev->spi->dev, "IMU660RC init OK\n");
            break;
        }

        case ZF_DEVICE_IMU963RA:
        {
            dev_info(&dev->spi->dev, "IMU963RA init OK\n");
            break;
        }

        default:
        {
            dev_info(&dev->spi->dev, "NO FIND IMU DEV\n");
            break;
        }       
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     自动识别并初始化挂载的IMU设备
// 参数说明     dev             IMU设备结构体指针
// 返回参数     int             0->识别并初始化成功；-1->未识别到任何IMU设备
// 使用示例     int ret = imu_device_init(dev);
// 备注信息     识别优先级：IMU963RA > IMU660RC > IMU660RB > IMU660RA；匹配成功绑定对应数据读取函数指针，无设备清空函数指针
//-------------------------------------------------------------------------------------------------------------------
int imu_device_init(struct imu_dev_struct *dev)
{
    uint8 ret = 0;

    do
    {
        // IMU963里面的六轴和IMU660RB是同一个芯片，所以要先初始化IMU963RA
        if(imu963ra_init(dev) == 0)
        {
            dev->type = ZF_DEVICE_IMU963RA;
            dev->imu_get_acc = imu963ra_get_acc;
            dev->imu_get_gyro = imu963ra_get_gyro;
            dev->imu_get_mag = imu963ra_get_mag;
            break;
        }

        if (imu660rc_init(dev) == 0)
        {
            dev->type = ZF_DEVICE_IMU660RC;
            dev->imu_get_acc    =   imu660rc_get_acc;
            dev->imu_get_gyro   =   imu660rc_get_gyro;
            dev->imu_get_quat   =   imu660rc_get_quat;
            break;
        }

        if (imu660rb_init(dev) == 0)
        {
            dev->type = ZF_DEVICE_IMU660RB;
            dev->imu_get_acc = imu660rb_get_acc;
            dev->imu_get_gyro = imu660rb_get_gyro;
            break;
        }

        if (imu660ra_init(dev) == 0)
        {
            dev->type = ZF_DEVICE_IMU660RA;
            dev->imu_get_acc = imu660ra_get_acc;
            dev->imu_get_gyro = imu660ra_get_gyro;
            break;
        }

        dev->type = NO_FIND_DEVICE;
        dev->imu_get_acc = NULL;
        dev->imu_get_gyro = NULL;
        dev->imu_get_mag = NULL;
        ret = -1;

    } while (0);

    return ret;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU INT2上升沿中断资源初始化与注册
// 参数说明     dev             IMU设备结构体指针
// 返回参数     int             0-中断注册成功；负数-中断/GPIO资源申请失败
// 使用示例     int irq_ret = imu_device_irq_init(dev);
// 备注信息     仅IMU660RC支持INT2硬件中断；自动获取INT2 GPIO、转换中断号，注册上升沿触发线程化中断；其他型号销毁已申请中断资源
//-------------------------------------------------------------------------------------------------------------------
int imu_device_irq_init(struct imu_dev_struct *dev)
{
    int ret = 0;
    if(dev->type == ZF_DEVICE_IMU660RC)
    {
        if(dev->int2_irq_num <= 0)
        {
            // dev->imu_int2_irq_callback = imu660rc_get_reg_quat;
            dev->imu_int2_thread_callback = imu660rc_get_reg_quat;

            // 申请中断
            dev_info(&dev->spi->dev, "开始中断获取\n");
            // 中断获取+注册
            dev->int2_gpiod = devm_gpiod_get(&dev->spi->dev, "int2", GPIOD_IN);

            if (IS_ERR(dev->int2_gpiod))
            {
                dev_err(&dev->spi->dev, "获取 int-gpios 失败\n");
                return PTR_ERR(dev->int2_gpiod);
            }

            dev->int2_irq_num = gpiod_to_irq(dev->int2_gpiod);
            if (dev->int2_irq_num <= 0) 
            {
                dev_err(&dev->spi->dev, "GPIO 转中断失败\n");
                return -EINVAL;
            }

            // 注册上升沿中断，IMU660RC INT2配置为上升沿触发
            ret = devm_request_threaded_irq
            (
                &dev->spi->dev,
                dev->int2_irq_num,
                imu_int2_irq_handler,       // 中断顶半部
                imu_int2_thread,            // 线程底半部
                IRQF_TRIGGER_RISING,
                "imu_int2_irq",
                dev
            );

            if (ret == 0) 
            {
                dev_info(&dev->spi->dev, "中断注册成功！irq=%d\n", dev->int2_irq_num);
            } 
            else 
            {
                dev_err(&dev->spi->dev, "中断注册失败！\n");
            }
        }
    }
    else
    {
        if (dev->int2_irq_num > 0) 
        {
            dev_info(&dev->spi->dev, "imu_remove: disable_irq_nosync irq=%d\n", dev->int2_irq_num);
            disable_irq_nosync(dev->int2_irq_num);
            dev->int2_irq_num = -1;
        }
    }

    return ret;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");