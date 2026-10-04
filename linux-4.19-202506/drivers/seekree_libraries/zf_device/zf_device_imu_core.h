#ifndef _zf_device_imu_core_h_
#define _zf_device_imu_core_h_

#include "../zf_common/zf_common_typedef.h"

#include <linux/spinlock.h>

enum imu_type {
    NO_FIND_DEVICE = 0,
    ZF_DEVICE_IMU660RA,
    ZF_DEVICE_IMU660RB,
	ZF_DEVICE_IMU660RC,
	ZF_DEVICE_IMU963RA,
};

struct imu_dev_struct 
{
	// 硬件资源结构体
	uint8 type;							// 设备类型,查看enum imu_type

	struct spi_device *spi;				// spi设备
	struct regmap *regmap;				// 寄存器访问接口
	struct regmap_config reg_cfg;		// 寄存器访问接口

	struct gpio_desc *   int1_gpiod;				// int1中断引脚编号
	struct gpio_desc *   int2_gpiod;				// int2中断引脚编号
	int   int1_irq_num;					// int1中断号
	int   int2_irq_num;					// int2中断号

	// 传感器操作回调结构体
	int16 (*imu_get_acc ) (struct imu_dev_struct *dev, int axis);
	int16 (*imu_get_gyro) (struct imu_dev_struct *dev, int axis);
	int16 (*imu_get_mag ) (struct imu_dev_struct *dev, int axis);
    int16 (*imu_get_quat) (struct imu_dev_struct *dev, int axis);


	void (*imu_int2_irq_callback) 		(struct imu_dev_struct *dev);
	void (*imu_int2_thread_callback) 	(struct imu_dev_struct *dev);

	// 姿态解算浮点缓存结构体
	float transition_factor[2];			// 0 -> acc量程, 1 -> gyro量程
	float euler[3] ;					// 0 -> roll, 1 -> pitch, 2 -> yaw;

	int16 acc[3] ;						// 0 -> x, 1 -> y, 2 -> z;
	int16 gyro[3];						// 0 -> x, 1 -> y, 2 -> z;
	int16 mag[3] ;						// 0 -> x, 1 -> y, 2 -> z;

	uint16 quarternion[4];				// 0 -> w, 1 -> x, 2 -> y, 3 -> z;
	struct mutex read_raw_mutex_lock;	// 读互斥锁
	spinlock_t read_raw_spin_lock;		// 读自旋锁

};

int 	imu_device_irq_init(struct imu_dev_struct *dev);
int 	imu_device_init(struct imu_dev_struct *dev);
void 	imu_type_print(struct imu_dev_struct *dev);

#endif