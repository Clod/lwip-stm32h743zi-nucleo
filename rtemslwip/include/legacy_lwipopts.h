/*
 * Copyright (C) 2023 On-Line Applications Research Corporation (OAR)
 * Written by Kinsey Moore <kinsey.moore@oarcorp.com>
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

/*
 * The options here are pruned down, but roughly equivalent to the original set
 * of options configured for the BeagleBone and TMS570 BSPs originally pulled
 * from the uLan repository.
 */

#ifndef __LEGACY_LWIPOPTS_H__
#define __LEGACY_LWIPOPTS_H__

#define CPSW_DUAL_MAC_MODE

#define NUM_DHCP_TRIES 5

#define LWIP_ETHERNET 1

#define LWIP_TCPIP_CORE_LOCKING 1

/*****************************************************************************
**                          Memory Options
*****************************************************************************/
#define MEM_ALIGNMENT __SIZEOF_POINTER__
#define MEM_SIZE (1024 * 1024) /* 4K */
#define MEMP_NUM_TCP_SEG 32
#define MEMP_MEM_MALLOC 1
#define MEMP_MEM_INIT 1

#endif /* __LEGACY_LWIPOPTS_H__ */
