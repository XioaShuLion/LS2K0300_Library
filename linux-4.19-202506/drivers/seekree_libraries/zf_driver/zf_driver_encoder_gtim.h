
#ifndef _zf_device_encoder_gtim_h_
#define _zf_device_encoder_gtim_h_


#include "../zf_common/zf_common_typedef.h"
#include "zf_driver_gtim.h"


int16  encoder_gtim_get_count   (void *base_addr);
void   encoder_gtim_clear_count (void *base_addr);

int    encoder_gtim_quad_init   (void *base_addr);
int    encoder_gtim_dir_init    (void *base_addr);

#endif