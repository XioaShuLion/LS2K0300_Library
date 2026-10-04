
#include "zf_driver_atim.h"
#include "zf_driver_gtim.h"
#include "zf_driver_encoder_atim.h"
#include "zf_driver_encoder_gtim.h"
#include "zf_driver_encoder_core.h"


// GPIO 初始化+输入
int encoder_dir_pin_init(struct encoder_dev_struct *dev)
{
    int ret;

    // 从设备树中获取 GPIO 信息
    dev->dir_pin = of_get_named_gpio(dev->pdev->dev.of_node, "dir-gpios", 0);
    if (!gpio_is_valid(dev->dir_pin)) 
    {
        dev_err(&dev->pdev->dev, "Failed to get GPIO from device tree\n");
        return -ENODEV;
    }

    // 申请 GPIO 引脚
    ret = gpio_request(dev->dir_pin, "dir-gpios");
    if (ret < 0) 
    {
        dev_err(&dev->pdev->dev, "Failed to request dir-gpios: %d\n", ret);
        return ret;
    }
    
    dev_info(&dev->pdev->dev, "dev->dir_pin = %d\n",  dev->dir_pin);

    // 2. 设置 GPIO 为输出模式
    gpio_direction_input(dev->dir_pin);  // 先默认拉高（可选，保证初始态可控）
    // dev_info(&dev->client->dev, "gpio_direction_output\n");

    return 0;
}


int16 encoder_get_count(struct encoder_dev_struct *dev)
{
    int16 value = 0;
    if(dev->device_type == ZF_DEVICE_ENCODER_QUAD)
    {
        // 都是调用同一个寄存器，随便调用哪个都行
        // value = encoder_atim_get_count(dev->mmio_base);
        value = encoder_gtim_get_count(dev->mmio_base);
    }
    else if(dev->device_type == ZF_DEVICE_ENCODER_DIR)
    {
        value = encoder_gtim_get_count(dev->mmio_base);
        if(gpio_get_value(dev->dir_pin) == 0)
        {
            value = -value;
        }
    }

    return value;
}

void encoder_clear_count(struct encoder_dev_struct *dev)
{
    // 都是调用同一个寄存器，随便调用哪个都行
    // encoder_atim_clear_count(dev->mmio_base);
    encoder_gtim_clear_count(dev->mmio_base);
}



int encoder_init(struct encoder_dev_struct *dev)
{
    int ret = 0;
    if(dev->device_type == ZF_DEVICE_ENCODER_QUAD)
    {
        // 都是调用同一个寄存器，随便调用哪个都行
        // encoder_atim_quad_init(dev->mmio_base);
        encoder_gtim_quad_init(dev->mmio_base);
    }
    else if(dev->device_type == ZF_DEVICE_ENCODER_DIR)
    {
        // 都是调用同一个寄存器，随便调用哪个都行
        // encoder_atim_dir_init(dev->mmio_base);
        encoder_gtim_dir_init(dev->mmio_base);
        encoder_dir_pin_init(dev);
    }
    else
    {
        ret = -1;
    }

    return ret;
}