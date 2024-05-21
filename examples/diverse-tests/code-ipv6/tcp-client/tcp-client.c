/*
 * Copyright (c) 2022, RISE Research Institutes of Sweden AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * \file
 *   A TCP client example.
 * \author
 *   Nicolas Tsiftes <nicolas.tsiftes@ri.se>
 */
#include <contiki.h>
#include "net/routing/routing.h"
#include "lib/random.h"
#include "lib/csprng.h"
#include <net/ipv6/uip-ds6.h>
#include <net/ipv6/tcp-socket.h>

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "TCPClient"
#define LOG_LEVEL LOG_LEVEL_INFO

PROCESS(test_tcp_client, "TCP client");
AUTOSTART_PROCESSES(&test_tcp_client);

uint16_t tcp_test_port;  /* 18962 */
#define SOCKET_BUF_SIZE 128

static struct tcp_socket client_sock;
static uint8_t in_buf[SOCKET_BUF_SIZE];
static uint8_t out_buf[SOCKET_BUF_SIZE];
static size_t bytes_received;
static size_t bytes_sent;
static size_t last_sent;
static size_t test_stream_length;
static unsigned stream_count;
static volatile bool can_send;
static volatile bool shutdown_test;
/*****************************************************************************/
static int
data_callback(struct tcp_socket *sock, void *ptr, const uint8_t *input, int len)
{
  LOG_INFO("RECV %d bytes\n", len);
  if(len >= 0) {
    bytes_received += len;
  }

  return 0;
}
/*****************************************************************************/
static void
event_callback(struct tcp_socket *sock, void *ptr, tcp_socket_event_t event)
{
  LOG_INFO("TCP socket event: ");
  switch(event) {
  case TCP_SOCKET_CONNECTED:
    LOG_INFO_("CONNECTED\n");
    can_send = true;
    break;
  case TCP_SOCKET_CLOSED:
    LOG_INFO_("CLOSED\n");
    shutdown_test = true;
    break;
  case TCP_SOCKET_TIMEDOUT:
    LOG_INFO_("ERROR TIMED OUT\n");
    shutdown_test = true;
    break;
  case TCP_SOCKET_ABORTED:
    LOG_INFO_("ERROR ABORTED\n");
    shutdown_test = true;
    break;
  case TCP_SOCKET_DATA_SENT:
    LOG_INFO_("DATA SENT\n");
    bytes_sent += last_sent;
    LOG_INFO("SENT %zu bytes (total %zu/%zu)\n", last_sent, bytes_sent,
             test_stream_length);
    last_sent = 0;
    can_send = true;
    break;
  default:
    LOG_INFO_("ERROR UNKNOWN (%d)\n", (int)event);
    shutdown_test = true;
    break;
  }

  process_poll(&test_tcp_client);
}
/*****************************************************************************/
PROCESS_THREAD(test_tcp_client, ev, data)
{
  static struct etimer timer;
  static uint8_t buf[SOCKET_BUF_SIZE];
  uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();

  etimer_set(&timer, CLOCK_SECOND * 3);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

  for(;;) {
    while(tcp_test_port == 0) {
      etimer_set(&timer, CLOCK_SECOND / 10);
      PROCESS_WAIT_EVENT();
    }

    /* Delay to ensure server has setup the right port */
    etimer_set(&timer, CLOCK_SECOND * 5);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

    while(!NETSTACK_ROUTING.node_is_reachable() ||
          !NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      etimer_set(&timer, CLOCK_SECOND / 10);
      PROCESS_WAIT_EVENT();
    }

    bytes_sent = 0;
    bytes_received = 0;
    last_sent = 0;
    shutdown_test = false;
    can_send = false;
    stream_count++;

    LOG_INFO("Connecting to the TCP server on address ");
    LOG_6ADDR(LOG_LEVEL_INFO, &dest_ipaddr);
    LOG_INFO_(" and port %d\n", tcp_test_port);

    int ret = tcp_socket_register(&client_sock, NULL, in_buf, sizeof(in_buf),
                                  out_buf, sizeof(out_buf),
                                  data_callback, event_callback);
    if(ret < 0) {
      LOG_ERR("Failed to register a TCP socket\n");
      PROCESS_EXIT();
    }

    ret = tcp_socket_connect(&client_sock, &dest_ipaddr, tcp_test_port);
    if(ret < 0) {
      LOG_ERR("Failed to connect\n");
      PROCESS_EXIT();
    }

    tcp_test_port = 0;
    test_stream_length = ((random_rand() & 3) << 16) + (random_rand() & 0xffff);
    LOG_INFO("Sending #%u, %zu bytes to the server...\n",
             stream_count, test_stream_length);

    for(;;) {
      PROCESS_YIELD();
      if(shutdown_test) {
        break;
      } else if(bytes_sent == test_stream_length) {
        LOG_INFO("Sent #%u, %zu bytes successfully\n",
                 stream_count, bytes_sent);
        LOG_INFO("Stream OK\n");
        tcp_socket_close(&client_sock);
      } else if(can_send) {
        size_t bytes_to_send = test_stream_length < sizeof(buf) + bytes_sent ?
          test_stream_length - bytes_sent : sizeof(buf);

        if(!csprng_rand(buf, sizeof(buf))) {
          LOG_WARN("failed to generate random data\n");
        }

        int ret = tcp_socket_send(&client_sock, buf, bytes_to_send);
        if(ret < 0) {
          LOG_ERR("Failed to send %zu bytes\n", bytes_to_send);
          break;
        }
        last_sent = ret;
        can_send = false;
      }
    }

    LOG_INFO("Shutting down\n");
    tcp_socket_unregister(&client_sock);
  }

  PROCESS_END();
}
/*****************************************************************************/
