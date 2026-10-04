#ifndef _zf_device_imu660rb_h_
#define _zf_device_imu660rb_h_

#include "../zf_common/zf_common_typedef.h"
#include "zf_device_imu_core.h"


#define IMU660RB_TIMEOUT_COUNT      (10)                            // IMU660 超时计数

//================================================定义 imu660rb 内部地址================================================
#define IMU660RB_DEV_ADDR           (0x6B)                              // SA0接地：0x68 SA0上拉：0x69 模块默认上拉
#define IMU660RB_SPI_W              (0x00)
#define IMU660RB_SPI_R              (0x80)


#define IMU660RB_FUNC_CFG_ACCESS    (0x01)
#define IMU660RB_CHIP_ID            (0x0F)

#define IMU660RB_INT1_CTRL          (0x0D)
#define IMU660RB_CTRL1_XL           (0x10)
#define IMU660RB_CTRL2_G            (0x11)
#define IMU660RB_CTRL3_C            (0x12)
#define IMU660RB_CTRL4_C            (0x13)
#define IMU660RB_CTRL5_C            (0x14)
#define IMU660RB_CTRL6_C            (0x15)
#define IMU660RB_CTRL7_G            (0x16)
#define IMU660RB_CTRL9_XL           (0x18)

#define IMU660RB_ACC_ADDRESS        (0x28)
#define IMU660RB_GYRO_ADDRESS       (0x22)

#define IMU660RB_ACC_SAMPLE         (0x3C)                      // 加速度计量程
// 设置为:0x30 加速度量程为:±2G      获取到的加速度计数据 除以16393，可以转化为带物理单位的数据，单位：g(m/s^2)
// 设置为:0x38 加速度量程为:±4G      获取到的加速度计数据 除以8197， 可以转化为带物理单位的数据，单位：g(m/s^2)
// 设置为:0x3C 加速度量程为:±8G      获取到的加速度计数据 除以4098， 可以转化为带物理单位的数据，单位：g(m/s^2)
// 设置为:0x34 加速度量程为:±16G     获取到的加速度计数据 除以2049， 可以转化为带物理单位的数据，单位：g(m/s^2)

#define IMU660RB_GYR_SAMPLE         (0x5C)                      // 陀螺仪量程
// 设置为:0x52 陀螺仪量程为:±125dps  获取到的陀螺仪数据除以228.6，   可以转化为带物理单位的数据，单位为：°/s
// 设置为:0x50 陀螺仪量程为:±250dps  获取到的陀螺仪数据除以114.3，   可以转化为带物理单位的数据，单位为：°/s
// 设置为:0x54 陀螺仪量程为:±500dps  获取到的陀螺仪数据除以57.1，    可以转化为带物理单位的数据，单位为：°/s
// 设置为:0x58 陀螺仪量程为:±1000dps 获取到的陀螺仪数据除以28.6，    可以转化为带物理单位的数据，单位为：°/s
// 设置为:0x5C 陀螺仪量程为:±2000dps 获取到的陀螺仪数据除以14.3，    可以转化为带物理单位的数据，单位为：°/s
// 设置为:0x51 陀螺仪量程为:±4000dps 获取到的陀螺仪数据除以7.1，     可以转化为带物理单位的数据，单位为：°/s



//================================================声明 IMU963RB 全局变量================================================
// extern int16 imu660rb_gyro_x, imu660rb_gyro_y, imu660rb_gyro_z;                   // 三轴陀螺仪数据      gyro (陀螺仪)
// extern int16 imu660rb_acc_x, imu660rb_acc_y, imu660rb_acc_z;                      // 三轴加速度计数据     acc (accelerometer 加速度计)
//================================================声明 IMU963RB 全局变量================================================


//================================================声明 IMU963RB 基础函数================================================
int16  imu660rb_get_acc            (struct imu_dev_struct *dev, int axis);       // 获取 IMU660RB 加速度计数据
int16  imu660rb_get_gyro           (struct imu_dev_struct *dev, int axis);       // 获取 IMU660RB 陀螺仪数据
uint8  imu660rb_init               (struct imu_dev_struct *dev);                   // 初始化 IMU660RB
//================================================声明 IMU963RB 基础函数================================================


#endif

