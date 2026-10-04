/*
* SE ENCRYPT CHIP Driver
*
* Written By: dcm, WESTONE Corporation
*
* Copyright (C) 2021 08 WESTONE Corp
*
* All rights reserved.
*/
#ifndef _WST_SE_KTRANS_H
#define _WST_SE_KTRANS_H

#include "wst_se_common_type.h"
#include "wst_se_define.h"

typedef struct tagSWCOMMUDATA{
	unsigned short usFlags;
	unsigned short usInputLen;
	unsigned short usOutputLen;
	unsigned short usReserve;
	unsigned char *pucInbuf;
	unsigned char *pucOutbuf;
} SWCommuData;


#endif
