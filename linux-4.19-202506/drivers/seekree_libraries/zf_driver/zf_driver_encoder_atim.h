#ifndef _zf_device_encoder_atim_h_
#define _zf_device_encoder_atim_h_


#include "../zf_common/zf_common_typedef.h"
#include "zf_driver_atim.h"



int16   encoder_atim_get_count  (void *base_addr);
void    encoder_atim_clear_count(void *base_addr);

int     encoder_atim_dir_init   (void *base_addr);
int     encoder_atim_quad_init  (void *base_addr);

#endif