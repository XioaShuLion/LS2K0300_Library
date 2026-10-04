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
#include <linux/math64.h>
#include <linux/kernel.h>

#include "zf_device_imu660rc.h"


// float imu660rc_transition_factor[2];

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660Rc 写单字节寄存器
// 参数说明     dev             IMU设备结构体指针
// 参数说明     reg             寄存器地址
// 参数说明     dat             待写入单字节数据
// 返回参数     void
// 使用示例     imu660rc_write_register(dev, IMU660RC_PWR_CONF, 0x00);          	// 关闭高级省电模式
// 备注信息     内部调用，基于regmap实现SPI寄存器写入
//-------------------------------------------------------------------------------------------------------------------
static void imu660rc_write_register(struct imu_dev_struct *dev, uint8 reg, uint8 dat)
{
	regmap_write(dev->regmap, reg, dat);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RC 批量写寄存器（旧裸机SPI接口，已注释废弃）
// 参数说明     reg             寄存器起始地址
// 参数说明     dat             待写入数据缓冲区
// 参数说明     len             写入数据字节长度
// 返回参数     void
// 使用示例     imu660rc_write_registers(IMU660RC_INIT_DATA, imu660rc_config_file, sizeof(imu660rc_config_file));
// 备注信息     旧裸机SPI实现，内核regmap版本不再使用，保留注释备查
//-------------------------------------------------------------------------------------------------------------------
// static void imu660rc_write_registers(uint8 reg, const uint8 *dat, uint32 len)
// {
//     spi_write_8bit_registers(IMU660RC_SPI, reg | IMU660RC_SPI_W, dat, len);
// }

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RC 读单字节寄存器
// 参数说明     dev             IMU设备结构体指针
// 参数说明     reg             寄存器地址
// 返回参数     uint8           寄存器读取的单字节数据
// 使用示例     imu660rc_read_register(dev, IMU660RC_CHIP_ID);
// 备注信息     内部调用，regmap读取后截取低8位有效数据
//-------------------------------------------------------------------------------------------------------------------
static uint8 imu660rc_read_register(struct imu_dev_struct *dev, uint8 reg)
{
    uint8 ret = 0; 
    uint32 dat = 0; 
    ret = regmap_read(dev->regmap, reg, &dat); 
    return dat & 0xFF; 
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RC 批量读取连续寄存器
// 参数说明     dev             IMU设备结构体指针
// 参数说明     reg             寄存器起始地址
// 参数说明     dat             读取数据存放缓冲区
// 参数说明     len             读取字节长度
// 返回参数     void
// 使用示例     imu660rc_read_registers(dev, IMU660RC_ACC_ADDRESS, dat, 6);
// 备注信息     内部调用，regmap批量读取连续地址寄存器
//-------------------------------------------------------------------------------------------------------------------
static void imu660rc_read_registers(struct imu_dev_struct *dev, uint8 reg, uint8 *dat, uint32 len)
{
    regmap_bulk_read(dev->regmap, reg, dat, len);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RC 切换存储分区Bank
// 参数说明     dev             IMU设备结构体指针
// 参数说明     bank            目标存储分区枚举值
// 返回参数     void
// 使用示例     imu660rc_set_mem_bank(dev, IMU660RC_EMBED_MEM_BANK);
// 备注信息     内部调用，访问嵌入式功能寄存器前必须切换分区
//-------------------------------------------------------------------------------------------------------------------
static void imu660rc_set_mem_bank (struct imu_dev_struct *dev, imu660rc_mem_bank_enum bank)
{
    
    imu660rc_write_register(dev, IMU660RC_FUNC_CFG_ACCESS, bank);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RC 硬件设备自检
// 参数说明     dev             IMU设备结构体指针
// 返回参数     uint8           1-自检超时失败 0-读取正确芯片ID自检成功
// 使用示例     imu660rc_self_check(dev);
// 备注信息     内部调用，循环读取芯片ID 0x70，超时判定硬件未挂载或故障
//-------------------------------------------------------------------------------------------------------------------
static uint8 imu660rc_self_check (struct imu_dev_struct *dev)
{
    uint8 dat = 0, return_state = 0;
    uint16 timeout_count = 0;
    do
    {
        if(timeout_count ++ > IMU660RC_TIMEOUT_COUNT)
        {
            return_state =  1;
            break;
        }
        dat = imu660rc_read_register(dev, IMU660RC_CHIP_ID);
        mdelay(1);
        dev_info(&dev->spi->dev, "imu660rc_self_check dat = %d\r\n", dat);  
    }while(0x70 != dat);                                                        // 读取设备ID是否等于0x70，如果不是0x70则认为没检测到设备
	// printf("imu660rc_self_check = 0x%x\r\n", dat);
	return return_state;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取单轴加速度原始数据
// 参数说明     dev             IMU设备结构体指针
// 参数说明     axis            轴标识：1-X 2-Y 3-Z
// 返回参数     int16           对应轴16位加速度原始采样值
// 使用示例     int16 acc_x = imu660rc_get_acc(dev, 1);
// 备注信息     启用INT2上升沿中断缓存时直接读取缓存；否则实时读取寄存器拼接高低字节
//-------------------------------------------------------------------------------------------------------------------
int16 imu660rc_get_acc (struct imu_dev_struct *dev, int axis)
{
#if USE_INT_GET_ACC_GYRO
    return  dev->acc[axis - IIO_MOD_X];
#else
    uint8_t dat[2] = {0};

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取加速度计数据
    imu660rc_read_registers(dev, IMU660RC_OUTX_L_A + (axis - IIO_MOD_X) * 2, dat, 2);

    return (int16_t)(((uint16_t)dat[1] << 8 | dat[0]));
#endif
    
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取单轴陀螺仪原始数据
// 参数说明     dev             IMU设备结构体指针
// 参数说明     axis            轴标识：1-X 2-Y 3-Z
// 返回参数     int16           对应轴16位陀螺仪原始采样值
// 使用示例     int16 gyr_z = imu660rc_get_gyro(dev, 3);
// 备注信息     启用INT2上升沿中断缓存时直接读取缓存；否则实时读取寄存器拼接高低字节
//-------------------------------------------------------------------------------------------------------------------
int16 imu660rc_get_gyro (struct imu_dev_struct *dev, int axis)
{
#if USE_INT_GET_ACC_GYRO
    return  dev->gyro[axis - IIO_MOD_X];

#else
    uint8_t dat[2];

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取陀螺仪数据
    imu660rc_read_registers(dev, IMU660RC_OUTX_L_G + (axis - IIO_MOD_X) * 2, dat, 2);

    return (int16_t)(((uint16_t)dat[1] << 8 | dat[0]));

#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取指定下标四元数缓存数据
// 参数说明     dev             IMU设备结构体指针
// 参数说明     axis            四元数分量下标
// 返回参数     int16           对应分量四元数原始数值
// 使用示例     int16 q0 = imu660rc_get_quat(dev, 0);
// 备注信息     直接读取设备缓存中已更新的四元数数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu660rc_get_quat (struct imu_dev_struct *dev, int axis)
{
    return dev->quarternion[axis];
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读取寄存器四元数并更新本地缓存
// 参数说明     dev             IMU设备结构体指针
// 返回参数     void
// 使用示例     imu660rc_get_reg_quat(dev);
// 备注信息     切换嵌入式存储分区读取8字节四元数原始数据；开启INT2中断缓存时同步刷新加速度、陀螺仪缓存
//-------------------------------------------------------------------------------------------------------------------
void imu660rc_get_reg_quat(struct imu_dev_struct *dev)
{
    uint8   i;
    uint8   *buff_ptr = (uint8 *)dev->quarternion;

    imu660rc_set_mem_bank(dev, IMU660RC_EMBED_MEM_BANK);
    imu660rc_write_register(dev, IMU660RC_PAGE_RW, 0x20);
    imu660rc_write_register(dev, IMU660RC_PAGE_SEL, 0x31);
    
    for(i = 0; 8 > i; i++) 
    {
        imu660rc_write_register(dev, 0x08, 0x4C + i);
        buff_ptr[i] = imu660rc_read_register(dev, 0x09);
    }

    imu660rc_write_register(dev, IMU660RC_PAGE_RW, 0x00);
    imu660rc_set_mem_bank(dev, IMU660RC_MAIN_MEM_BANK);

#if USE_INT_GET_ACC_GYRO

    int16 buff2_ptr[6] = {0};

    imu660rc_read_registers(dev, IMU660RC_OUTX_L_A, (uint8 *)buff2_ptr, 6);
    dev->acc[0] = buff2_ptr[0];
    dev->acc[1] = buff2_ptr[1];
    dev->acc[2] = buff2_ptr[2];

    imu660rc_read_registers(dev, IMU660RC_OUTX_L_G, (uint8 *)buff2_ptr, 6);
    dev->gyro[0] = buff2_ptr[0];
    dev->gyro[1] = buff2_ptr[1];
    dev->gyro[2] = buff2_ptr[2];
#endif

}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RC 传感器初始化配置
// 参数说明     dev             IMU设备结构体指针
// 返回参数     uint8           1-自检失败初始化失败 0-全部寄存器配置完成初始化成功
// 使用示例     uint8 init_ret = imu660rc_init(dev);
// 备注信息     包含上电延时、硬件自检、传感器复位、量程/滤波/采样率配置；开启四元数时配置INT2上升沿硬件中断采集四元素、加速度计、陀螺仪数据。
//-------------------------------------------------------------------------------------------------------------------
uint8 imu660rc_init (struct imu_dev_struct *dev)
{
    uint8 return_state = 0;
    mdelay(20);                                                         // 等待设备上电成功

    do{
        if(imu660rc_self_check(dev))                                    // IMU660RC 自检
        {
            // 如果程序在输出了断言信息 并且提示出错位置在这里
            // 那么就是 IMU660RC 自检出错并超时退出了
            // 检查一下接线有没有问题 如果没问题可能就是坏了
            // printf("imu660rc self check error.");
            // dev_info(&dev->spi->dev, "imu660rc self check error.\n");
            return_state = 1;
            break;
        }

        // 复位传感器
        imu660rc_write_register(dev, IMU660RC_FUNC_CFG_ACCESS, 0x04);
        mdelay(30);
        // 启用块更新功能与地址递增
        imu660rc_write_register(dev, IMU660RC_CTRL3, 0x44);        // 设置加速度计量程 ±8G 以及数据输出速率 52hz 以及加速度信息从第一级滤波器输出

        // 设置加速度计量程 ±8G 
        imu660rc_write_register(dev, IMU660RC_CTRL8, 0x02);
        // 设置陀螺仪量程 ±2000dps
        imu660rc_write_register(dev, IMU660RC_CTRL6, 0x04);

        // 设置传感器模式，设置为高精度模式以及输出速率
        imu660rc_write_register(dev, IMU660RC_CTRL1, 0x15);
        imu660rc_write_register(dev, IMU660RC_CTRL2, 0x18);

        // 开启LPF1滤波器
        imu660rc_write_register(dev, IMU660RC_CTRL7, 0x01);
        
        // 开启LPF2滤波器
        imu660rc_write_register(dev, IMU660RC_CTRL9, 0x08);


        // 如果启用四元数输出，则配置相应数据
        #if(IMU660RC_QUARTERNION_DISABLE != QUARTERNION_RATE_CONFIG)
        {
            // 设置中断触发信号
            imu660rc_write_register(dev, IMU660RC_FIFO_CRTL1, 0x01);
            imu660rc_write_register(dev, IMU660RC_FIFO_CRTL4, 0x06);
            imu660rc_write_register(dev,IMU660RC_INT2_CTRL, 0x80);
            imu660rc_write_register(dev,IMU660RC_CTRL4, 0x08);
            
            // 重新配置加速度、角速度输出速率
            imu660rc_write_register(dev,IMU660RC_CTRL1, 0x10 | (QUARTERNION_RATE_CONFIG + 3));
            imu660rc_write_register(dev,IMU660RC_CTRL2, 0x10 | (QUARTERNION_RATE_CONFIG + 3));
            
            // 设置四元数输出速率并开启
            imu660rc_set_mem_bank(dev, IMU660RC_EMBED_MEM_BANK);
            imu660rc_write_register(dev, IMU660RC_EMB_FUNC_FIFO_EN_A, 0x02);
            imu660rc_write_register(dev, IMU660RC_SFLP_ODR, 0x43 | (QUARTERNION_RATE_CONFIG << 3));
            imu660rc_write_register(dev, IMU660RC_EMB_FUNC_EN_A, 0x02);
            imu660rc_write_register(dev, IMU660RC_PAGE_RW, 0x00);
            imu660rc_set_mem_bank(dev, IMU660RC_MAIN_MEM_BANK);
        }
        #endif

    }while(0);
    return return_state;
}