/*
 * Copyright (c) 2007, Swedish Institute of Computer Science.
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
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "dev/radio.h"
#include "net/netstack.h"
#include <stdio.h>
/*---------------------------------------------------------------------------*/
PROCESS(rssi_scan_process, "RSSI scanner");
AUTOSTART_PROCESSES(&rssi_scan_process);
/*---------------------------------------------------------------------------*/
static radio_value_t channel_min = 11;
static radio_value_t channel_max = 26;
/*---------------------------------------------------------------------------*/
static void
run_rssi_scan(void)
{
  radio_value_t ch;
  radio_value_t rssi;
  printf("RSSI:");
  for(ch = channel_min; ch <= channel_max; ch++) {
    if(NETSTACK_RADIO.set_value(RADIO_PARAM_CHANNEL, ch) != RADIO_RESULT_OK) {
      printf("ERROR: failed to change radio channel\n");
      break;
    }
    if(NETSTACK_RADIO.get_value(RADIO_PARAM_RSSI, &rssi) != RADIO_RESULT_OK) {
      printf(" ff");
    } else {
      printf(" %02x", rssi & 0xff);
    }
  }
  printf("\n");
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rssi_scan_process, ev, data)
{
  radio_value_t tmp;
  PROCESS_BEGIN();

  NETSTACK_MAC.off();
  NETSTACK_RADIO.on();
  if(NETSTACK_RADIO.get_value(RADIO_CONST_CHANNEL_MIN, &tmp) == RADIO_RESULT_OK) {
    channel_min = tmp;
  }
  if(NETSTACK_RADIO.get_value(RADIO_CONST_CHANNEL_MAX, &tmp) == RADIO_RESULT_OK) {
    channel_max = tmp;
  }

  while(true) {
    run_rssi_scan();
    PROCESS_PAUSE();
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
