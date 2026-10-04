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

#include "zf_device_imu.h"
#include "zf_device_imu_core.h"

//-------------------------------------------------------------------------------------------------------------------
// 结构体简介     IMU通道事件配置结构体
// 成员说明     type            事件类型：数值变化事件
// 成员说明     dir             事件方向：无方向区分
// 成员说明     mask_separate   独立掩码，使能事件开关
// 使用示例     static const struct iio_event_spec imu_channels_event;
// 备注信息     用于IIO子系统sysfs事件开关配置，控制设备重初始化事件
//-------------------------------------------------------------------------------------------------------------------
static const struct iio_event_spec imu_channels_event = 
{
	.type = IIO_EV_TYPE_CHANGE,
	.dir = IIO_EV_DIR_NONE,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

//-------------------------------------------------------------------------------------------------------------------
// 枚举简介     IMU扫描通道索引枚举
// 成员说明     INV_IMU_CHAN_ACC_X     加速度X轴扫描下标
// 成员说明     INV_IMU_CHAN_ACC_Y     加速度Y轴扫描下标
// 成员说明     INV_IMU_CHAN_ACC_Z     加速度Z轴扫描下标
// 成员说明     INV_IMU_CHAN_GYRO_X    陀螺仪X轴扫描下标
// 成员说明     INV_IMU_CHAN_GYRO_Y    陀螺仪Y轴扫描下标
// 成员说明     INV_IMU_CHAN_GYRO_Z    陀螺仪Z轴扫描下标
// 成员说明     INV_IMU_CHAN_MAG_X     磁力计X轴扫描下标
// 成员说明     INV_IMU_CHAN_MAG_Y     磁力计Y轴扫描下标
// 成员说明     INV_IMU_CHAN_MAG_Z     磁力计Z轴扫描下标
// 成员说明     INV_IMU_CHAN_QUAT      四元数统一扫描索引
// 使用示例     enum inv_imu_chan scan_idx = INV_IMU_CHAN_ACC_X;
// 备注信息     配合IIO通道scan_index使用，区分各传感器轴数据存储位置
//-------------------------------------------------------------------------------------------------------------------
enum inv_imu_chan
{
    INV_IMU_CHAN_ACC_X,
    INV_IMU_CHAN_ACC_Y,
    INV_IMU_CHAN_ACC_Z,
    INV_IMU_CHAN_GYRO_X,
    INV_IMU_CHAN_GYRO_Y,
    INV_IMU_CHAN_GYRO_Z,
    INV_IMU_CHAN_MAG_X,
    INV_IMU_CHAN_MAG_Y,
    INV_IMU_CHAN_MAG_Z,
    INV_IMU_CHAN_QUAT,

};

//-------------------------------------------------------------------------------------------------------------------
// 宏简介     标准三轴传感器IIO通道快速定义宏
// 参数说明     _type       IIO通道类型，IIO_ACCEL/IIO_ANGL_VEL/IIO_MAGN
// 参数说明     _channel2   轴修饰符，IIO_MOD_X/IIO_MOD_Y/IIO_MOD_Z
// 参数说明     _index      扫描索引，对应enum inv_imu_chan
// 使用示例     IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X);
// 备注信息     输出有符号16位大端原始数据，提供raw只读节点
//-------------------------------------------------------------------------------------------------------------------
#define IMU_CHAN(_type, _channel2, _index)                    \
    {                                                         \
        .type = _type,                                        \
        .modified = 1,                                        \
        .channel2 = _channel2,                                \
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	      \
        .scan_index = _index,                                 \
        .scan_type = {                                        \
                .sign = 's',                                  \
                .realbits = 16,                               \
                .storagebits = 16,                            \
                .shift = 0,                                   \
                .endianness = IIO_BE,                         \
                 },                                           \
    }

//-------------------------------------------------------------------------------------------------------------------
// 宏简介     四元数旋转量IIO通道快速定义宏
// 参数说明     _mod        四元数修饰符 IIO_MOD_QUATERNION
// 参数说明     _index      扫描索引，对应enum inv_imu_chan
// 使用示例     IMU_QUAT_CHAN(IIO_MOD_QUATERNION, INV_IMU_CHAN_QUAT);
// 备注信息     输出无符号16位大端原始数据，配套多值读取接口
//-------------------------------------------------------------------------------------------------------------------
#define IMU_QUAT_CHAN(_mod, _index)                   \
    {                                                         \
        .type = IIO_ROT,                                      \
        .modified = 1,                                        \
        .channel2 = _mod,                                     \
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	      \
        .scan_index = _index,                                 \
        .scan_type = {                                        \
                .sign = 'u',                                  \
                .realbits = 16,                               \
                .storagebits = 16,                            \
                .shift = 0,                                   \
                .endianness = IIO_BE,                        \
                 },                                           \
        .datasheet_name = #_mod,                              \
    }

//-------------------------------------------------------------------------------------------------------------------
// 数组简介     三轴加速度IIO通道配置数组
// 成员说明     依次为加速度X/Y/Z轴标准通道
// 使用示例     indio_dev->channels = imu_3axis_channels;
// 备注信息     仅适配无陀螺仪、无磁力计、无四元数的简易IMU
//-------------------------------------------------------------------------------------------------------------------
// 定义IMU的通道配置数组 3轴加速度
static const struct iio_chan_spec imu_3axis_channels[] = 
{
    // 加速度计X轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X),
    // 加速度计Y轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_IMU_CHAN_ACC_Y),
    // 加速度计Z轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_IMU_CHAN_ACC_Z),
};

//-------------------------------------------------------------------------------------------------------------------
// 数组简介     六轴加速度+陀螺仪IIO通道配置数组
// 成员说明     加速度三轴 + 陀螺仪三轴标准通道
// 使用示例     indio_dev->channels = imu_6axis_channels;
// 备注信息     适配6轴IMU，不含磁力计与四元数输出
//-------------------------------------------------------------------------------------------------------------------
// 定义IMU的通道配置数组 6轴加速度+陀螺仪
static const struct iio_chan_spec imu_6axis_channels[] = 
{
    // 加速度计X轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X),
    // 加速度计Y轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_IMU_CHAN_ACC_Y),
    // 加速度计Z轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_IMU_CHAN_ACC_Z),

    // 陀螺仪X轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_IMU_CHAN_GYRO_X),
    // 陀螺仪Y轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_IMU_CHAN_GYRO_Y),
    // 陀螺仪Z轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_IMU_CHAN_GYRO_Z),
};

//-------------------------------------------------------------------------------------------------------------------
// 数组简介     九轴加速度+陀螺仪+磁力计IIO通道配置数组
// 成员说明     加速度三轴+陀螺仪三轴+磁力计三轴+事件控制通道
// 使用示例     indio_dev->channels = imu_9axis_channels;
// 备注信息     适配9轴IMU，无四元数输出，末尾挂载事件开关节点
//-------------------------------------------------------------------------------------------------------------------
// 定义IMU的通道配置数组 9轴加速度+陀螺仪+磁力计
static const struct iio_chan_spec imu_9axis_channels[] = 
{
    // 加速度计X轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X),
    // 加速度计Y轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_IMU_CHAN_ACC_Y),
    // 加速度计Z轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_IMU_CHAN_ACC_Z),

    // 陀螺仪X轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_IMU_CHAN_GYRO_X),
    // 陀螺仪Y轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_IMU_CHAN_GYRO_Y),
    // 陀螺仪Z轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_IMU_CHAN_GYRO_Z),

    // 磁力计X轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_X, INV_IMU_CHAN_MAG_X),
    // 磁力计Y轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_Y, INV_IMU_CHAN_MAG_Y),
    // 磁力计Z轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_Z, INV_IMU_CHAN_MAG_Z),

    {
        .event_spec = &imu_channels_event,
        .num_event_specs = 1,
    }
};

//-------------------------------------------------------------------------------------------------------------------
// 数组简介     九轴+四元数完整版IIO通道配置数组
// 成员说明     加速度三轴+陀螺仪三轴+磁力计三轴+四元数通道+事件控制通道
// 使用示例     indio_dev->channels = imu_9axis_quat_channels;
// 备注信息     当前驱动默认使用，支持姿态四元数批量读取
//-------------------------------------------------------------------------------------------------------------------
// 定义IMU的通道配置数组 9轴+四元数
static const struct iio_chan_spec imu_9axis_quat_channels[] = 
{
    // 加速度计X轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X),
    // 加速度计Y轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_IMU_CHAN_ACC_Y),
    // 加速度计Z轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_IMU_CHAN_ACC_Z),

    // 陀螺仪X轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_IMU_CHAN_GYRO_X),
    // 陀螺仪Y轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_IMU_CHAN_GYRO_Y),
    // 陀螺仪Z轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_IMU_CHAN_GYRO_Z),

    // 磁力计X轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_X, INV_IMU_CHAN_MAG_X),
    // 磁力计Y轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_Y, INV_IMU_CHAN_MAG_Y),
    // 磁力计Z轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_Z, INV_IMU_CHAN_MAG_Z),

    // 四元数IIO_MOD_QUATERNION
    IMU_QUAT_CHAN(IIO_ROT, IIO_MOD_QUATERNION),

    // 事件通道放数组最后
    {
        .event_spec = &imu_channels_event,
        .num_event_specs = 1,
    }
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读取单轴加速度原始数据
// 参数说明     dev             IMU设备私有结构体指针
// 参数说明     axis            轴修饰符 IIO_MOD_X/Y/Z
// 参数说明     val             输出原始16位采样值
// 返回参数     int             IIO_VAL_INT 代表纯整数输出
// 使用示例     imu_get_acc_raw(dev, IIO_MOD_X, &val);
// 备注信息     校验函数指针有效性，空指针打印错误日志
//-------------------------------------------------------------------------------------------------------------------
static int imu_get_acc_raw (struct imu_dev_struct *dev, int axis, int *val)
{
    if(dev->imu_get_acc != NULL)
    {
        *val = dev->imu_get_acc(dev, axis);
    }
    else
    {
        dev_err(&dev->spi->dev, "dev->imu_get_acc = NULL\n");
    }
    return IIO_VAL_INT;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读取单轴陀螺仪原始数据
// 参数说明     dev             IMU设备私有结构体指针
// 参数说明     axis            轴修饰符 IIO_MOD_X/Y/Z
// 参数说明     val             输出原始16位采样值
// 返回参数     int             IIO_VAL_INT 代表纯整数输出
// 使用示例     imu_get_gyro_raw(dev, IIO_MOD_X, &val);
// 备注信息     校验函数指针有效性，空指针打印错误日志
//-------------------------------------------------------------------------------------------------------------------
static int imu_get_gyro_raw (struct imu_dev_struct *dev, int axis, int *val)
{
    if(dev->imu_get_gyro != NULL)
    {
        *val = dev->imu_get_gyro(dev, axis);
    }
    else
    {
        dev_err(&dev->spi->dev, "dev->imu_get_gyro = NULL\n");
    }

    return IIO_VAL_INT;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读取单轴磁力计原始数据
// 参数说明     dev             IMU设备私有结构体指针
// 参数说明     axis            轴修饰符 IIO_MOD_X/Y/Z
// 参数说明     val             输出原始16位采样值
// 返回参数     int             IIO_VAL_INT 代表纯整数输出
// 使用示例     imu_get_mag_raw(dev, IIO_MOD_X, &val);
// 备注信息     校验函数指针有效性，空指针打印错误日志并置0
//-------------------------------------------------------------------------------------------------------------------
static int imu_get_mag_raw (struct imu_dev_struct *dev, int axis, int *val)
{
    if(dev->imu_get_mag != NULL)
    {
        *val = dev->imu_get_mag(dev, axis);
    }
    else
    {
        dev_err(&dev->spi->dev, "dev->imu_get_mag = NULL\n");
    }

    return IIO_VAL_INT;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     批量读取四元数W/X/Y/Z四分量原始数据
// 参数说明     dev             IMU设备私有结构体指针
// 参数说明     vals            输出数组，存放4个分量
// 参数说明     val_len         输出有效数据长度，成功为4，失败为0
// 返回参数     int             IIO_VAL_INT_MULTIPLE 多整数返回标识
// 使用示例     imu_get_quat_multi_raw(dev, vals, &len);
// 备注信息     无四元数接口时数组清零并置长度为0
//-------------------------------------------------------------------------------------------------------------------
static int imu_get_quat_multi_raw (struct imu_dev_struct *dev, int *vals, int *val_len)
{
    uint8 i = 0;
    if (dev->imu_get_quat != NULL)
    {
        *val_len = 4; // 四元数有四个分量
        for(i = 0; i < 4; i++)
        {
            vals[i] = dev->imu_get_quat(dev, i);
        }
    }
    else
    {
        dev_err(&dev->spi->dev, "dev->imu_get_quat = NULL\n");
        for(i = 0; i < 4; i++)
        {
            vals[i] = 0;
        }
        *val_len = 0;
    }
    return IIO_VAL_INT_MULTIPLE;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IIO多通道统一原始数据读取入口
// 参数说明     indio_dev       IIO设备实例指针
// 参数说明     chan            当前访问通道描述符
// 参数说明     size            输出数组最大长度
// 参数说明     vals            存放读取到的原始数据
// 参数说明     val_len         输出有效数据个数
// 参数说明     mask            读取类型掩码，仅支持RAW原始数据
// 返回参数     int             IIO数据格式标识，负数代表读取失败
// 使用示例     IIO子系统内部自动回调
// 备注信息     互斥锁保护SPI/regmap并发访问；区分加速度/陀螺仪/磁力计/四元数分支
//-------------------------------------------------------------------------------------------------------------------
static int imu_rot_read_multi_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int size, int *vals, int *val_len,
				long mask)
{
	struct imu_dev_struct *dev = iio_priv(indio_dev);
	int ret = 0;

    mutex_lock(&dev->read_raw_mutex_lock);
	switch (mask) 
    {
        case IIO_CHAN_INFO_RAW:
        {
            switch (chan->type) 
            {
                case IIO_ROT:
                {
                    ret = imu_get_quat_multi_raw(dev, vals, val_len);
                    break;
                }
                case IIO_ACCEL:
                {
                    if (dev->imu_get_acc) 
                    {
                        ret = imu_get_acc_raw(dev, chan->channel2, vals);
                        *val_len = 1;
                    } 
                    else 
                    {
                        ret = -EINVAL;
                    }
                    break;
                }
                case IIO_ANGL_VEL:
                {
                    if (dev->imu_get_gyro) 
                    {
                        ret = imu_get_gyro_raw(dev, chan->channel2, vals);
                        *val_len = 1;
                    }
                    else 
                    {
                        ret = -EINVAL;
                    }
                    break;
                }
                case IIO_MAGN:
                {
                    if (dev->imu_get_mag) 
                    {
                        ret = imu_get_mag_raw(dev, chan->channel2, vals);
                        *val_len = 1;
                    } 
                    else 
                    {
                        ret = -EINVAL;
                    }
                    break;
                }
                default:
                    ret = -EINVAL;
                    break;
            }
            break;
        }
        default:
            ret = -EINVAL;
            break;
	}
    mutex_unlock(&dev->read_raw_mutex_lock);
	return ret;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读取事件开关状态（sysfs读接口）
// 参数说明     indio_dev       IIO设备实例指针
// 参数说明     chan            事件关联通道描述符
// 参数说明     type            事件类型
// 参数说明     dir             事件方向
// 返回参数     int             返回当前IMU设备型号枚举值
// 使用示例     cat /sys/bus/iio/devices/iio:deviceX/events/in_distance_change_en
// 备注信息     仅做状态查询占位，返回设备类型标识
//-------------------------------------------------------------------------------------------------------------------
static int imu_read_event_config(struct iio_dev *indio_dev, const struct iio_chan_spec *chan, enum iio_event_type type, enum iio_event_direction dir)
{
    struct imu_dev_struct *dev = iio_priv(indio_dev);
    // 返回设备类型
    return dev->type;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     写入事件开关控制（sysfs写接口）
// 参数说明     indio_dev       IIO设备实例指针
// 参数说明     chan            事件关联通道描述符
// 参数说明     type            事件类型
// 参数说明     dir             事件方向
// 参数说明     state           写入值，1触发重新初始化
// 返回参数     int             返回当前IMU设备型号枚举值
// 使用示例     echo 1 > /sys/bus/iio/devices/iio:deviceX/events/in_distance_change_en
// 备注信息     写入1时重新自动识别IMU、打印型号、初始化INT2中断
//-------------------------------------------------------------------------------------------------------------------
static int imu_write_event_config(struct iio_dev *indio_dev, const struct iio_chan_spec *chan,
    enum iio_event_type type, enum iio_event_direction dir, int state)
{
    struct imu_dev_struct *dev = iio_priv(indio_dev);

    // 重新初始化
    if(state == 1)
    {
        // 自动判断是 imu660ra、imu660rb、imu963ra
        imu_device_init(dev);

        // 输出设备信息
        imu_type_print(dev);

        // 初始化中断
        imu_device_irq_init(dev);
    }
    return dev->type;
}

//-------------------------------------------------------------------------------------------------------------------
// 结构体简介     IIO设备操作回调信息结构体
// 成员说明     read_raw_multi   多值原始数据读取回调
// 成员说明     write_event_config 事件开关写入回调
// 成员说明     read_event_config   事件开关读取回调
// 使用示例     static const struct iio_info imu_info = { ... };
// 备注信息     挂载至iio_dev->info，提供sysfs读写底层实现
//-------------------------------------------------------------------------------------------------------------------
// iio_info结构体变量
static const struct iio_info imu_info = {
    .read_raw_multi         = imu_rot_read_multi_raw,
	// .read_raw		        = imu_read_raw,
    .write_event_config     = imu_write_event_config,
    .read_event_config      = imu_read_event_config,
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化SPI regmap配置参数
// 参数说明     dev             IMU设备私有结构体指针
// 返回参数     void
// 使用示例     setup_regmap_config(dev);
// 备注信息     寄存器8bit、数值8bit；SPI读指令最高位需置1(0x80掩码)
//-------------------------------------------------------------------------------------------------------------------
static void setup_regmap_config(struct imu_dev_struct *dev)
{
    // 寄存器长度为 8 位
    dev->reg_cfg.reg_bits = 8;
    // 值长度为 8 位
    dev->reg_cfg.val_bits = 8;
    // 读掩码设置为 0X80，imu 使用 SPI 接口读的时候寄存器最高位应该为 1
    dev->reg_cfg.read_flag_mask = 0x80;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     填充iio_dev设备基础配置
// 参数说明     indio_dev       待配置IIO设备实例
// 参数说明     dev             IMU私有设备结构体指针
// 返回参数     void
// 使用示例     setup_iio_dev(indio_dev, dev);
// 备注信息     绑定父设备、回调函数、设备名、工作模式、通道数组
//-------------------------------------------------------------------------------------------------------------------
static void setup_iio_dev(struct iio_dev *indio_dev, struct imu_dev_struct *dev)
{
    // 设置 iio_dev 的父设备为当前 spi 设备
    indio_dev->dev.parent = &dev->spi->dev;
    // 关联 iio_dev 的操作函数信息
    indio_dev->info = &imu_info;
    // 设置 iio_dev 的名称
    indio_dev->name = "zf_device_imu";
    // 设置工作模式为直接模式，提供 sysfs 接口
    indio_dev->modes = INDIO_DIRECT_MODE;
    // 关联 iio_dev 的通道配置
    indio_dev->channels = imu_9axis_quat_channels;
    // 设置 iio_dev 的通道数量
    indio_dev->num_channels = ARRAY_SIZE(imu_9axis_quat_channels);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     SPI设备驱动探测初始化函数
// 参数说明     spi             匹配成功的SPI设备指针
// 返回参数     int             0-初始化成功；负数-资源申请/注册失败
// 使用示例     设备树匹配后内核自动调用
// 备注信息     分配IIO内存、创建regmap、SPI初始化、自动识别IMU、注册IIO、注册INT2上升沿中断
//-------------------------------------------------------------------------------------------------------------------
static int imu_probe(struct spi_device *spi)
{
    int ret;
    struct imu_dev_struct *dev;
    struct iio_dev *indio_dev;

    // 打印调试信息，表明进入驱动初始化流程
    dev_info(&spi->dev, "imu spi driver probe start\n");

    // 申请 iio_dev 内存
    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*dev));
    if (!indio_dev) {
        dev_err(&spi->dev, "Failed to allocate iio_dev memory \n");
        return -ENOMEM;
    }

    // 获取 imu_dev 结构体地址
    dev = iio_priv(indio_dev);
    dev->spi = spi;
    // 将 indio_dev 设置为 spi->dev 的 driver_data，方便后续通过 spi 设备获取 iio_dev
    spi_set_drvdata(spi, indio_dev);
    // 初始化互斥锁，用于保护对设备的并发访问
    mutex_init(&dev->read_raw_mutex_lock);

    // 初始化 regmap_config 设置
    setup_regmap_config(dev);

    // 初始化 spi 接口的 regmap
    dev->regmap = regmap_init_spi(spi, &dev->reg_cfg);
    if (IS_ERR(dev->regmap))
    {
        dev_err(&spi->dev, "Failed to initialize regmap for \n");
        ret = PTR_ERR(dev->regmap);
        return ret;
    }

    // 保存 spi 设备指针
    dev->spi = spi;

    // 初始化 spi_device，设置模式为 0
    spi->mode = SPI_MODE_0;
    
    // 应用 SPI 设备的设置
    ret = spi_setup(spi);
    if (ret < 0) 
    {
        dev_err(&spi->dev, "Failed to setup SPI device for\n");
        regmap_exit(dev->regmap);
        return ret;
    }

    // 自动判断是 imu660ra、imu660rb、imu963ra
    imu_device_init(dev);

    // 输出设备信息
    imu_type_print(dev);

    // 设置 iio_dev 的其他成员变量
    setup_iio_dev(indio_dev, dev);

    // 注册 iio_dev
    ret = iio_device_register(indio_dev);
    if (ret < 0) {
        dev_err(&spi->dev, "Failed to register iio device \n");
        regmap_exit(dev->regmap);
        dev->regmap = NULL;
        return -EPERM;
    }

    dev_info(&spi->dev, "imu spi driver probe success\n");

    imu_device_irq_init(dev);

    // 初始化成功，返回 0
    return 0;

}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     SPI设备驱动资源释放移除函数
// 参数说明     spi             待卸载SPI设备指针
// 返回参数     int             固定返回0，移除完成
// 使用示例     设备热拔插/模块卸载时内核自动调用
// 备注信息     关闭INT2中断、销毁regmap、注销IIO设备，释放底层硬件资源
//-------------------------------------------------------------------------------------------------------------------
static int imu_remove(struct spi_device *spi)
{
    // 从SPI设备中获取驱动数据
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct imu_dev_struct *dev = iio_priv(indio_dev);
	
    if (dev) 
    {
        if (dev->int2_irq_num > 0) 
        {
            dev_info(&spi->dev, "imu_remove: disable_irq_sync irq=%d\n", dev->int2_irq_num);
            disable_irq_nosync(dev->int2_irq_num);
            dev->int2_irq_num = -1;
        }
    }

	/* 删除regmap */ 
    if (dev && dev->regmap != NULL) 
    {
        regmap_exit(dev->regmap);
        dev->regmap = NULL; 
    }

    if (indio_dev != NULL) 
    {
        /* 注销IIO */
        iio_device_unregister(indio_dev);
        indio_dev = NULL;
    }

    // 打印移除成功信息
    dev_err(&spi->dev, "imu_remove: Successfully removed driver for device \n");

    return 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 数组简介     设备树兼容匹配表
// 成员说明     compatible = "seekfree,imu" 匹配DTS内节点compatible属性
// 使用示例     static const struct of_device_id imu_of_match[];
// 备注信息     SPI驱动与设备树节点绑定依据，末尾空成员做结束标记
//-------------------------------------------------------------------------------------------------------------------
// 设备树匹配表
static const struct of_device_id imu_of_match[] = {
	{ .compatible = "seekfree,imu" },
	{ /* Sentinel */ }
};

//-------------------------------------------------------------------------------------------------------------------
// 结构体简介     SPI驱动注册主结构体
// 成员说明     probe      设备探测初始化回调
// 成员说明     remove     设备卸载资源释放回调
// 成员说明     driver     底层驱动基础信息：模块所有者、驱动名、设备树匹配表
// 使用示例     spi_register_driver(&imu_driver);
// 备注信息     Linux SPI标准驱动框架载体
//-------------------------------------------------------------------------------------------------------------------
// IMU驱动结构体
static struct spi_driver imu_driver = {
	.probe = imu_probe,
	.remove = imu_remove,
	.driver = {
			.owner = THIS_MODULE,
		   	.name = "imu",
		   	.of_match_table = imu_of_match,
		   },
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动模块入口函数
// 参数说明     void
// 返回参数     int             0-驱动注册成功；负数-注册失败
// 使用示例     module_init(imu_driver_init);
// 备注信息     内核加载模块时自动执行，注册SPI驱动
//-------------------------------------------------------------------------------------------------------------------
static int __init imu_driver_init(void)
{
	return spi_register_driver(&imu_driver);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动模块出口函数
// 参数说明     void
// 返回参数     void
// 使用示例     module_exit(imu_driver_exit);
// 备注信息     卸载模块时自动执行，注销SPI驱动，释放驱动框架资源
//-------------------------------------------------------------------------------------------------------------------
static void __exit imu_driver_exit(void)
{
	spi_unregister_driver(&imu_driver);
}

module_init(imu_driver_init);
module_exit(imu_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");