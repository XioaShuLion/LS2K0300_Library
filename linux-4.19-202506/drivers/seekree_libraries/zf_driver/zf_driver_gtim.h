
#ifndef _zf_device_gtim_h_
#define _zf_device_gtim_h_


#include "../zf_common/zf_common_typedef.h"

typedef struct
{
  vuint32 CR1;        // 0x00
  vuint32 CR2;        // 0x04
  vuint32 SMCR;       // 0x08
  vuint32 DIER;       // 0x0C
  vuint32 SR;         // 0x10
  vuint32 EGR;        // 0x14
  vuint32 CCMR1;      // 0x18
  vuint32 CCMR2;      // 0x1C
  vuint32 CCER;       // 0x20
  vuint32 CNT;        // 0x24
  vuint32 PSC;        // 0x28
  vuint32 ARR;        // 0x2C
  vuint32 RESERVED0;  // 0x30     // GTIM没有这个寄存器
  vuint32 CCR1;       // 0x34
  vuint32 CCR2;       // 0x38
  vuint32 CCR3;       // 0x3C
  vuint32 CCR4;       // 0x40
  vuint32 BDTR;       // 0x44
  vuint32 RESERVED1;  // 0x48
  vuint32 RESERVED2;  // 0x4C
  vuint32 INSTA;      // 0x50
} gtim_typedef;

#endif