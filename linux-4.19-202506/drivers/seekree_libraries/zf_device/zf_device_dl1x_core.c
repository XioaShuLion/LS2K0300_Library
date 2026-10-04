

#include "zf_device_dl1x_core.h"
#include "zf_device_dl1a.h"
#include "zf_device_dl1b.h"

// #define dl1a_write_array(data, len)          (iic_write_8bit_array    (DL1A_IIC, DL1A_DEV_ADDR, (data), (len)))
// #define dl1a_write_register(reg, data)       (iic_write_8bit_register (DL1A_IIC, DL1A_DEV_ADDR, (reg), (data)))
// #define dl1a_read_register(reg)              (iic_read_8bit_register  (DL1A_IIC, DL1A_DEV_ADDR, (reg)))
// #define dl1a_read_registers(reg, data, len)  (iic_read_8bit_registers (DL1A_IIC, DL1A_DEV_ADDR, (reg), (data), (len)))

int iic_write_8bit_register(struct dl1x_dev *dev, uint8 iic_addr, uint8 reg, uint8 val)
{
    mutex_lock(&dev->i2c_lock);
    // 构建I2C写消息：[寄存器地址, 写入值]
    uint8 buf[2] = {reg, val};
    int ret = 0;
    struct i2c_msg msg = 
    {
        .addr = iic_addr,        // I2C从地址
        .flags = 0,              // 0=写操作，I2C_M_RD=读操作
        .len = sizeof(buf),      // 数据长度
        .buf = buf,              // 数据缓冲区
    };

    ret = i2c_transfer(dev->client->adapter, &msg, 1);
    mutex_unlock(&dev->i2c_lock);
    return ret;
}

int iic_write_8bit_array(struct dl1x_dev *dev, uint8 iic_addr, uint8 *buffer, uint32 len)
{
    mutex_lock(&dev->i2c_lock);
    int ret = 0;
    // 构建I2C写消息：[寄存器地址, 写入值]
    struct i2c_msg msg = 
    {
        .addr  = iic_addr,      // I2C从地址
        .flags = 0,             // 0=写操作，I2C_M_RD=读操作
        .len   = len,           // 数据长度
        .buf   = buffer,        // 数据缓冲区
    };

    ret = i2c_transfer(dev->client->adapter, &msg, 1);
    mutex_unlock(&dev->i2c_lock);
    return ret;
}

int iic_read_8bit_register(struct dl1x_dev *dev, uint8 iic_addr, uint8 reg, uint8 *val)
{
    mutex_lock(&dev->i2c_lock);
    int ret = 0;
    // 步骤1：发送要读取的寄存器地址（写操作）
    struct i2c_msg msg_w = 
    {
        .addr = iic_addr,
        .flags = 0,
        .len = 1,
        .buf = &reg,
    };

    // 步骤2：读取该寄存器的值（读操作）
    struct i2c_msg msg_r = 
    {
        .addr = iic_addr,
        .flags = I2C_M_RD,  // 标记为读操作
        .len = 1,
        .buf = val,
    };
    struct i2c_msg msgs[] = {msg_w, msg_r};

    // 批量执行2条消息（先写寄存器地址，再读数据）
    ret = i2c_transfer(dev->client->adapter, msgs, 2);
    if (ret != 2) 
    {
        dev_err(&dev->client->dev, "iic_read_8bit_register = %d\n", ret);
    }
    mutex_unlock(&dev->i2c_lock);

    return ret;
}

int iic_read_8bit_registers(struct dl1x_dev *dev, uint8 iic_addr, uint8 reg, uint8 *buf, int len)
{

    if (len <= 0 || !buf)
        return -EINVAL;

    mutex_lock(&dev->i2c_lock);
    // 步骤1：发送要读取的寄存器地址（写操作）
    struct i2c_msg msg_w = 
    {
        .addr = iic_addr,
        .flags = 0,
        .len = 1,
        .buf = &reg,
    };
    
    // 步骤2：批量读取len字节数据
    struct i2c_msg msg_r = {
        .addr = iic_addr,
        .flags = I2C_M_RD,
        .len = len,
        .buf = buf,
    };

    struct i2c_msg msgs[] = {msg_w, msg_r};
    int ret = i2c_transfer(dev->client->adapter, msgs, 2);
    if (ret != 2) 
    {
        dev_err(&dev->client->dev, "iic_read_8bit_registers = %d\n", ret);
    }

    mutex_unlock(&dev->i2c_lock);
    return ret;
}


int iic_transfer_8bit_array(struct dl1x_dev *dev, uint8 iic_addr, uint8 *w_buf, uint32 w_len, uint8 *r_buf, uint32 r_len)
{

    if (w_len <= 0 || !w_buf)
        return -EINVAL;

    mutex_lock(&dev->i2c_lock);

    // 步骤1：发送要读取的寄存器地址（写操作）
    struct i2c_msg msg_w = 
    {
        .addr = iic_addr,
        .flags = 0,
        .len = w_len,
        .buf = w_buf,
    };
    
    // 步骤2：批量读取len字节数据
    struct i2c_msg msg_r = {
        .addr = iic_addr,
        .flags = I2C_M_RD,
        .len = r_len,
        .buf = r_buf,
    };

    struct i2c_msg msgs[] = {msg_w, msg_r};
    int ret = i2c_transfer(dev->client->adapter, msgs, 2);
    if (ret != 2) 
    {
        dev_err(&dev->client->dev, "iic_transfer_8bit_array = %d\n", ret);
    }

    mutex_unlock(&dev->i2c_lock);
    return ret;
}


// GPIO 初始化+电平控制核心函数
int dl1x_xs_gpio_init(struct dl1x_dev *dev)
{
    int ret;

    // 1、从设备树中获取 GPIO 信息
    dev->xs_pin = of_get_named_gpio(dev->client->dev.of_node, "xs-gpios", 0);
    if (!gpio_is_valid(dev->xs_pin)) 
    {
        dev_err(&dev->client->dev, "Failed to get GPIO from device tree\n");
        return -ENODEV;
    }

    // 申请 GPIO 引脚
    ret = gpio_request(dev->xs_pin, "xs-gpios");
    if (ret < 0) 
    {
        dev_err(&dev->client->dev, "Failed to request xs-gpios: %d\n", ret);
        return ret;
    }
    
    dev_info(&dev->client->dev, "dev->xs_pin = %d\n",  dev->xs_pin);

    // 2. 设置 GPIO 为输出模式
    gpio_direction_output(dev->xs_pin, 1);  // 先默认拉高（可选，保证初始态可控）
    // dev_info(&dev->client->dev, "gpio_direction_output\n");

    // msleep(100);

    // // 3. 写入低电平
    // gpio_set_value(dev->xs_pin, 0);
    // dev_info(&dev->client->dev, "gpio_set_value 0\n");


    // // 4. 延迟 50ms
    // msleep(50);

    // // 5. 拉高电平
    // gpio_set_value(dev->xs_pin, 1);
    // msleep(100);
    // dev_info(&dev->client->dev, "gpio_set_value 1\n");


    return 0;
}

void dl1x_type_print(struct dl1x_dev *dev)
{
    if(dev->type == ZF_DEVICE_DL1A)
    {
        dev_info(&dev->client->dev, "DL1A INIT OK\n");
    }
    else if(dev->type == ZF_DEVICE_DL1B)
    {
        dev_info(&dev->client->dev, "DL1B INIT OK\n");
    }
    else
    {
        dev_info(&dev->client->dev, "NO FIND DL1X DEV\n");
    }
}


int dl1x_init(struct dl1x_dev *dev)
{
    uint8 ret = 0;

    // if(dev->type == NO_FIND_DEVICE || dev->dl1x_get_distance == NULL)
    // {
        do
        {
            if (dl1a_init(dev) == 0)
            {
                dev->type = ZF_DEVICE_DL1A;
                dev->dl1x_get_distance = dl1a_get_distance;
                break;
            }
            if (dl1b_init(dev) == 0)
            {
                dev->type = ZF_DEVICE_DL1B;
                dev->dl1x_get_distance = dl1b_get_distance;
                break;
            }
            dev->type = NO_FIND_DEVICE;
            dev->dl1x_get_distance = NULL;
            ret = -1;
        } while (0);
    // }
    
    return ret;
}