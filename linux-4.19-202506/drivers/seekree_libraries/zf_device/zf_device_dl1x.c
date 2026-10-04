#include "zf_device_dl1x_core.h"
#include "zf_device_dl1x.h"

static const struct iio_event_spec dl1x_channels_event = 
{
	.type = IIO_EV_TYPE_CHANGE,
	.dir = IIO_EV_DIR_NONE,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};


static const struct iio_chan_spec dl1x_channels[] = 
{
    {
        .type = IIO_DISTANCE,
        .address = 0,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        
    },
    {
        .event_spec = &dl1x_channels_event,
        .num_event_specs = 1,
    }
};

static int dl1x_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct dl1x_dev *dev = iio_priv(indio_dev);
	int ret = 0;  // 默认返回整数型

    // spin_lock_irqsave(&dev->read_raw_lock, flags);
    // 如果没有初始化，则再次初始化
    if (dev->type == NO_FIND_DEVICE)
    {
        dl1x_init(dev);

        // 初始化失败，直接返回
        if (dev->type == NO_FIND_DEVICE)
            return -EINVAL;
    }

    switch (mask)
    {
        case IIO_CHAN_INFO_RAW:
            *val = dev->dl1x_get_distance(dev);
            ret = IIO_VAL_INT;  // 返回整形
            break;
        default:
            ret = -EINVAL;
            break;
	}
    // spin_unlock_irqrestore(&dev->read_raw_lock, flags);

    return ret;
}	



// 通过/sys/bus/iio/devices/iio:device1/events/in_distance_change_en 接口读取
static int dl1x_read_event_config(struct iio_dev *indio_dev, const struct iio_chan_spec *chan,
    enum iio_event_type type, enum iio_event_direction dir)
{
    struct dl1x_dev *dev = iio_priv(indio_dev);
    // 返回设备类型
    return dev->type;
}

// 通过/sys/bus/iio/devices/iio:device1/events/in_distance_change_en 接口写入
// 例如 echo 1 >> /sys/bus/iio/devices/iio:device1/events/in_distance_change_en 重新初始化
static int dl1x_write_event_config(struct iio_dev *indio_dev, const struct iio_chan_spec *chan,
    enum iio_event_type type, enum iio_event_direction dir, int state)
{
    struct dl1x_dev *dev = iio_priv(indio_dev);
    // 重新初始化
    if(state == 1)
    {
        // 初始化DL1X
        dl1x_init(dev);
        // 打印设备信息
        dl1x_type_print(dev);
    }
    return dev->type;
}

// iio_info结构体变量
static const struct iio_info dl1x_iio_info = 
{
	.read_raw		        = dl1x_read_raw,

    .write_event_config     = dl1x_write_event_config,
    .read_event_config      = dl1x_read_event_config,
};

// 设置 iio_dev 的其他成员变量
static void setup_iio_dev(struct iio_dev *indio_dev, struct dl1x_dev *dev)
{

    // 绑定父设备（sysfs节点创建依赖）
    indio_dev->dev.parent = &dev->client->dev;

    // 设置IIO设备名称
    indio_dev->name = "zf_device_dl1x";

    // 绑定IIO操作集（必须实现read_raw等核心函数）
    indio_dev->info = &dl1x_iio_info;

    // 绑定通道列表（至少1个有效通道）
    indio_dev->channels = dl1x_channels;
    indio_dev->num_channels = ARRAY_SIZE(dl1x_channels);

    // 设置工作模式（直接模式，无缓冲）
    indio_dev->modes = INDIO_DIRECT_MODE;

    // // 可选：绑定私有数据（便于IIO操作函数访问dev）
    // iio_device_set_drvdata(indio_dev, dev);

}



// ==================== 设备初始化（probe函数）====================
// ==================== 设备初始化（probe函数）====================
/**
 * dl1x_probe - I2C设备匹配成功后的初始化入口函数
 * @client: 匹配成功的I2C客户端设备结构体
 * @id:     I2C设备ID匹配表项（传统匹配方式）
 * 
 * 返回值：0表示成功，负值表示失败
 * 核心功能：
 *  1. 检查I2C适配器功能
 *  2. 分配IIO设备内存并初始化私有数据
 *  3. 配置IIO设备参数并注册
 *  4. 初始化同步锁等核心资源
 */
static int dl1x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    struct dl1x_dev *dev;          // 私有设备数据结构体指针
    struct iio_dev *indio_dev;     // IIO设备核心结构体指针

    dev_info(&client->dev, "dl1x i2c driver probe start\n");

    // 检查I2C适配器是否支持原生I2C读写功能（非SMBus）
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "I2C adapter not support native I2C function\n");
        return -EOPNOTSUPP; // 返回"操作不支持"错误码
    }

    // 申请IIO设备内存（devm_前缀：内核自动管理内存释放）
    // 参数说明：&client->dev - 父设备上下文；sizeof(*dev) - 私有数据区大小
    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*dev));
    if (!indio_dev) {
        dev_err(&client->dev, "Failed to allocate iio_dev memory\n");
        return -ENOMEM; // 返回"内存不足"错误码
    }

    // 从IIO设备中提取私有数据区（dl1x_dev）
    dev = iio_priv(indio_dev);
    dev->client = client; // 绑定I2C客户端到私有数据
    // 将私有数据dev绑定到I2C客户端（后续可通过i2c_get_clientdata获取）
    // 【注意】若需获取indio_dev，需后续从dev反向推导（dev = iio_priv(indio_dev)）
    i2c_set_clientdata(client, dev);

    // 初始化I2C操作互斥锁，保护并发访问
    mutex_init(&dev->i2c_lock);  

    setup_iio_dev(indio_dev, dev);

    // 申请xs引脚
    ret = dl1x_xs_gpio_init(dev);
    if (ret < 0) 
    {
        return ret;
    }

    // 注册IIO设备到内核IIO子系统（注册后对外提供sysfs等访问接口）
    ret = iio_device_register(indio_dev);
    if (ret < 0) 
    {
        dev_err(&client->dev, "Failed to register iio device, ret=%d\n", ret);
        return ret;
    }

    // 初始化DL1X
    dl1x_init(dev);
    // 打印设备信息
    dl1x_type_print(dev);


    dev_info(&client->dev, "dl1x i2c driver probe success\n");
    return 0;
}
// ==================== 设备移除（remove函数）====================
static int dl1x_remove(struct i2c_client *client)
{
    // 取出私有数据（强制转换为结构体类型）
    struct dl1x_dev *dev = i2c_get_clientdata(client);

    dev_info(&client->dev, "dl1x device removed\n");

    // 释放 GPIO 引脚
    if(dev->xs_pin != 0)
    {
        gpio_free(dev->xs_pin);
        dev_info(&client->dev, "GPIO %d freed\n", dev->xs_pin);
    }
    return 0;
}

// ==================== 设备树匹配表 ====================
static const struct of_device_id dl1x_of_match[] = {
    { .compatible = "seekfree,dl1x" },  // 与设备树compatible一致
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dl1x_of_match);

// ==================== I2C设备ID表（兼容非设备树）====================
static const struct i2c_device_id dl1x_id_table[] = {
    { "seekfree,dl1x", 0 },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, dl1x_id_table);

// ==================== I2C驱动结构体 ====================
static struct i2c_driver dl1x_driver = {
    .driver = {
        .name = "dl1x-i2c",
        .of_match_table = dl1x_of_match,
        .owner = THIS_MODULE,
    },
    .probe = dl1x_probe,
    .remove = dl1x_remove,
    .id_table = dl1x_id_table,
};

// ==================== 模块加载/卸载 ====================
module_i2c_driver(dl1x_driver);

// 模块信息
MODULE_AUTHOR("SEEKFREE_BigW");
MODULE_DESCRIPTION("Linux 4.19 Native I2C Driver for dl1x (No SMBus)");
MODULE_LICENSE("GPL");
