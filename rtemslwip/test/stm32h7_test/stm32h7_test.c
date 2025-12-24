#include <rtems.h>
#include <rtems/console.h>
#include <rtems/shell.h>
#include <netstart.h>
#include <lwip/tcpip.h>
#include <lwip/netif.h>
#include <lwip/apps/lwiperf.h>
#include "stm32h7_eth.h"
#include <stdio.h>
#include "tmacros.h"

const char rtems_test_name[] = "STM32H7 LWIP TEST";

static struct netif netif;

static void lwiperf_report(void *arg, enum lwiperf_report_type report_type,
  const ip_addr_t *local_addr, u16_t local_port, const ip_addr_t *remote_addr, u16_t remote_port,
  u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
  printf("IPERF report: %u kbps\n", (unsigned int)bandwidth_kbitpsec);
}

void Error_Handler(void)
{
  printf("Error_Handler called!\n");
  while(1);
}

static rtems_task Init(rtems_task_argument argument)
{
  ip4_addr_t ipaddr, netmask, gw;

  printf("\n\n*** STM32H7 LWIP TEST ***\n");

  tcpip_init(NULL, NULL);

  IP4_ADDR(&ipaddr, 192, 168, 1, 10);
  IP4_ADDR(&netmask, 255, 255, 255, 0);
  IP4_ADDR(&gw, 192, 168, 1, 1);

  netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input);
  netif_set_default(&netif);

  printf("Waiting for Ethernet link to come up...\n");
  while (!netif_is_link_up(&netif)) {
    rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(500));
  }

  printf("Interface is UP. IP: 192.168.1.10\n");
  printf("Starting lwiperf server on port 5001...\n");

  LOCK_TCPIP_CORE();
  lwiperf_start_tcp_server_default(lwiperf_report, NULL);
  UNLOCK_TCPIP_CORE();

  extern __IO uint32_t EthIrqCount;
  extern __IO uint32_t RxIrqCount;
  while(1) {
    printf("Heartbeat: Total IRQs: %lu, RX Cplt: %lu\n", 
           (unsigned long)EthIrqCount, (unsigned long)RxIrqCount);
    rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(2000));
  }

  rtems_task_exit();
}

#define CONFIGURE_INIT
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

/* LwIP needs a few tasks, semaphores and message queues */
#define CONFIGURE_MAXIMUM_TASKS 12
#define CONFIGURE_MAXIMUM_SEMAPHORES 32
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES 16
#define CONFIGURE_MAXIMUM_MRESOURCES 16

/* Give plenty of memory for message buffers */
#define CONFIGURE_MESSAGE_BUFFER_MEMORY \
  (16 * 32 * (sizeof(void *) + 16))

#define CONFIGURE_UNIFIED_WORK_AREAS

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_STACK_SIZE (32 * 1024)
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#include <rtems/confdefs.h>
