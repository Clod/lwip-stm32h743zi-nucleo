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

#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#include <lwipconfig.h>

/* Critical items that all BSPs must use for full functionality */
#define SYS_LIGHTWEIGHT_PROT 1
#define NO_SYS 0
#define LWIP_SOCKET 1
#define SO_REUSE 1
#define LWIP_COMPAT_SOCKETS 1
#define LWIP_NETCONN 1
#define LWIP_NETIF_LOOPBACK 1 /* Required for socketpair implementation */
#define LWIP_NETIF_API 1
#define LWIP_TIMEVAL_PRIVATE 0
#define LWIP_CALLBACK_API 1
#define NO_SYS_NO_TIMERS 1
#define LWIP_COMPAT_MUTEX 0
#define LWIP_ALLOW_MEM_FREE_FROM_OTHER_CONTEXT 1

#include <lwipbspopts.h>

/* Sane defaults that the configuration or BSP can override */

#ifndef ARP_QUEUEING
#define ARP_QUEUEING 1
#endif

#ifndef ARP_TABLE_SIZE
#define ARP_TABLE_SIZE 10
#endif

#ifndef CHECKSUM_CHECK_IP
#define CHECKSUM_CHECK_IP 1
#endif

#ifndef CHECKSUM_CHECK_TCP
#define CHECKSUM_CHECK_TCP 1
#endif

#ifndef CHECKSUM_CHECK_UDP
#define CHECKSUM_CHECK_UDP 1
#endif

#ifndef CHECKSUM_GEN_IP
#define CHECKSUM_GEN_IP 1
#endif

#ifndef CHECKSUM_GEN_TCP
#define CHECKSUM_GEN_TCP 1
#endif

#ifndef CHECKSUM_GEN_UDP
#define CHECKSUM_GEN_UDP 1
#endif

#ifndef CONFIG_LINKSPEED_AUTODETECT
#define CONFIG_LINKSPEED_AUTODETECT 1
#endif

#ifndef DEFAULT_ACCEPTMBOX_SIZE
#define DEFAULT_ACCEPTMBOX_SIZE 5
#endif

#ifndef DEFAULT_TCP_RECVMBOX_SIZE
#define DEFAULT_TCP_RECVMBOX_SIZE 20
#endif

#ifndef DEFAULT_UDP_RECVMBOX_SIZE
#define DEFAULT_UDP_RECVMBOX_SIZE 20
#endif

#ifndef LWIP_DHCP_DOES_ACD_CHECK
#define LWIP_DHCP_DOES_ACD_CHECK 1
#endif

#ifndef ICMP_TTL
#define ICMP_TTL 255
#endif

#ifndef IP_DEFAULT_TTL
#define IP_DEFAULT_TTL 255
#endif

#ifndef IP_FORWARD
#define IP_FORWARD 0
#endif

#ifndef IP_FRAG
#define IP_FRAG 1
#endif

#ifndef IP_FRAG_MAX_MTU
#define IP_FRAG_MAX_MTU 1500
#endif

#ifndef IP_OPTIONS
#define IP_OPTIONS 1
#endif

#ifndef IP_OPTIONS_ALLOWED
#define IP_OPTIONS_ALLOWED 0
#endif

#ifndef IP_REASS_BUFSIZE
#define IP_REASS_BUFSIZE 5760
#endif

#ifndef IP_REASSEMBLY
#define IP_REASSEMBLY 1
#endif

#ifndef LWIP_ARP
#define LWIP_ARP 1
#endif

#ifndef LWIP_AUTOIP
#define LWIP_AUTOIP 1
#endif

#ifndef LWIP_CHKSUM_ALGORITHM
#define LWIP_CHKSUM_ALGORITHM 3
#endif

#ifndef LWIP_DHCP
#define LWIP_DHCP 1
#endif

#ifndef LWIP_DHCP_AUTOIP_COOP
#define LWIP_DHCP_AUTOIP_COOP ((LWIP_DHCP) && (LWIP_AUTOIP))
#endif

#ifndef LWIP_DNS
#define LWIP_DNS 1
#endif

#ifndef LWIP_IPV4
#define LWIP_IPV4 1
#endif

#ifndef LWIP_IPV6
#define LWIP_IPV6 1
#endif

#ifndef LWIP_TCP
#define LWIP_TCP 1
#endif

#ifndef LWIP_UDP
#define LWIP_UDP 1
#endif

#ifndef MEMP_NUM_FRAG_PBUF
#define MEMP_NUM_FRAG_PBUF 256
#endif

#ifndef MEMP_NUM_NETCONN
#define MEMP_NUM_NETCONN 16
#endif

#ifndef MEMP_NUM_PBUF
#define MEMP_NUM_PBUF 96
#endif

#ifndef MEMP_NUM_TCP_PCB
#define MEMP_NUM_TCP_PCB 32
#endif

#ifndef MEMP_NUM_TCP_PCB_LISTEN
#define MEMP_NUM_TCP_PCB_LISTEN 8
#endif

#ifndef MEMP_NUM_TCP_SEG
#define MEMP_NUM_TCP_SEG 256
#endif

#ifndef MEMP_NUM_UDP_PCB
#define MEMP_NUM_UDP_PCB 16
#endif

#ifndef MEMP_SEPARATE_POOLS
#define MEMP_SEPARATE_POOLS 1
#endif

#ifndef MEM_SIZE
#define MEM_SIZE 2 * 1024 * 1024
#endif

#ifndef PBUF_LINK_HLEN
#define PBUF_LINK_HLEN 16
#endif

#ifndef PBUF_POOL_BUFSIZE
#define PBUF_POOL_BUFSIZE 1600
#endif

#ifndef PBUF_POOL_SIZE
#define PBUF_POOL_SIZE 512
#endif

#ifndef TCP_FAST_INTERVAL
#define TCP_FAST_INTERVAL 250
#endif

#ifndef TCPIP_MBOX_SIZE
#define TCPIP_MBOX_SIZE 20
#endif

#ifndef TCP_MAXRTX
#define TCP_MAXRTX 12
#endif

#ifndef TCP_MSS
#define TCP_MSS 1576
#endif

#ifndef TCP_OVERSIZE
#define TCP_OVERSIZE TCP_MSS
#endif

#ifndef TCP_QUEUE_OOSEQ
#define TCP_QUEUE_OOSEQ 1
#endif

#ifndef TCP_SLOW_INTERVAL
#define TCP_SLOW_INTERVAL 500
#endif

#ifndef TCP_SND_BUF
#define TCP_SND_BUF (8 * TCP_MSS)
#endif

#ifndef TCP_SND_QUEUELEN
#define TCP_SND_QUEUELEN 16 * TCP_SND_BUF / TCP_MSS
#endif

#ifndef TCP_SYNMAXRTX
#define TCP_SYNMAXRTX 4
#endif

#ifndef TCP_TMR_INTERVAL
#define TCP_TMR_INTERVAL 250
#endif

#ifndef TCP_TTL
#define TCP_TTL 255
#endif

#ifndef TCP_WND
#define TCP_WND (8 * TCP_MSS)
#endif

#ifndef UDP_TTL
#define UDP_TTL 255
#endif

#endif /* __LWIPOPTS_H__ */
