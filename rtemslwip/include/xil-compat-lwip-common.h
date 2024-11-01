/*
 * Copyright (C) 2024 On-Line Applications Research Corporation (OAR)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _RTEMSLWIP_XIL_COMPAT_LWIP_COMMON_H
#define _RTEMSLWIP_XIL_COMPAT_LWIP_COMMON_H

#include <bsp/xil-compat.h>
typedef int32_t XStatus;
typedef long LONG;
typedef void (*Xil_InterruptHandler)(void*);
typedef void (*XInterruptHandler)(void *);
#define print(msg) xil_printf("%s\r\n", msg)

#define ULONG64_HI_MASK 0xFFFFFFFF00000000LLU
#define ULONG64_LO_MASK 0x00000000FFFFFFFFLLU

#define XIL_COMPONENT_IS_STARTED 0x22222222U

#define XST_DEVICE_IS_STOPPED 6L
#define XST_INVALID_PARAM 15L
#define XST_IS_STARTED 23L
#define XST_DMA_SG_IS_STARTED 514L
#define XST_DMA_SG_IS_STOPPED 515L
#define XST_DMA_SG_NO_LIST 523L
#define XST_DMA_SG_LIST_ERROR 526L
#define XST_EMAC_MII_BUSY 1004L

#endif
