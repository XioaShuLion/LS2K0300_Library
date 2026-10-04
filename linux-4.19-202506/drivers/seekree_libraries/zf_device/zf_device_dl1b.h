#ifndef _zf_device_dl1b_h_
#define _zf_device_dl1b_h_

#include "../zf_common/zf_common_typedef.h"


#define DL1B_TIMEOUT_COUNT                                      ( 2 )           // DL1B 超时计数

//================================================定义 DL1B 内部地址================================================

#define DL1B_DEV_ADDR                                           ( 0x52 >> 1 )   // 0b0101001

#define DL1B_I2C_SLAVE__DEVICE_ADDRESS                          ( 0x0001 )
#define DL1B_GPIO__TIO_HV_STATUS                                ( 0x0031 )
#define DL1B_SYSTEM__INTERRUPT_CLEAR                            ( 0x0086 )
#define DL1B_RESULT__RANGE_STATUS                               ( 0x0089 )
#define DL1B_RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0     ( 0x0096 )
#define DL1B_FIRMWARE__SYSTEM_STATUS                            ( 0x00E5 )

//================================================定义 DL1B 内部地址================================================

extern uint8 dl1b_finsh_flag;
extern uint16 dl1b_distance_mm;

uint16 dl1b_get_distance (struct dl1x_dev *dev);
uint8  dl1b_init         (struct dl1x_dev *dev);

#endif

