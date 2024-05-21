#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <inttypes.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define WITH_SERVER_REPLY  1
uint16_t udp_client_port = 0; 	/* 8765 */
uint16_t udp_server_port = 0; 	/* 5678 */

#define SEND_INTERVAL		  (30 * CLOCK_SECOND)

static struct simple_udp_connection udp_conn;
static uint32_t rx_count = 0;

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client");
AUTOSTART_PROCESSES(&udp_client_process);
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{

  LOG_INFO("Received response '%.*s' from ", datalen, (char *) data);
  LOG_INFO_6ADDR(sender_addr);
#if LLSEC802154_CONF_ENABLED
  LOG_INFO_(" LLSEC LV:%d", uipbuf_get_attr(UIPBUF_ATTR_LLSEC_LEVEL));
#endif
  LOG_INFO_("\n");
  rx_count++;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer periodic_timer;
  static uint32_t tx_count;
  static uint32_t missed_tx_count;
  uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();

  while(udp_server_port == 0 || udp_client_port == 0) {
    etimer_set(&periodic_timer, CLOCK_SECOND / 10);
    PROCESS_WAIT_EVENT();
  }

  LOG_INFO("Client started with server port %u and client port %u\n",
           udp_server_port, udp_client_port);

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, udp_client_port, NULL,
                      udp_server_port, udp_rx_callback);

  etimer_set(&periodic_timer, random_rand() % SEND_INTERVAL);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

    if(NETSTACK_ROUTING.node_is_reachable() &&
        NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {

      /* Print statistics every 10th TX */
      if(tx_count % 10 == 0) {
        LOG_INFO("Tx/Rx/MissedTx: %" PRIu32 "/%" PRIu32 "/%" PRIu32 "\n",
                 tx_count, rx_count, missed_tx_count);
      }

#if UIP_BUFSIZE < 1200
#error Too small UIP buffer
#endif
      char *buf = (char *)&uip_buf[UIP_IPUDPH_LEN];

      uipbuf_clear();
      snprintf(buf, UIP_BUFSIZE - UIP_IPUDPH_LEN - 1,
               "hello %" PRIu32 "", tx_count);
      uint16_t payload_len = MAX(strlen(buf) + 1, random_rand() % 1024);
      for(int i = strlen(buf) + 2; i < payload_len; i++) {
        buf[i] = (char)(random_rand() & 0xff);
      }
      /* Send to DAG root */
      LOG_INFO("Sending request %"PRIu32" to ", tx_count);
      LOG_INFO_6ADDR(&dest_ipaddr);
      LOG_INFO_(" (%u bytes)\n", payload_len);
      simple_udp_sendto(&udp_conn, buf, payload_len, &dest_ipaddr);
      tx_count++;
    } else {
      LOG_INFO("Not reachable yet\n");
      if(tx_count > 0) {
        missed_tx_count++;
      }
    }

    /* Add some jitter */
    etimer_set(&periodic_timer, SEND_INTERVAL
      - CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
