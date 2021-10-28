/*****************************************************************************
 * Copyright ©2017-2019 Gemalto – a Thales Company. All rights Reserved.
 *
 * This copy is licensed under the Apache License, Version 2.0 (the "License");
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *     http://www.apache.org/licenses/LICENSE-2.0 or https://www.apache.org/licenses/LICENSE-2.0.html 
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.

 ****************************************************************************/

/**
 * @file
 * $Author$
 * $Revision$
 * $Date$
 *
 * libse-gto main functions.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <fcntl.h>
#include <log/log.h>
#include <sys/ioctl.h>

#include "se-gto/libse-gto.h"
#include "libse-gto-private.h"
#include "spi.h"

#define SE_GTO_GTODEV "/dev/gto"

SE_GTO_EXPORT void *
se_gto_get_userdata(struct se_gto_ctx *ctx)
{
    if (ctx == NULL)
        return NULL;

    return ctx->userdata;
}

SE_GTO_EXPORT void
se_gto_set_userdata(struct se_gto_ctx *ctx, void *userdata)
{
    if (ctx == NULL)
        return;

    ctx->userdata = userdata;
}

static int
log_level(const char *priority)
{
    char *endptr;
    int   prio;

    prio = strtol(priority, &endptr, 10);
    if ((endptr[0] == '\0') || isspace(endptr[0]))
        return prio;

    if (strncmp(priority, "err", 3) == 0)
        return 0;

    if (strncmp(priority, "info", 4) == 0)
        return 3;

    if (strncmp(priority, "debug", 5) == 0)
        return 4;

    return 0;
}

static void
log_stderr(struct se_gto_ctx *ctx, const char *s)
{
    //fputs(s, stderr);
    ALOGD("%s",s);
}

SE_GTO_EXPORT int
se_gto_new(struct se_gto_ctx **c)
{
    const char        *env;
    struct se_gto_ctx *ctx;

    ctx = calloc(1, sizeof(struct se_gto_ctx));
    if (!ctx) {
        errno = ENOMEM;
        return -1;
    }

    isot1_init(&ctx->t1);

    ctx->log_fn = log_stderr;

    ctx->gtodev = SE_GTO_GTODEV;

    ctx->log_level = 2;
    /* environment overwrites config */
    env = getenv("SE_GTO_LOG");
    if (env != NULL)
        se_gto_set_log_level(ctx, log_level(env));

    dbg("ctx %p created\n", ctx);
    dbg("log_level=%d\n", ctx->log_level);
    *c = ctx;
    return 0;
}

SE_GTO_EXPORT int
se_gto_get_log_level(struct se_gto_ctx *ctx)
{
    return ctx->log_level;
}

SE_GTO_EXPORT void
se_gto_set_log_level(struct se_gto_ctx *ctx, int level)
{
    if (level < 0)
        level = 0;
    else if (level > 4)
        level = 4;
    ctx->log_level = level;
}

SE_GTO_EXPORT se_gto_log_fn *
se_gto_get_log_fn(struct se_gto_ctx *ctx)
{
    return ctx->log_fn;
}

SE_GTO_EXPORT void
se_gto_set_log_fn(struct se_gto_ctx *ctx, se_gto_log_fn *fn)
{
    ctx->log_fn = fn;
}

SE_GTO_EXPORT const char *
se_gto_get_gtodev(struct se_gto_ctx *ctx)
{
    return ctx->gtodev;
}

SE_GTO_EXPORT void
se_gto_set_gtodev(struct se_gto_ctx *ctx, const char *gtodev)
{
    ctx->gtodev = strdup(gtodev);
}

SE_GTO_EXPORT int
se_gto_reset(struct se_gto_ctx *ctx, void *atr, size_t r)
{
    int err;

    err = isot1_reset(&ctx->t1);
    if (err < 0) {
        errno = -err;
        ctx->check_alive = 1;
    }
    else {
        err = isot1_get_atr(&ctx->t1, atr, r);
        if (err < 0)
            errno = -err;
    }
    return err;
}

SE_GTO_EXPORT int
se_gto_apdu_transmit(struct se_gto_ctx *ctx, const void *apdu, int n, void *resp, int r)
{
    if (!apdu || (n < 4) || !resp || (r < 2)) {
        errno = EINVAL;
        return -1;
    }
    r = isot1_transceive(&ctx->t1, apdu, n, resp, r);
    dbg("isot1_transceive: r=%d\n", r);
    dbg("isot1_transceive: ctx->t1.recv.end - ctx->t1.recv.start = %d\n", ctx->t1.recv.end - ctx->t1.recv.start);
    dbg("isot1_transceive: ctx->t1.recv.size = %zu\n", ctx->t1.recv.size);
    dbg("isot1_transceive: ctx->t1.buf[2] = %02X\n", ctx->t1.buf[2]);
    if (r < 0) {
        errno = -r;
        err("failed to read APDU response, %s\n", strerror(-r));
    } else if (r < 2) {
        err("APDU response too short, only %d bytes, needs 2 at least\n", r);
    }
    if (r < 2){
        ctx->check_alive = 1;
        return -1;
    } else
        return r;
}

SE_GTO_EXPORT int
se_gto_open(struct se_gto_ctx *ctx)
{
    info("eSE GTO: using %s\n", ctx->gtodev);

    if (spi_setup(ctx) < 0) {
        err("failed to set up se-gto.\n");
        return -1;
    }

    ctx->check_alive = 0;

    isot1_bind(&ctx->t1, 0x2, 0x1);

    dbg("fd: spi=%d\n", ctx->t1.spi_fd);
    return 0;
}

int se_gto_Spi_Reset()
{
    /**
	* @Todo: Add proprietary SPI Reset code here
	**/
    return 0;
}

int gtoSPI_checkAlive(struct se_gto_ctx *ctx);
int gtoSPI_checkAlive(struct se_gto_ctx *ctx)
{
  int ret = 0;
  int count = 3;
  unsigned char apdu[5]= {0x80,0xCA,0x9F,0x7F,0x2D};
  unsigned char resp[258] = {0,};

recheck:
  /*Check Alive implem*/
  ret = se_gto_apdu_transmit(ctx, apdu, 5, resp, sizeof(resp));
  if(ret < 0){
    if (count == 0) return -1;
    count--;
    /*Run SPI reset*/
    se_gto_Spi_Reset();
    goto recheck;
  }

  return 0;
}

void dump_bytes(struct se_gto_ctx *ctx, char sep, const uint8_t *p, int n)
{
    const uint8_t *s = p;
    char *msg;
    int len = 0;
    int input_len = n;

    msg = (char*) malloc (input_len * 3 + 1);
    if(!msg) {
        errno = ENOMEM;
        return;
    }

    while (input_len--) {
        len += sprintf(msg + len, "%02X" , *s++);
        //len = len + 2;
        if (input_len && sep) {
            len += sprintf(msg + len, ":");
            //len++;
        }
    }
    sprintf(msg + len, "\n");
    dbg("data = %s", msg);

    if(msg) free(msg);
}

int gtoSPI_getSeVersion(struct se_gto_ctx *ctx)
{
  int r = 0;
  int count = 3;
  unsigned char select_aid_apdu[14]= {0x00,0xA4,0x04,0x00,0x08,0xA0,0x00,0x00,0x01,0x51,0x00,0x00,0x00,0x00};
  unsigned char get_cplc_apdu[5]= {0x80,0xCA,0x9F,0x7F,0x00};
  unsigned char get_fe_apdu[5]= {0x80,0xCA,0x00,0xFE,0x00};
  unsigned char resp[258] = {0,};

  r = se_gto_apdu_transmit(ctx, select_aid_apdu, 14, resp, sizeof(resp));
  if(r < 2){
    err("gtoSPI_getSeVersion Error on select ISD\n");
    return -1;
  }
  else {
    if (resp[r - 2] != 0x90){
        err("gtoSPI_getSeVersion select ISD bad SW \n");
        return -1;
    }
	else {
        r = se_gto_apdu_transmit(ctx, get_cplc_apdu, 5, resp, sizeof(resp));
        if(r < 2){
            err("gtoSPI_getSeVersion Error on get CPLC\n");
            return -1;
        }
        else {
            dbg("gtoSPI_getSeVersion CPLC data : \n");
            dump_bytes(ctx, ':', resp, r); 
            if (resp[r - 2] != 0x90){
                err("gtoSPI_getSeVersion get CPLC bad SW \n");
                return -1;
            }
	        else {
                r = se_gto_apdu_transmit(ctx, get_fe_apdu, 5, resp, sizeof(resp));
                dbg("gtoSPI_getSeVersion FE Tag data : \n");
                dump_bytes(ctx, ':', resp, r); 
            }
        }
    }
  }

  return 0;
}

SE_GTO_EXPORT int
se_gto_close(struct se_gto_ctx *ctx)
{
    int status = 0;

    if(ctx) dbg("se_gto_close check_alive = %d\n", ctx->check_alive);
    if (ctx->check_alive == 1)
        if (gtoSPI_checkAlive(ctx) != 0) status = 0xDEAD;

    (void)isot1_release(&ctx->t1);
    (void)spi_teardown(ctx);
    log_teardown(ctx);
    if(ctx) free(ctx);
    return status;
}
