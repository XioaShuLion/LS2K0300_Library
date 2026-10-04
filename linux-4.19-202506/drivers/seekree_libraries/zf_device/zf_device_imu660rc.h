#ifndef _zf_device_imu660rc_h_
#define _zf_device_imu660rc_h_

#include "../zf_common/zf_common_typedef.h"
#include "zf_device_imu_core.h"


typedef enum
{
    IMU660RC_MAIN_MEM_BANK  = 0x00,
    IMU660RC_HUB_MEM_BANK   = 0x40,
    IMU660RC_EMBED_MEM_BANK = 0x80,
}imu660rc_mem_bank_enum;



#define     IMU660RC_QUARTERNION_15HZ       ( 0 )       // 15 Hz
#define     IMU660RC_QUARTERNION_30HZ       ( 1 )       // 30 Hz
#define     IMU660RC_QUARTERNION_60HZ       ( 2 )       // 60 Hz
#define     IMU660RC_QUARTERNION_120HZ      ( 3 )       // 120Hz
#define     IMU660RC_QUARTERNION_240HZ      ( 4 )       // 240Hz
#define     IMU660RC_QUARTERNION_480HZ      ( 5 )       // 480Hz
#define     IMU660RC_QUARTERNION_DISABLE    ( 6 )       // 禁用四元数输出

#define     USE_INT_GET_ACC_GYRO            ( 0 )       // 0-不使用中断采集，1-使用中断采集
#define     QUARTERNION_RATE_CONFIG         ( IMU660RC_QUARTERNION_120HZ )   // 四元数输出频率配置

//================================================定义 imu660rb 内部地址================================================
#define IMU660RC_DEV_ADDR           ( 0x6B )                        // SA0接地：0x6A SA0上拉：0x6B 模块默认上拉
#define IMU660RC_SPI_W              ( 0x00 )
#define IMU660RC_SPI_R              ( 0x80 )
#define IMU660RC_TIMEOUT_COUNT      ( 10 )                          // IMU660RC 超时计数

#define IMU660RC_FUNC_CFG_ACCESS    ( 0x01 )
#define IMU660RC_FIFO_CRTL1         ( 0x07 )
#define IMU660RC_FIFO_CRTL2         ( 0x08 )
#define IMU660RC_FIFO_CRTL3         ( 0x09 )
#define IMU660RC_FIFO_CRTL4         ( 0x0A )
#define IMU660RC_INT2_CTRL          ( 0x0E )
#define IMU660RC_CHIP_ID            ( 0x0F )
#define IMU660RC_CTRL1              ( 0x10 )
#define IMU660RC_CTRL2              ( 0x11 )
#define IMU660RC_CTRL3              ( 0x12 )
#define IMU660RC_CTRL4              ( 0x13 )
#define IMU660RC_CTRL5              ( 0x14 )
#define IMU660RC_CTRL6              ( 0x15 )
#define IMU660RC_CTRL7              ( 0x16 )
#define IMU660RC_CTRL8              ( 0x17 )
#define IMU660RC_CTRL9              ( 0x18 )
#define IMU660RC_CTRL10             ( 0x19 )
#define IMU660RC_CTRL_STATUS        ( 0x1A )
#define IMU660RC_STATUS_REG         ( 0x1E )
#define IMU660RC_OUT_TEMP_L         ( 0x20 )
#define IMU660RC_OUT_TEMP_H         ( 0x21 )
#define IMU660RC_OUTX_L_G           ( 0x22 )
#define IMU660RC_OUTX_H_G           ( 0x23 )
#define IMU660RC_OUTY_L_G           ( 0x24 )
#define IMU660RC_OUTY_H_G           ( 0x25 )
#define IMU660RC_OUTZ_L_G           ( 0x26 )
#define IMU660RC_OUTZ_H_G           ( 0x27 )
#define IMU660RC_OUTX_L_A           ( 0x28 )
#define IMU660RC_OUTX_H_A           ( 0x29 )
#define IMU660RC_OUTY_L_A           ( 0x2A )
#define IMU660RC_OUTY_H_A           ( 0x2B )
#define IMU660RC_OUTZ_L_A           ( 0x2C )
#define IMU660RC_OUTZ_H_A           ( 0x2D )

#define IMU660RC_PAGE_SEL           ( 0x02 )
#define IMU660RC_EMB_FUNC_EN_A      ( 0x04 )
#define IMU660RC_PAGE_RW            ( 0x17 )      
#define IMU660RC_EMB_FUNC_FIFO_EN_A ( 0x44 )  
#define IMU660RC_SFLP_ODR           ( 0x5E )
#define IMU660RC_EMB_FUNC_CFG       ( 0x63 )


//================================================声明 IMU660RC 基础函数================================================
int16   imu660rc_get_acc            (struct imu_dev_struct *dev, int axis);         // 获取 IMU660RC 加速度计数据
int16   imu660rc_get_gyro           (struct imu_dev_struct *dev, int axis);         // 获取 IMU660RC 陀螺仪数据
int16   imu660rc_get_quat           (struct imu_dev_struct *dev, int axis);         // 通过寄存器读取四元素
void    imu660rc_get_reg_quat       (struct imu_dev_struct *dev);                   // 获取四元素数据
uint8   imu660rc_init               (struct imu_dev_struct *dev);                   // 初始化 IMU660RC
//================================================声明 IMU660RC 基础函数================================================

#endif

