/*
 * Copyright (c) 2017, RISE SICS.
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

/**
 * \file
 *         i2c streaming
 * \author
 *         Niklas Wirström <niklas.wirstrom@ri.se>
 *
 */

#ifndef I2C_DEV_STREAM_H_
#define I2C_DEV_STREAM_H_

#include "dev/i2c-dev.h"

typedef enum {
  I2C_STREAM_START,
  I2C_STREAM_LAST_CHUNK,
  I2C_STREAM_CONTINUE,
  I2C_STREAM_END
} i2c_dev_stream_state_t;

typedef struct _chunk i2c_dev_stream_t;
typedef i2c_dev_stream_state_t (*i2c_chunk_handler)(i2c_dev_stream_t *);

struct _chunk {
  volatile int length;
  volatile uint8_t *buffer;
  volatile i2c_chunk_handler handler;
};

i2c_dev_status_t i2c_dev_read_register_stream(i2c_device_t *dev, uint8_t reg, i2c_dev_stream_t *stream);

#endif /* I2C_DEV_STREAM_H_ */
