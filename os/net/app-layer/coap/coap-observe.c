/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
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
 */

/**
 * \file
 *      CoAP module for observing resources (draft-ietf-core-observe-11).
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

/**
 * \addtogroup coap
 * @{
 */

#include <stdio.h>
#include <string.h>
#include "coap-observe.h"
#include "coap-engine.h"
#include "lib/memb.h"
#include "lib/list.h"

/* Log configuration */
#include "coap-log.h"
#define LOG_MODULE "coap"
#define LOG_LEVEL  LOG_LEVEL_COAP

#define NOTIFICATION_RETRY_PERIOD            5000

#define COAP_OBSERVER_STATE_TRANSACTION   (1 << 0)
#define COAP_OBSERVER_STATE_REMOVED       (1 << 1)
/*---------------------------------------------------------------------------*/
MEMB(observers_memb, coap_observer_t, COAP_MAX_OBSERVERS);
LIST(unactive_observers_list);
LIST(pending_observers_list);

static coap_timer_t observers_timer;
/*---------------------------------------------------------------------------*/
static void coap_observers_send_notification();
/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static coap_observer_t *
add_observer(const coap_endpoint_t *endpoint, const uint8_t *token,
             size_t token_len, const char *uri, int uri_len)
{
  /* Remove existing observe relationship, if any. */
  coap_remove_observer_by_uri(endpoint, uri);

  coap_observer_t *o = memb_alloc(&observers_memb);

  if(o) {
    memset(o, 0, sizeof(coap_observer_t));

    /* Save space for the null termination */
    memcpy(o->url, uri, MIN(uri_len, sizeof(o->url) - 1));

    coap_endpoint_copy(&o->endpoint, endpoint);
    o->token_len = token_len;
    memcpy(o->token, token, token_len);
    o->last_mid = 0;

    LOG_INFO("Adding observer (%u/%u) for /%s [0x%02X%02X]\n",
             list_length(unactive_observers_list) + 1, COAP_MAX_OBSERVERS,
             o->url, o->token[0], o->token[1]);
    list_add(unactive_observers_list, o);
  }

  return o;
}
/*---------------------------------------------------------------------------*/
/*- Removal -----------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void
coap_remove_observer(coap_observer_t *o)
{
  if(o->state & COAP_OBSERVER_STATE_TRANSACTION) {
    /* The observer is in an active transation - can not free its memory now */
    o->state |= COAP_OBSERVER_STATE_REMOVED;
    LOG_INFO("Pending remove of observer for /%s [0x%02X%02X] in transaction\n",
             o->url, o->token[0], o->token[1]);
  } else {
    LOG_INFO("Removing observer for /%s [0x%02X%02X]\n", o->url, o->token[0],
             o->token[1]);
    list_remove(unactive_observers_list, o);
    list_remove(pending_observers_list, o);
    memb_free(&observers_memb, o);
  }
}
/*---------------------------------------------------------------------------*/
void
coap_remove_all_observers(void)
{
  coap_observer_t *obs, *obs_aux;
  for(obs = list_head(unactive_observers_list); obs; obs = obs_aux) {
    obs_aux = obs->next;
    coap_remove_observer(obs);
  }
  for(obs = list_head(pending_observers_list); obs; obs = obs_aux) {
    obs_aux = obs->next;
    coap_remove_observer(obs);
  }
}
/*---------------------------------------------------------------------------*/
int
coap_remove_observer_by_client(const coap_endpoint_t *endpoint)
{
  int removed = 0;
  coap_observer_t *obs, *obs_aux;

  LOG_DBG("Remove check client ");
  LOG_DBG_COAP_EP(endpoint);
  LOG_DBG_("\n");
  for(obs = list_head(unactive_observers_list); obs; obs = obs_aux) {
    obs_aux = obs->next;
    if(coap_endpoint_cmp(&obs->endpoint, endpoint)) {
      coap_remove_observer(obs);
      removed++;
    }
  }
  for(obs = list_head(pending_observers_list); obs; obs = obs_aux) {
    obs_aux = obs->next;
    if(coap_endpoint_cmp(&obs->endpoint, endpoint)) {
      coap_remove_observer(obs);
      removed++;
    }
  }
  return removed;
}
/*---------------------------------------------------------------------------*/
static int
remove_observer_by_token(const coap_endpoint_t *endpoint,
                         uint8_t *token, size_t token_len,
                         coap_observer_t *obs)
{
  int removed = 0;
  coap_observer_t *obs_aux;

  for(; obs; obs = obs_aux) {
    obs_aux = obs->next;
    if(coap_endpoint_cmp(&obs->endpoint, endpoint)
       && obs->token_len == token_len
       && memcmp(obs->token, token, token_len) == 0) {
      coap_remove_observer(obs);
      removed++;
    }
  }
  return removed;
}
/*---------------------------------------------------------------------------*/
int
coap_remove_observer_by_token(const coap_endpoint_t *endpoint,
                              uint8_t *token, size_t token_len)
{
  LOG_DBG("Remove check Token 0x%02X%02X\n", token[0], token[1]);

  return remove_observer_by_token(endpoint, token, token_len,
                                  list_head(unactive_observers_list)) +
    remove_observer_by_token(endpoint, token, token_len,
                             list_head(pending_observers_list));
}
/*---------------------------------------------------------------------------*/
static int
remove_observer_by_uri(const coap_endpoint_t *endpoint,
                       const char *uri, coap_observer_t *obs)
{
  int removed = 0;
  coap_observer_t *obs_aux;

  for(; obs; obs = obs_aux) {
    obs_aux = obs->next;
    if((endpoint == NULL || (coap_endpoint_cmp(&obs->endpoint, endpoint)))
       && (obs->url == uri || strcmp(obs->url, uri) == 0)) {
      coap_remove_observer(obs);
      removed++;
    }
  }
  return removed;
}
/*---------------------------------------------------------------------------*/
int
coap_remove_observer_by_uri(const coap_endpoint_t *endpoint,
                            const char *uri)
{
  LOG_DBG("Remove check URL %s\n", uri);

  return remove_observer_by_uri(endpoint, uri,
                                list_head(unactive_observers_list)) +
    remove_observer_by_uri(endpoint, uri, list_head(pending_observers_list));
}
/*---------------------------------------------------------------------------*/
int
coap_remove_observer_by_mid(const coap_endpoint_t *endpoint, uint16_t mid)
{
  int removed = 0;
  coap_observer_t *obs, *obs_aux;

  LOG_DBG("Remove check MID %u\n", mid);

  for(obs = list_head(unactive_observers_list); obs; obs = obs_aux) {
    obs_aux = obs->next;
    if(coap_endpoint_cmp(&obs->endpoint, endpoint) && obs->last_mid == mid) {
      coap_remove_observer(obs);
      removed++;
    }
  }

  for(obs = list_head(pending_observers_list); obs; obs = obs_aux) {
    obs_aux = obs->next;
    if(coap_endpoint_cmp(&obs->endpoint, endpoint) && obs->last_mid == mid) {
      coap_remove_observer(obs);
      removed++;
    }
  }
  return removed;
}
/*---------------------------------------------------------------------------*/
/*- Notification ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void
coap_notify_observers(coap_resource_t *resource)
{
  coap_notify_observers_sub(resource, NULL);
}
/* Can be used either for sub - or when there is not resource - just
   a handler */
void
coap_notify_observers_sub(coap_resource_t *resource, const char *subpath)
{
  int url_len, obs_url_len;
  char url[COAP_OBSERVER_URL_LEN];
  uint8_t sub_ok = 0;
  coap_observer_t *obs = NULL;
  coap_observer_t *obs_aux = NULL;

  if(resource != NULL) {
    url_len = strlen(resource->url);
    strncpy(url, resource->url, COAP_OBSERVER_URL_LEN - 1);
    if(url_len < COAP_OBSERVER_URL_LEN - 1 && subpath != NULL) {
      strncpy(&url[url_len], subpath, COAP_OBSERVER_URL_LEN - url_len - 1);
    }
  } else if(subpath != NULL) {
    strncpy(url, subpath, COAP_OBSERVER_URL_LEN - 1);
  } else {
    /* No resource, no subpath */
    return;
  }

  /* Ensure url is null terminated because strncpy does not guarantee this */
  url[COAP_OBSERVER_URL_LEN - 1] = '\0';
  /* url now contains the notify URL that needs to match the observer */

  /* iterate over observers */
  url_len = strlen(url);
  /* Assumes lazy evaluation... */
  sub_ok = (resource == NULL) || (resource->flags & HAS_SUB_RESOURCES);

  for(obs = list_head(unactive_observers_list); obs; obs = obs_aux) {
    /* To continue the iteration in the unactive observers list */
    obs_aux = obs->next;

    obs_url_len = strlen(obs->url);

    /* Do a match based on the parent/sub-resource match so that it is
       possible to do parent-node observe */

    /***** TODO fix here so that we handle the notofication correctly ******/
    /* All the new-style ... is assuming that the URL might be within */
    if((obs_url_len == url_len
        || (obs_url_len > url_len
            && sub_ok
            && obs->url[url_len] == '/'))
      && strncmp(url, obs->url, url_len) == 0) {

      list_remove(unactive_observers_list, obs);
      list_add(pending_observers_list, obs);
      LOG_INFO("Marked observer /%s [0x%02X%02X] as pending (Pending list length: %d, Unactive list length: %d)\n",
                obs->url, obs->token[0], obs->token[1], list_length(pending_observers_list), list_length(unactive_observers_list));

      if(list_length(pending_observers_list) == 1) {
        /* First observer, trigger send_notification. Timer is used to avoid stack overflow. */
        coap_timer_set(&observers_timer, 10);
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
coap_observers_con_notification_callback(void *data, coap_message_t *response)
{
  coap_observer_t *obs = data;

  list_remove(pending_observers_list, obs);

  if(obs->state & COAP_OBSERVER_STATE_REMOVED) {
    LOG_INFO("Transaction done, removing observer /%s [0x%02X%02X]\n",
             obs->url, obs->token[0], obs->token[1]);
    memb_free(&observers_memb, obs);
  } else {
    obs->state &= ~COAP_OBSERVER_STATE_TRANSACTION;
    list_add(unactive_observers_list, obs);
    LOG_INFO("Marked observer /%s [0x%02X%02X] as unactive (Pending list length: %d, Unactive list length: %d)\n",
             obs->url, obs->token[0], obs->token[1], list_length(pending_observers_list), list_length(unactive_observers_list));
  }
  /* Trigger send_notification to check if there are more pendings. Timer is used to avoid stack overflow. */
  coap_timer_set(&observers_timer, 1);
}
/*---------------------------------------------------------------------------*/
static void
coap_observers_send_notification(coap_timer_t *timer)
{
  coap_observer_t *obs = list_head(pending_observers_list);
  coap_transaction_t *transaction = NULL;
  int32_t new_offset = 0;

  if(obs && ((obs->state & COAP_OBSERVER_STATE_TRANSACTION) == 0)) {
    /* build notification */
    coap_message_t notification[1]; /* this way the message can be treated as pointer as usual */
    coap_message_t request[1]; /* this way the message can be treated as pointer as usual */

    LOG_INFO("Notification from %s\n", obs->url);

    coap_init_message(notification, COAP_TYPE_NON, CONTENT_2_05, 0);
    /* create a "fake" request for the URI */
    coap_init_message(request, COAP_TYPE_CON, COAP_GET, 0);
    coap_set_header_uri_path(request, obs->url);

    if((transaction = coap_new_transaction(coap_get_mid(), &obs->endpoint))) {
      if(obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0) {
        LOG_DBG("           Force Confirmable for\n");
        notification->type = COAP_TYPE_CON;
      }

      LOG_DBG("           Observer ");
      LOG_DBG_COAP_EP(&obs->endpoint);
      LOG_DBG_("\n");

      /* update last MID for RST matching */
      obs->last_mid = transaction->mid;

      /* prepare response */
      notification->mid = transaction->mid;

      /* Either old style get_handler or the full handler */
      if(coap_call_handlers(request, notification, transaction->message +
                            COAP_MAX_HEADER_SIZE, COAP_MAX_CHUNK_SIZE,
                            &new_offset) > 0) {
        LOG_DBG("Notification on new handlers\n");
      } else {

        /* What to do here? */
        notification->code = BAD_REQUEST_4_00;

      }

      if(notification->code < BAD_REQUEST_4_00) {
        coap_set_header_observe(notification, (obs->obs_counter)++);
        /* mask out to keep the CoAP observe option length <= 3 bytes */
        obs->obs_counter &= 0xffffff;
      }
      coap_set_token(notification, obs->token, obs->token_len);

      if(new_offset != 0) {
        coap_set_header_block2(notification,
                               0,
                               new_offset != -1,
                               COAP_MAX_BLOCK_SIZE);
        coap_set_payload(notification, notification->payload,
                         MIN(notification->payload_len,
                             COAP_MAX_BLOCK_SIZE));
      }

      transaction->message_len =
        coap_serialize_message(notification, transaction->message);

      if(notification->type == COAP_TYPE_CON) {
        /* If notification is confirmable, set a callback to mark as unactive and trigger send_notification again */
        transaction->callback_data = obs;
        transaction->callback = coap_observers_con_notification_callback;
        obs->state |= COAP_OBSERVER_STATE_TRANSACTION;
        coap_send_transaction(transaction);
      } else {
        /* If notification is non confirmable, mark as unactive after sending and trigger send_notification */
        coap_send_transaction(transaction);
        list_remove(pending_observers_list, obs);
        list_add(unactive_observers_list, obs);
        LOG_INFO("Marked observer /%s [0x%02X%02X] as unactive (Pending list length: %d, Unactive list length: %d)\n",
                obs->url, obs->token[0], obs->token[1], list_length(pending_observers_list), list_length(unactive_observers_list));
        coap_timer_set(&observers_timer, 10);
      }
    } else {
      /* No transaction available - retry again after some time */
      coap_timer_set(&observers_timer, NOTIFICATION_RETRY_PERIOD);
    }
  }
}
/*---------------------------------------------------------------------------*/
void
coap_observe_handler(coap_resource_t *resource, coap_message_t *coap_req,
                     coap_message_t *coap_res)
{
  const coap_endpoint_t *src_ep;
  coap_observer_t *obs;

  LOG_DBG("CoAP observer handler rsc: %d\n", resource != NULL);

  if(coap_req->code == COAP_GET && coap_res->code < 128) { /* GET request and response without error code */
    if(coap_is_option(coap_req, COAP_OPTION_OBSERVE)) {
      src_ep = coap_get_src_endpoint(coap_req);
      if(src_ep == NULL) {
        /* No source endpoint, can not add */
      } else if(coap_req->observe == 0) {
        obs = add_observer(src_ep,
                           coap_req->token, coap_req->token_len,
                           coap_req->uri_path, coap_req->uri_path_len);
        if(obs) {
          coap_set_header_observe(coap_res, (obs->obs_counter)++);
          /* mask out to keep the CoAP observe option length <= 3 bytes */
          obs->obs_counter &= 0xffffff;
          /*
           * Following payload is for demonstration purposes only.
           * A subscription should return the same representation as a normal GET.
           * Uncomment if you want an information about the avaiable observers.
           */
#if 0
          static char content[16];
          coap_set_payload(coap_res,
                           content,
                           snprintf(content, sizeof(content), "Added %u/%u",
                                    list_length(unactive_observers_list),
                                    COAP_MAX_OBSERVERS));
#endif
        } else {
          coap_res->code = SERVICE_UNAVAILABLE_5_03;
          coap_set_payload(coap_res, "TooManyObservers", 16);
        }
      } else if(coap_req->observe == 1) {

        /* remove client if it is currently observe */
        coap_remove_observer_by_token(src_ep,
                                      coap_req->token, coap_req->token_len);
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
uint8_t
coap_has_observers(char *path)
{
  coap_observer_t *obs = NULL;
  int pathlen;

  pathlen = strlen(path);
  for(obs = list_head(unactive_observers_list); obs; obs = obs->next) {
    if(strncmp(obs->url, path, pathlen) == 0) {
      return 1;
    }
  }
  for(obs = list_head(pending_observers_list); obs; obs = obs->next) {
    if(strncmp(obs->url, path, pathlen) == 0) {
      return 1;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
void
coap_observe_init(void)
{
  list_init(unactive_observers_list);
  list_init(pending_observers_list);
  coap_timer_set_callback(&observers_timer, coap_observers_send_notification);
}
/*---------------------------------------------------------------------------*/
/** @} */
