#include "udp_server.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <rtems.h>

#define UDP_SERVER_PORT 5005
#define RX_BUFFER_SIZE  256

static void udp_server_thread(void *arg)
{
  int sock;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_addr_len;
  uint32_t rx_buffer_storage[RX_BUFFER_SIZE / 4];
  char *rx_buffer = (char *)rx_buffer_storage;
  int recv_len;

  printf("UDP Server: Thread started, waiting for system to settle...\n");
  fflush(stdout);
  rtems_task_wake_after(rtems_clock_get_ticks_per_second() * 2);

  /* Create UDP socket - use lwip_ prefix to be explicit */
  sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    printf("UDP Server: Failed to create socket (lwip_errno=%d)\n", errno);
    fflush(stdout);
    while(1) { rtems_task_wake_after(100); }
  }

  /* Bind to port */
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(UDP_SERVER_PORT);

  if (lwip_bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    printf("UDP Server: Failed to bind socket (lwip_errno=%d)\n", errno);
    fflush(stdout);
    lwip_close(sock);
    while(1) { rtems_task_wake_after(100); }
  }

  printf("UDP Server: Listening on port %d...\n", UDP_SERVER_PORT);
  fflush(stdout);

  while (1) {
    client_addr_len = sizeof(client_addr);
    
    // Explicitly zero buffer
    memset(rx_buffer, 0, RX_BUFFER_SIZE);
    
    recv_len = lwip_recvfrom(sock, rx_buffer, RX_BUFFER_SIZE - 1, 0,
                             (struct sockaddr *)&client_addr, &client_addr_len);

    if (recv_len > 0) {
      rx_buffer[recv_len] = '\0';
      printf("UDP Server: RECEIVED %d bytes: '%s'\n", recv_len, rx_buffer);

      /* Invert string in place */
      int start = 0;
      int end = recv_len - 1;
      while (start < end) {
        char temp = rx_buffer[start];
        rx_buffer[start] = rx_buffer[end];
        rx_buffer[end] = temp;
        start++;
        end--;
      }

      /* Send back */
      printf("UDP Server: SENDING BACK: '%s'\n", rx_buffer);
      fflush(stdout);
      
      /* Send reversed string back with a carriage return as requested */
      int n = recv_len;
      if (n < RX_BUFFER_SIZE - 2) {
        rx_buffer[n++] = '\r';
        rx_buffer[n++] = '\n';
      }
      int sent = lwip_sendto(sock, rx_buffer, n, 0,
                             (struct sockaddr *)&client_addr, client_addr_len);
      if (sent < 0) {
        printf("UDP Server: lwip_sendto failed (errno=%d)\n", errno);
      } else {
        printf("UDP Server: SENT successfully (%d bytes)\n", sent);
      }
      fflush(stdout);
    } else {
        printf("UDP Server: lwip_recvfrom returned %d (errno=%d)\n", recv_len, errno);
        fflush(stdout);
    }
  }
}

void udp_server_init(void)
{
  /* Lower priority (15) than TCPIP thread (10) for stability */
  sys_thread_new("udp_server", udp_server_thread, NULL, 4096, 15);
}