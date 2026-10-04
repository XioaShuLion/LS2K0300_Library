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

#include "zf_driver_encoder_gtim.h"

int encoder_gtim_dir_init(void *base_addr)
{
    gtim_typedef *GTIM = (gtim_typedef *)base_addr;
    
    GTIM->CR1   = 0x00; 	        // 关闭定时器
    GTIM->ARR   = 0xFFFF;           // 周期寄存器
    GTIM->PSC   = 0x00;             // 分频寄存器
    GTIM->SMCR  = 0x07 | 5 << 4;    // 外部计数模式 , 滤波后的CH1输入
    GTIM->CCER  = 0x00;             // 关闭所有输出
    GTIM->CCMR1 = 0x01;	            // 01:CC1 通道被配置为输入，IC1 映射在TI1 上 01
    GTIM->CR1   = 0x01; 	        // 开启定时器
      
    return 0;
}

int encoder_gtim_quad_init(void *base_addr)
{
    gtim_typedef *GTIM = (gtim_typedef *)base_addr;
    
    GTIM->CR1   = 0x00; 	    // 开启定时器
    GTIM->ARR   = 0xFFFF;       // 周期寄存器
    GTIM->PSC   = 0x00;         // 分频寄存器
    GTIM->SMCR  = 0x02;         // 010:编码器模式2-根据TI2 的电平，计数器在TI1 的边沿向上/向下计数
    GTIM->CCER  = 0x00;         // 关闭所有输出
    GTIM->CCMR1 = 0x0101;	    // 01:CC1 通道被配置为输入，IC1 映射在TI1 上 01:CC2 通道被配置为输入，IC2 映射在TI2 上
    GTIM->CR1   = 0x01; 	    // 开启定时器
      
    return 0;
}


int16 encoder_gtim_get_count(void *base_addr)
{
    return ((gtim_typedef *)base_addr)->CNT;
}

void encoder_gtim_clear_count(void *base_addr)
{
    ((gtim_typedef *)base_addr)->CNT = 0;
}
