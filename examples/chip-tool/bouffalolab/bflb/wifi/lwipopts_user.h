/*
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#if defined(CONFIG_FHOST)
#include <lwipopts.h>
#else

#include <stdlib.h>
#include <string.h>

/* WL80211 lwIP configuration, based on the SDK's macsw_bare example. */
#define LWIP_NETIF_API 1
#define LWIP_DEBUG 0
#define LWIP_STATS_DISPLAY 0
#define SOCKETS_DEBUG LWIP_DBG_OFF
#define DHCP_DEBUG LWIP_DBG_OFF
#define IP6_DEBUG LWIP_DBG_OFF
#define DHCP6_DEBUG LWIP_DBG_OFF
#define IP_DEBUG LWIP_DBG_OFF
#define NETIF_DEBUG LWIP_DBG_OFF
#define ICMP_DEBUG LWIP_DBG_OFF
#define ETHARP_DEBUG LWIP_DBG_OFF
#define TCP_DEBUG LWIP_DBG_OFF
#define TCP_INPUT_DEBUG LWIP_DBG_OFF
#define LWIP_MULTICAST_PING 1
#define LWIP_BROADCAST_PING 1
#define TCPIP_MBOX_SIZE 64
#define TCPIP_THREAD_STACKSIZE 1024
#define TCPIP_THREAD_PRIO 28
#define DEFAULT_THREAD_STACKSIZE 1024
#define DEFAULT_THREAD_PRIO 1
#define DEFAULT_RAW_RECVMBOX_SIZE 32
#define DEFAULT_UDP_RECVMBOX_SIZE 64
#define DEFAULT_TCP_RECVMBOX_SIZE 64
#define DEFAULT_ACCEPTMBOX_SIZE 32
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_HAVE_LOOPIF 1
#define LWIP_LOOPBACK_MAX_PBUFS 0
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
#define PBUF_LINK_ENCAPSULATION_HLEN 360
#define IP_REASS_MAX_PBUFS 4
#define MEMP_MEM_MALLOC 0
#define MEM_LIBC_MALLOC 1

#if MEM_LIBC_MALLOC
#ifndef mem_clib_free
void wl80211_platform_free_wram(void * ptr);
#define mem_clib_free(ptr) wl80211_platform_free_wram(ptr)
#endif
#ifndef mem_clib_malloc
void * wl80211_platform_malloc_wram(size_t size);
#define mem_clib_malloc(size) wl80211_platform_malloc_wram(size)
#endif
#ifndef mem_clib_calloc
void * wl80211_platform_malloc_wram(size_t size);
static inline void * _lwip_calloc(size_t count, size_t size)
{
    void * ptr = wl80211_platform_malloc_wram(count * size);
    if (ptr != NULL)
    {
        memset(ptr, 0, count * size);
    }
    return ptr;
}
#define mem_clib_calloc(count, size) _lwip_calloc(count, size)
#endif
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) u8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)]
#endif
#define MEMP_NUM_NETBUF 32
#define MEMP_NUM_NETCONN 8
#define MEMP_NUM_UDP_PCB 8
#define MEMP_NUM_TCP_PCB 8
#define MEMP_NUM_TCPIP_MSG_API 16
#define MEMP_NUM_TCPIP_MSG_INPKT 16
#define MEMP_NUM_PBUF 48
#define MEMP_NUM_REASSDATA LWIP_MIN(IP_REASS_MAX_PBUFS, 5)
#define PBUF_POOL_SIZE 0
#define TCP_MSS (1500 - 40)
#define TCP_WND (((32 * 1024 + TCP_MSS) / TCP_MSS) * TCP_MSS)
#define TCP_SND_BUF (((16 * 1024 + TCP_MSS) / TCP_MSS) * TCP_MSS)
#define TCP_QUEUE_OOSEQ 1
#define MEMP_NUM_TCP_SEG ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define LWIP_WND_SCALE 1
#define TCP_RCV_SCALE 2
#define TCP_SNDLOWAT LWIP_MIN(LWIP_MAX((TCP_SND_BUF / 4), (2 * TCP_MSS) + 1), TCP_SND_BUF - 1)
#define MEM_ALIGNMENT 4
#define LWIP_RAW 1
#define LWIP_MULTICAST_TX_OPTIONS 1
#define LWIP_TIMEVAL_PRIVATE 0
#define LWIP_ERRNO_STDINCLUDE 1
#define LWIP_SOCKET_SET_ERRNO 1
#define LWIP_DHCP 1
#define LWIP_DNS 1
#define LWIP_DNS_SERVER 0
#define LWIP_IGMP 0
#define LWIP_SO_RCVTIMEO 1
#define LWIP_SO_SNDTIMEO 1
#define SO_REUSE 1
#define LWIP_TCP_KEEPALIVE 1
#define ETHARP_SUPPORT_STATIC_ENTRIES 1
#define TCP_OVERSIZE 128
#define LWIP_SUPPORT_CUSTOM_PBUF 1
#define LWIP_TCPIP_FORCE_TX_COPY 1

#undef LWIP_RAND
#define LWIP_RAND() ((u32_t) random())

#define LWIP_IPV6 1
#define LWIP_IPV6_DHCP6 1
#define LWIP_IPV6_MLD 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1

#endif // CONFIG_FHOST
