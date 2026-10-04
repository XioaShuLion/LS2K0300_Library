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

#include "zf_device_imu660rb.h"


// int16 imu660rb_gyro_x = 0, imu660rb_gyro_y = 0, imu660rb_gyro_z = 0;            // 三轴陀螺仪数据   gyro (陀螺仪)
// int16 imu660rb_acc_x = 0, imu660rb_acc_y = 0, imu660rb_acc_z = 0;               // 三轴加速度计数据 acc  (accelerometer 加速度计)


//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RB 写寄存器
// 参数说明     reg             寄存器地址
// 参数说明     dat            数据
// 返回参数     void
// 使用示例     imu660rb_write_register(IMU660RB_PWR_CONF, 0x00);          	// 关闭高级省电模式
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
static void imu660rb_write_register(struct imu_dev_struct *dev, uint8 reg, uint8 dat)
{
	regmap_write(dev->regmap, reg, dat);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RB 写数据
// 参数说明     reg             寄存器地址
// 参数说明     dat            数据
// 返回参数     void
// 使用示例     imu660rb_write_registers(IMU660RB_INIT_DATA, imu660rb_config_file, sizeof(imu660rb_config_file));
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
// static void imu660rb_write_registers(uint8 reg, const uint8 *dat, uint32 len)
// {
//     //IMU660RB_CS(0);
//     spi_write_8bit_registers(IMU660RB_SPI, reg | IMU660RB_SPI_W, dat, len);
//     //IMU660RB_CS(1);
// }

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RB 读寄存器
// 参数说明     reg             寄存器地址
// 返回参数     uint8           数据
// 使用示例     imu660rb_read_register(IMU660RB_CHIP_ID);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
static uint8 imu660rb_read_register(struct imu_dev_struct *dev, uint8 reg)
{
    uint8 ret = 0; 
    uint32 dat = 0; 
    ret = regmap_read(dev->regmap, reg, &dat); 
    return dat & 0xFF; 
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RB 读数据
// 参数说明     reg             寄存器地址
// 参数说明     dat             数据缓冲区
// 参数说明     len             数据长度
// 返回参数     void
// 使用示例     imu660rb_read_registers(IMU660RB_ACC_ADDRESS, dat, 6);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
static void imu660rb_read_registers(struct imu_dev_struct *dev, uint8 reg, uint8 *dat, uint32 len)
{
    regmap_bulk_read(dev->regmap, reg, dat, len);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RB 自检
// 参数说明     void
// 返回参数     uint8           1-自检失败 0-自检成功
// 使用示例     imu660rb_self_check();
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
static uint8 imu660rb_self_check (struct imu_dev_struct *dev)
{
    uint8 dat = 0, return_state = 0;
    uint16 timeout_count = 0;
    do
    {
        if(timeout_count ++ > IMU660RB_TIMEOUT_COUNT)
        {
            return_state =  1;
            break;
        }
        dat = imu660rb_read_register(dev, IMU660RB_CHIP_ID);
        mdelay(1);
        // dev_info(&dev->spi->dev, "imu660rb_self_check dat = %d\r\n", dat);  
    }while(0x6B != dat);                                                        // 读取设备ID是否等于0X24，如果不是0X24则认为没检测到设备
	// printf("imu660rb_self_check = 0x%x\r\n", dat);
	return return_state;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取 IMU660RB 加速度计数据
// 参数说明     void
// 返回参数     void
// 使用示例     imu660rb_get_acc();                                             // 执行该函数后，直接查看对应的变量即可
// 备注信息     使用 SPI 的采集时间为69us
//             使用 IIC 的采集时间为126us        采集加速度计的时间与采集陀螺仪的时间一致的原因是都只是读取寄存器数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu660rb_get_acc (struct imu_dev_struct *dev, int axis)
{

    uint8_t dat[2];

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取加速度计数据
    imu660rb_read_registers(dev, IMU660RB_ACC_ADDRESS + (axis - IIO_MOD_X) * 2, dat, 2);
    // 将读取到的两个字节数据合并为一个16位整数

    return (int16_t)(((uint16_t)dat[1] << 8 | dat[0]));


}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取 IMU660RB 陀螺仪数据
// 参数说明     void
// 返回参数     void
// 使用示例     imu660rb_get_gyro();                                            // 执行该函数后，直接查看对应的变量即可
// 备注信息     使用 SPI 的采集时间为69us
//             使用 IIC 的采集时间为126us
//-------------------------------------------------------------------------------------------------------------------
int16 imu660rb_get_gyro (struct imu_dev_struct *dev, int axis)
{
    uint8_t dat[2];

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取陀螺仪数据
    imu660rb_read_registers(dev, IMU660RB_GYRO_ADDRESS + (axis - IIO_MOD_X) * 2, dat, 2);

    // 将读取到的两个字节数据合并为一个16位整数
    return (int16_t)(((uint16_t)dat[1] << 8 | dat[0]));

}

// //-------------------------------------------------------------------------------------------------------------------
// // 函数简介     将 IMU660RB 加速度计数据转换为实际物理数据
// // 参数说明     gyro_value      任意轴的加速度计数据
// // 返回参数     void
// // 使用示例     float dat = imu660rb_acc_transition(imu660rb_acc_x);           // 单位为 g(m/s^2)
// // 备注信息
// //-------------------------------------------------------------------------------------------------------------------
// float imu660rb_acc_transition (int16 acc_value)
// {
//     float acc_data = 0;
//     switch(IMU660RB_ACC_SAMPLE)
//     {
//         case 0x30: acc_data = (float)acc_value / 16393; break;                  // 0x30 加速度量程为:±2G      获取到的加速度计数据 除以 16393 ，可以转化为带物理单位的数据，单位：g(m/s^2)
//         case 0x38: acc_data = (float)acc_value / 8197;  break;                  // 0x38 加速度量程为:±4G      获取到的加速度计数据 除以 8197 ， 可以转化为带物理单位的数据，单位：g(m/s^2)
//         case 0x3C: acc_data = (float)acc_value / 4098;  break;                  // 0x3C 加速度量程为:±8G      获取到的加速度计数据 除以 4098 ， 可以转化为带物理单位的数据，单位：g(m/s^2)
//         case 0x34: acc_data = (float)acc_value / 2049;  break;                  // 0x34 加速度量程为:±16G     获取到的加速度计数据 除以 2049 ， 可以转化为带物理单位的数据，单位：g(m/s^2)
//         default: break;
//     }
//     return acc_data;
// }

// -------------------------------------------------------------------------------------------------------------------
// 函数简介     将 IMU660RB 陀螺仪数据转换为实际物理数据
// 参数说明     gyro_value      任意轴的陀螺仪数据
// 返回参数     void
// 使用示例     float dat = imu660rb_gyro_transition(imu660rb_gyro_x);         // 单位为°/s
// 备注信息
// -------------------------------------------------------------------------------------------------------------------
// float imu660rb_gyro_transition (int16 gyro_value)
// {
//     float gyro_data = 0;
//     switch(IMU660RB_GYR_SAMPLE)
//     {
//         case 0x52: gyro_data = (float)gyro_value / 228.6f;  break;              //  0x52 陀螺仪量程为:±125dps  获取到的陀螺仪数据除以 228.6，   可以转化为带物理单位的数据，单位为：°/s
//         case 0x50: gyro_data = (float)gyro_value / 114.3f;  break;              //  0x50 陀螺仪量程为:±250dps  获取到的陀螺仪数据除以 114.3，   可以转化为带物理单位的数据，单位为：°/s
//         case 0x54: gyro_data = (float)gyro_value / 57.1f;   break;              //  0x54 陀螺仪量程为:±500dps  获取到的陀螺仪数据除以 57.1，    可以转化为带物理单位的数据，单位为：°/s
//         case 0x58: gyro_data = (float)gyro_value / 28.6f;   break;              //  0x58 陀螺仪量程为:±1000dps 获取到的陀螺仪数据除以 28.6，    可以转化为带物理单位的数据，单位为：°/s
//         case 0x5C: gyro_data = (float)gyro_value / 14.3f;   break;              //  0x5C 陀螺仪量程为:±2000dps 获取到的陀螺仪数据除以 14.3，    可以转化为带物理单位的数据，单位为：°/s
//         case 0x51: gyro_data = (float)gyro_value / 7.1f;    break;              //  0x51 陀螺仪量程为:±4000dps 获取到的陀螺仪数据除以 7.1，     可以转化为带物理单位的数据，单位为：°/s
//         default: break;
//     }
//     return gyro_data;
// }

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化 IMU660RB
// 参数说明     void
// 返回参数     uint8           1-初始化失败 0-初始化成功
// 使用示例     imu660rb_init();
// 备注信息
//-------------------------------------------------------------------------------------------------------------------
uint8 imu660rb_init (struct imu_dev_struct *dev)
{
    uint8 return_state = 0;
    mdelay(20);                                                        // 等待设备上电成功

	
	
    do{
        imu660rb_write_register(dev, IMU660RB_FUNC_CFG_ACCESS, 0x00);       // 关闭HUB寄存器访问
        imu660rb_write_register(dev, IMU660RB_CTRL3_C, 0x01);               // 复位设备
        mdelay(2);
        imu660rb_write_register(dev, IMU660RB_FUNC_CFG_ACCESS, 0x00);       // 关闭HUB寄存器访问
        if(imu660rb_self_check(dev))                                        // IMU660RB 自检
        {
            // 如果程序在输出了断言信息 并且提示出错位置在这里
            // 那么就是 IMU660RB 自检出错并超时退出了
            // 检查一下接线有没有问题 如果没问题可能就是坏了
            // printf("imu660rb self check error.");
            return_state = 1;
            break;
        }
        imu660rb_write_register(dev, IMU660RB_INT1_CTRL, 0x03);                      // 开启陀螺仪 加速度数据就绪中断
        imu660rb_write_register(dev, IMU660RB_CTRL1_XL, IMU660RB_ACC_SAMPLE);        // 设置加速度计量程 ±8G 以及数据输出速率 52hz 以及加速度信息从第一级滤波器输出
        // IMU660RB_CTRL1_XL 寄存器
        // 设置为:0x30 加速度量程为:±2G      获取到的加速度计数据 除以16393，可以转化为带物理单位的数据，单位：g(m/s^2)
        // 设置为:0x38 加速度量程为:±4G      获取到的加速度计数据 除以8197， 可以转化为带物理单位的数据，单位：g(m/s^2)
        // 设置为:0x3C 加速度量程为:±8G      获取到的加速度计数据 除以4098， 可以转化为带物理单位的数据，单位：g(m/s^2)
        // 设置为:0x34 加速度量程为:±16G     获取到的加速度计数据 除以2049， 可以转化为带物理单位的数据，单位：g(m/s^2)

        imu660rb_write_register(dev, IMU660RB_CTRL2_G, IMU660RB_GYR_SAMPLE);         // 设置陀螺仪计量程 ±2000dps 以及数据输出速率 208hz
        // IMU660RB_CTRL2_G 寄存器
        // 设置为:0x52 陀螺仪量程为:±125dps  获取到的陀螺仪数据除以228.6，   可以转化为带物理单位的数据，单位为：°/s
        // 设置为:0x50 陀螺仪量程为:±250dps  获取到的陀螺仪数据除以114.3，   可以转化为带物理单位的数据，单位为：°/s
        // 设置为:0x54 陀螺仪量程为:±500dps  获取到的陀螺仪数据除以57.1，    可以转化为带物理单位的数据，单位为：°/s
        // 设置为:0x58 陀螺仪量程为:±1000dps 获取到的陀螺仪数据除以28.6，    可以转化为带物理单位的数据，单位为：°/s
        // 设置为:0x5C 陀螺仪量程为:±2000dps 获取到的陀螺仪数据除以14.3，    可以转化为带物理单位的数据，单位为：°/s
        // 设置为:0x51 陀螺仪量程为:±4000dps 获取到的陀螺仪数据除以7.1，     可以转化为带物理单位的数据，单位为：°/s

        imu660rb_write_register(dev, IMU660RB_CTRL3_C, 0x44);                        // 使能陀螺仪数字低通滤波器
        imu660rb_write_register(dev, IMU660RB_CTRL4_C, 0x02);                        // 使能数字低通滤波器
        imu660rb_write_register(dev, IMU660RB_CTRL5_C, 0x00);                        // 加速度计与陀螺仪四舍五入
        imu660rb_write_register(dev, IMU660RB_CTRL6_C, 0x00);                        // 开启加速度计高性能模式 陀螺仪低通滤波 133hz
        imu660rb_write_register(dev, IMU660RB_CTRL7_G, 0x00);                        // 开启陀螺仪高性能模式 关闭高通滤波
        imu660rb_write_register(dev, IMU660RB_CTRL9_XL, 0x01);                       // 关闭I3C接口

    }while(0);
    return return_state;
}


