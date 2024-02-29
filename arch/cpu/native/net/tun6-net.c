/*
 * Copyright (c) 2011, Swedish Institute of Computer Science.
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
 * \author
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#include "net/ipv6/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "lib/crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include "sys/platform.h"

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "Tun6"
#define LOG_LEVEL LOG_LEVEL_WARN

#ifdef linux
#include <linux/if.h>
#include <linux/if_tun.h>
#endif

#include <err.h>
#include "net/netstack.h"
#include "net/packetbuf.h"

static FILE *open_pcap(const char *filename);
static void write_pcap_packet(FILE *pcap_file);
static void close_pcap(FILE *pcap_file);

static const char *pcap_output_file;
static FILE *pcap_file;

static const char *config_ipaddr = "fd00::1/64";
/* Allocate some bytes in RAM and copy the string */
static char config_tundev[IFNAMSIZ + 1] = "tun0";

static int tunfd = -1;

static int set_fd(fd_set *rset, fd_set *wset);
static void handle_fd(fd_set *rset, fd_set *wset);
static const struct select_callback tun_select_callback = {
  set_fd,
  handle_fd
};

static int ssystem(const char *fmt, ...)
     __attribute__((__format__ (__printf__, 1, 2)));

int
static ssystem(const char *fmt, ...)
{
  char cmd[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(cmd, sizeof(cmd), fmt, ap);
  va_end(ap);
  LOG_INFO("%s\n", cmd);
  fflush(stdout);
  return system(cmd);
}

/*---------------------------------------------------------------------------*/
static void
cleanup(void)
{
#define TMPBUFSIZE 128
  /* Called from signal handler, avoid unsafe functions. */
  char buf[TMPBUFSIZE];
  strcpy(buf, "ifconfig ");
  /* Will not overflow, but null-terminate to avoid spurious warnings. */
  buf[TMPBUFSIZE - 1] = '\0';
  strncat(buf, config_tundev, TMPBUFSIZE - strlen(buf) - 1);
  strncat(buf, " down", TMPBUFSIZE - strlen(buf) - 1);
  system(buf);
#ifndef linux
  system("sysctl -w net.ipv6.conf.all.forwarding=1");
#endif
  strcpy(buf, "netstat -nr"
         " | awk '{ if ($2 == \"");
  buf[TMPBUFSIZE - 1] = '\0';
  strncat(buf, config_tundev, TMPBUFSIZE - strlen(buf) - 1);
  strncat(buf, "\") print \"route delete -net \"$1; }'"
          " | sh", TMPBUFSIZE - strlen(buf) - 1);
  system(buf);

  if(pcap_file) {
    close_pcap(pcap_file);
    pcap_file = NULL;
  }
}

/*---------------------------------------------------------------------------*/
static void CC_NORETURN
sigcleanup(int signo)
{
  const char *prefix = "signal ";
  const char *sig =
    signo == SIGHUP ? "HUP\n" : signo == SIGTERM ? "TERM\n" : "INT\n";
  write(fileno(stderr), prefix, strlen(prefix));
  write(fileno(stderr), sig, strlen(sig));
  cleanup();
  _exit(0);
}

/*---------------------------------------------------------------------------*/
static void
ifconf(const char *tundev, const char *ipaddr)
{
#ifdef linux
  ssystem("ifconfig %s inet `hostname` up", tundev);
  ssystem("ifconfig %s add %s", tundev, ipaddr);
#elif defined(__APPLE__)
  ssystem("ifconfig %s inet6 %s up", tundev, ipaddr);
  ssystem("sysctl -w net.inet.ip.forwarding=1");
#else
  ssystem("ifconfig %s inet `hostname` %s up", tundev, ipaddr);
  ssystem("sysctl -w net.inet.ip.forwarding=1");
#endif /* !linux */

  /* Print the configuration to the console. */
  ssystem("ifconfig %s\n", tundev);
}
/*---------------------------------------------------------------------------*/
#ifdef linux
static int
tun_alloc(char *dev, uint16_t devsize)
{
  struct ifreq ifr;
  int fd, err;
  LOG_INFO("Opening: %s\n", dev);
  if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
    /* Error message handled by caller */
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));

  /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
   *        IFF_NO_PI - Do not provide packet information
   */
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if(*dev != '\0') {
    memcpy(ifr.ifr_name, dev, MIN(sizeof(ifr.ifr_name), devsize));
  }
  if((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
    /* Error message handled by caller */
    close(fd);
    return err;
  }

  LOG_INFO("Using '%s' vs '%s'\n", dev, ifr.ifr_name);
  strncpy(dev, ifr.ifr_name, MIN(devsize - 1, sizeof(ifr.ifr_name)));
  dev[devsize - 1] = '\0';
  LOG_INFO("Using %s\n", dev);
  return fd;
}
#else
static int
devopen(const char *dev, int flags)
{
  char t[32];
  strcpy(t, "/dev/");
  strncat(t, dev, sizeof(t) - 5);
  return open(t, flags);
}
/*---------------------------------------------------------------------------*/
static int
tun_alloc(char *dev, uint16_t devsize)
{
  LOG_INFO("Opening: %s\n", dev);
  return devopen(dev, O_RDWR);
}
#endif
/*---------------------------------------------------------------------------*/
static void
tun_init()
{
  setvbuf(stdout, NULL, _IOLBF, 0); /* Line buffered output. */

  if(pcap_output_file) {
    pcap_file = open_pcap(pcap_output_file);
    if(pcap_file == NULL) {
      fprintf(stderr, "Warning: failed to open PCAP file '%s' for writing!\n",
              pcap_output_file);
    } else {
      fprintf(stderr, "Saving PCAP to %s\n", pcap_output_file);
    }
  }

  LOG_INFO("Initializing tun interface\n");

  tunfd = tun_alloc(config_tundev, sizeof(config_tundev));
  if(tunfd == -1) {
    LOG_WARN("Failed to open tun device '%s' "
             "(you may be lacking permission). Running without network.\n",
             config_tundev);
    /* err(1, "failed to allocate tun device ``%s''", config_tundev); */
    return;
  }

  LOG_INFO("Tun open:%d\n", tunfd);

  select_set_callback(tunfd, &tun_select_callback);

  fprintf(stderr, "opened %s device ``/dev/%s''\n",
          "tun", config_tundev);

  atexit(cleanup);
  signal(SIGHUP, sigcleanup);
  signal(SIGTERM, sigcleanup);
  signal(SIGINT, sigcleanup);
  ifconf(config_tundev, config_ipaddr);
}

/*---------------------------------------------------------------------------*/
static int
tun_output(uint8_t *data, int len)
{
  /* fprintf(stderr, "*** Writing to tun...%d\n", len); */
  if(tunfd != -1 && write(tunfd, data, len) != len) {
    err(1, "serial_to_tun: write");
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
static int
tun_input(unsigned char *data, int maxlen)
{
  int size;

  if(tunfd == -1) {
    /* tun is not open */
    return 0;
  }

  if((size = read(tunfd, data, maxlen)) == -1) {
    err(1, "tun_input: read");
  }
  return size;
}

/*---------------------------------------------------------------------------*/
static uint8_t
output(const linkaddr_t *localdest)
{
  LOG_DBG("SUT: %u\n", uip_len);
  if(uip_len > 0) {
    if(pcap_file) {
      write_pcap_packet(pcap_file);
    }
    return tun_output(uip_buf, uip_len);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/
/* tun and slip select callback                                              */
/*---------------------------------------------------------------------------*/
static int
set_fd(fd_set *rset, fd_set *wset)
{
  if(tunfd == -1) {
    return 0;
  }

  FD_SET(tunfd, rset);
  return 1;
}

/*---------------------------------------------------------------------------*/

static void
handle_fd(fd_set *rset, fd_set *wset)
{
  int size;

  if(tunfd == -1) {
    /* tun is not open */
    return;
  }

  if(FD_ISSET(tunfd, rset)) {
    size = tun_input(uip_buf, sizeof(uip_buf));
    LOG_DBG("TUN data incoming read:%d\n", size);
    if(size > 0) {
      uip_len = size;
      if(pcap_file) {
        write_pcap_packet(pcap_file);
      }
      tcpip_input();
    }
  }
}

static void input(void)
{
  /* should not happen */
  LOG_DBG("Tun6 - input\n");
}


const struct network_driver tun6_net_driver ={
  "tun6",
  tun_init,
  input,
  output
};


/*---------------------------------------------------------------------------*/
static int
tun_callback(const char *optarg)
{
  strncpy(config_tundev, optarg, sizeof(config_tundev) - 1);
  return 0;
}
CONTIKI_OPTION(CONTIKI_MIN_INIT_PRIO + 10,
               {"tun", required_argument, NULL, 't'},
               tun_callback, "set tun device", "tundev");
/*---------------------------------------------------------------------------*/
static int
pcap_callback(const char *optarg)
{
  pcap_output_file = optarg;
  return 0;
}
CONTIKI_OPTION(CONTIKI_MIN_INIT_PRIO + 11,
               {"pcap", required_argument, NULL, 0},
               pcap_callback,
               "save incoming and outgoing IP packets to pcap file",
               "pcap-output-file");
/*---------------------------------------------------------------------------*/
/* TODO PCAP should be moved to another module */
/*---------------------------------------------------------------------------*/
#define PCAP_ETHER_TYPE_IPV4 0x0800
#define PCAP_ETHER_TYPE_ARP  0x0806
#define PCAP_ETHER_TYPE_IPV6 0x86dd

#define ETH_HEADER_SIZE 14
#define ETH_CRC32_SIZE 4

// Define the PCAP file header structure
struct pcap_file_header {
  uint32_t magic_number;   // Magic number (0xa1b2c3d4)
  uint16_t version_major;  // Major version (2)
  uint16_t version_minor;  // Minor version (4)
  int32_t thiszone;        // Time zone correction (unused)
  uint32_t sigfigs;        // Accuracy of timestamps (unused)
  uint32_t snaplen;        // Maximum packet length (65535)
  uint32_t linktype;       // Link-layer header type (1 for Ethernet)
};

// Define the PCAP packet header structure
struct pcap_packet_header {
  uint32_t ts_sec;         // Timestamp seconds
  uint32_t ts_usec;        // Timestamp microseconds
  uint32_t caplen;         // Captured packet length
  uint32_t len;            // Original packet length
};

// Define the Ethernet header structure
struct ethernet_header {
  uint8_t dest_mac[6];     // Destination MAC address
  uint8_t src_mac[6];      // Source MAC address
  uint16_t ether_type;     // EtherType (0x0800 for IPv4, 0x86DD for IPv6)
};
/*---------------------------------------------------------------------------*/
static FILE *
open_pcap(const char *filename)
{
  FILE *pcap_file = fopen(filename, "wb");
  if(!pcap_file) {
    return NULL;
  }

  struct pcap_file_header file_header = {
    .magic_number = 0xa1b2c3d4,
    .version_major = 2,
    .version_minor = 4,
    .thiszone = 0,
    .sigfigs = 0,
    .snaplen = 4096,
    .linktype = 1  // Ethernet link type
  };

  fwrite(&file_header, sizeof(file_header), 1, pcap_file);
  fflush(pcap_file);

  return pcap_file;
}
/*---------------------------------------------------------------------------*/
static void
log_eth(const uint8_t eth_addr[6])
{
  LOG_OUTPUT("%02x", eth_addr[0]);
  for(int i = 1; i < 6; i++) {
    LOG_OUTPUT(":%02x", eth_addr[i]);
  }
}
/*---------------------------------------------------------------------------*/
static void
ipv6_to_eth_addr(const uip_ip6addr_t *addr, uint8_t eth_addr[6])
{
  const uint8_t *eui64 = &addr->u8[8];
  /* Reverse the universal/local bit inversion */
  eth_addr[0] = eui64[0] ^ 0x02;
  eth_addr[1] = eui64[1];
  eth_addr[2] = eui64[2];
  eth_addr[3] = eui64[5];
  eth_addr[4] = eui64[6];
  eth_addr[5] = eui64[7];
}
/*---------------------------------------------------------------------------*/
static void
write_pcap_packet(FILE *pcap_file)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);

  if(uip_len + ETH_HEADER_SIZE + ETH_CRC32_SIZE > UIP_BUFSIZE) {
    LOG_ERR("PCAP: too large IP packet: %u bytes\n", uip_len);
    return;
  }

  struct pcap_packet_header packet_header = {
    .ts_sec = tv.tv_sec,
    .ts_usec = tv.tv_usec,
    .caplen = uip_len + ETH_HEADER_SIZE + ETH_CRC32_SIZE,
    .len = uip_len + ETH_HEADER_SIZE + ETH_CRC32_SIZE
  };

  struct ethernet_header eth_header = {
    .dest_mac = {0},
    .src_mac = {0},
    .ether_type = UIP_HTONS(PCAP_ETHER_TYPE_IPV6)
  };
  ipv6_to_eth_addr(&UIP_IP_BUF->destipaddr, eth_header.dest_mac);
  ipv6_to_eth_addr(&UIP_IP_BUF->srcipaddr, eth_header.src_mac);

  if(LOG_INFO_ENABLED) {
    LOG_INFO("PCAP IPv6: ");
    LOG_INFO_6ADDR(&UIP_IP_BUF->srcipaddr);
    LOG_INFO_(" (");
    log_eth(eth_header.src_mac);
    LOG_INFO_(") to ");
    LOG_INFO_6ADDR(&UIP_IP_BUF->destipaddr);
    LOG_INFO_(" (");
    log_eth(eth_header.dest_mac);
    LOG_INFO_(") %u bytes\n", uip_len);
  }

  memmove(&uip_buf[ETH_HEADER_SIZE], uip_buf, uip_len);
  memcpy(&uip_buf[0], &eth_header, ETH_HEADER_SIZE);

  uint32_t checksum = crc32(uip_buf, uip_len + ETH_HEADER_SIZE);
  uip_buf[uip_len + ETH_HEADER_SIZE] = checksum & 0xff;
  uip_buf[uip_len + ETH_HEADER_SIZE + 1] = (checksum >> 8L) & 0xff;
  uip_buf[uip_len + ETH_HEADER_SIZE + 2] = (checksum >> 16L) & 0xff;
  uip_buf[uip_len + ETH_HEADER_SIZE + 3] = (checksum >> 24L) & 0xff;

  fwrite(&packet_header, sizeof(packet_header), 1, pcap_file);
  fwrite(uip_buf, uip_len + ETH_HEADER_SIZE + ETH_CRC32_SIZE, 1, pcap_file);
  fflush(pcap_file);

  memmove(uip_buf, &uip_buf[ETH_HEADER_SIZE], uip_len);
}
/*---------------------------------------------------------------------------*/
static void
close_pcap(FILE *pcap_file)
{
  if(pcap_file) {
    fclose(pcap_file);
  }
}
/*---------------------------------------------------------------------------*/
