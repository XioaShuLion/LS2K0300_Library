#include "zf_device_dl1x_core.h"
#include "zf_device_dl1b.h"
#include "zf_device_config.h"

static uint8 dl1b_init_flag = 0;
uint8 dl1b_finsh_flag = 0;
uint16 dl1b_distance_mm = 8192;

#define dl1b_write_8bit_array(dev, tdata, tlen)                     (iic_write_8bit_array(dev, DL1B_DEV_ADDR, (tdata), (tlen)))
#define dl1b_transfer_8bit_array(dev, tdata, tlen, rdata, rlen)     (iic_transfer_8bit_array(dev, DL1B_DEV_ADDR, (tdata), (tlen), (rdata), (rlen)))

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     返回以毫米为单位的范围读数
// 参数说明     void
// 返回参数     void
// 使用示例     dl1b_get_distance();
// 备注信息     在开始单次射程测量后也调用此函数
//-------------------------------------------------------------------------------------------------------------------
uint16 dl1b_get_distance (struct dl1x_dev *dev)
{
    if(dl1b_init_flag)
    {
        uint8 data_buffer[3];
        int16 dl1b_distance_temp = 0;

        data_buffer[0] = DL1B_GPIO__TIO_HV_STATUS >> 8;
        data_buffer[1] = DL1B_GPIO__TIO_HV_STATUS & 0xFF;
        dl1b_transfer_8bit_array(dev, data_buffer, 2, &data_buffer[2], 1);

        if(data_buffer[2])
        {

            data_buffer[0] = DL1B_SYSTEM__INTERRUPT_CLEAR >> 8;
            data_buffer[1] = DL1B_SYSTEM__INTERRUPT_CLEAR & 0xFF;
            data_buffer[2] = 0x01;
            dl1b_write_8bit_array(dev, data_buffer, 3);// clear Interrupt

            data_buffer[0] = DL1B_RESULT__RANGE_STATUS >> 8;
            data_buffer[1] = DL1B_RESULT__RANGE_STATUS & 0xFF;
            dl1b_transfer_8bit_array(dev, data_buffer, 2, &data_buffer[2], 1);
            
            if(0x89 == data_buffer[2])
            {
                data_buffer[0] = DL1B_RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0 >> 8;
                data_buffer[1] = DL1B_RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0 & 0xFF;
                dl1b_transfer_8bit_array(dev, data_buffer, 2, data_buffer, 2);
                dl1b_distance_temp = data_buffer[0];
                dl1b_distance_temp = (dl1b_distance_temp << 8) | data_buffer[1];
                
                if(dl1b_distance_temp > 4000 || dl1b_distance_temp < 0)
                {
                    dl1b_distance_mm = 8192;
                    dl1b_finsh_flag = 0;
                }
                else
                {
                    dl1b_distance_mm = dl1b_distance_temp;
                    dl1b_finsh_flag = 1;
                }
            }
            else
            {
                dl1b_distance_mm = 8192;
                dl1b_finsh_flag = 0;
            }
        }
        else
        {
            dl1b_distance_mm = 8192;
            dl1b_finsh_flag = 0;
        }
    }
    return dl1b_distance_mm;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     DL1B INT 中断响应处理函数
// 参数说明     void
// 返回参数     void
// 使用示例     dl1b_int_handler();
// 备注信息     本函数需要在 DL1B_INT_PIN 对应的外部中断处理函数中调用
//-------------------------------------------------------------------------------------------------------------------
#ifdef DL1B_INT_ENABLE
void dl1b_int_handler (struct dl1x_dev *dev)
{

    if(exti_flag_get(DL1B_INT_PIN))
    {
        exti_flag_clear(DL1B_INT_PIN);
        dl1b_get_distance();
    }

}
#endif

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化 DL1B
// 参数说明     void
// 返回参数     uint8           1-初始化失败 0-初始化成功
// 使用示例     dl1b_init();
// 备注信息
//-------------------------------------------------------------------------------------------------------------------
uint8 dl1b_init (struct dl1x_dev *dev)
{
    uint8   return_state    = 0;
    uint8   data_buffer[2 + sizeof(dl1b_config_file)]; 
    uint16  time_out_count  = 0;


    gpio_set_value(dev->xs_pin, 1);

    do
    {
        msleep(50);
        gpio_set_value(dev->xs_pin, 0);
        msleep(10);
        gpio_set_value(dev->xs_pin, 1);
        msleep(50);

        data_buffer[0] = DL1B_FIRMWARE__SYSTEM_STATUS >> 8;
        data_buffer[1] = DL1B_FIRMWARE__SYSTEM_STATUS & 0xFF;
        dl1b_transfer_8bit_array(dev, data_buffer, 2, &data_buffer[2], 1);
        return_state = (0x01 == (data_buffer[2] & 0x01)) ? (0) : (1);
        if(1 == return_state)
        {
            break;
        }

        data_buffer[0] = DL1B_I2C_SLAVE__DEVICE_ADDRESS >> 8;
        data_buffer[1] = DL1B_I2C_SLAVE__DEVICE_ADDRESS & 0xFF;
        memcpy(&data_buffer[2], (uint8 *)dl1b_config_file, sizeof(dl1b_config_file));
        dl1b_write_8bit_array(dev, data_buffer, 2 + sizeof(dl1b_config_file));

        while(1)
        {
            data_buffer[0] = DL1B_GPIO__TIO_HV_STATUS >> 8;
            data_buffer[1] = DL1B_GPIO__TIO_HV_STATUS & 0xFF;
            dl1b_transfer_8bit_array(dev, data_buffer, 2, &data_buffer[2], 1);
            if(0x00 == (data_buffer[2] & 0x01))
            {
                time_out_count = 0;
                break;
            }
            if(DL1B_TIMEOUT_COUNT < time_out_count ++)
            {
                return_state = 1;
                break;
            }
            msleep(1);
        }

        dl1b_init_flag = 1;
    }while(0);

#ifdef DL1B_INT_ENABLE
    // 暂不支持
#endif

    return return_state;
}
