/*****************************************************************************
 * Copyright ©2017-2026 Thales. All rights Reserved.
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
 * libse-thales main functions.
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

#include "libse-thales.h"
#include "transport.h"
#include "libse-thales-private.h"

#define ESE_NAD_C 0x12

#include <sys/ioctl.h>
#include <se_gemalto.h>

#include "compiler.h"

int _thales_checkAlive(struct thalesEse_ctx *ctx);

//********************************************//
// mutex

int _thalesEse_lock_mutex(void);
int _thalesEse_unlock_mutex(void);

#ifdef MULTI_THREADING
#include <pthread.h>
#include <time.h>
static pthread_mutex_t sessionLock = PTHREAD_MUTEX_INITIALIZER;

int _thalesEse_lock_mutex(void){
    struct timespec timeout;

    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += MUTEX_LOCK_TIMEOUT;
    int result = pthread_mutex_timedlock(&sessionLock, &timeout);

    return result;
}

int _thalesEse_unlock_mutex(void){
    return pthread_mutex_unlock(&sessionLock);
}
#else
int _thalesEse_lock_mutex(void){ return 0;}
int _thalesEse_unlock_mutex(void){ return 0;}
#endif

//********************************************//
// logger

static int
log_level(const char *priority)
{
    char *endptr;
    int   prio;

    if(priority == NULL)
        return -1;

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
log_stderr(struct thalesEse_ctx *ctx, const char *s)
{
    fputs(s, stderr);
}


//********************************************//
// getters/setters


SE_THALES_EXPORT void
thalesEse_get_version(char* version, size_t size)
{
    if(size < 10)
        return;

    snprintf(version, size, "%d.%d.%d",
                LIBSE_THALES_MAJOR_VERSION,
                LIBSE_THALES_MINOR_VERSION,
                LIBSE_THALES_PATCH_VERSION);
}


SE_THALES_EXPORT void *
thalesEse_get_userdata(struct thalesEse_ctx *ctx)
{
    if (ctx == NULL)
        return NULL;

    return ctx->userdata;
}

SE_THALES_EXPORT void
thalesEse_set_userdata(struct thalesEse_ctx *ctx, void *userdata)
{
    if (ctx == NULL)
        return;

    ctx->userdata = userdata;
}
SE_THALES_EXPORT int
thalesEse_get_log_level(struct thalesEse_ctx *ctx)
{
    if (ctx == NULL)
        return 0;
    return ctx->log_level;
}

SE_THALES_EXPORT void
thalesEse_set_log_level(struct thalesEse_ctx *ctx, int level)
{
    if (ctx == NULL)
        return;

    if (level < 0)
        level = 0;
    else if (level > 4)
        level = 4;
    ctx->log_level = level;
}

SE_THALES_EXPORT thalesEse_log_fn *
thalesEse_get_log_fn(struct thalesEse_ctx *ctx)
{
    if (ctx == NULL)
        return NULL;
    return ctx->log_fn;
}

SE_THALES_EXPORT void
thalesEse_set_log_fn(struct thalesEse_ctx *ctx, thalesEse_log_fn *fn)
{
    if (ctx == NULL)
        return;
    ctx->log_fn = fn;
}

SE_THALES_EXPORT const char *
thalesEse_get_devNode(struct thalesEse_ctx *ctx)
{
    if (ctx == NULL)
        return NULL;
    else
        return ctx->devNode;
}

SE_THALES_EXPORT void
thalesEse_set_devnode(struct thalesEse_ctx *ctx, const char *devNode)
{
    if (ctx == NULL)
        return;
    ctx->devNode = strdup(devNode);
}

SE_THALES_EXPORT void
thalesEse_set_frequency(struct thalesEse_ctx *ctx, int frequency)
{
    if (ctx == NULL)
        return;

    //check frequency validation
    if (frequency < FREQUENCY_MIN || frequency > FREQUENCY_MAX)
        return;

    ctx->frequency = frequency;
}


//********************************************//
// manager eSE communication/exchanges

SE_THALES_EXPORT int
thalesEse_new(struct thalesEse_ctx **c)
{
    int err = -1;
    if(_thalesEse_lock_mutex() == 0){ //0 for success else mutex not available
        const char        *env;
        struct thalesEse_ctx *ctx;

        ctx = calloc(1, sizeof(struct thalesEse_ctx));
        if (!ctx) {
            errno = ENOMEM;
            *c = NULL;
            goto close;
        }

        isot1_init(&ctx->t1);

        ctx->log_fn = log_stderr;
        ctx->devNode = DEFAULT_DEVICE_NODE;
        ctx->frequency = FREQUENCY;

        ctx->log_level = 2;
        /* environment overwrites config */
        env = getenv("SE_THA_LOG");
        if (env != NULL)
            thalesEse_set_log_level(ctx, log_level(env));

        debug("ctx %p created\n", ctx);
        debug("log_level=%d\n", ctx->log_level);
        *c = ctx;
        err = 0;
        close:
        _thalesEse_unlock_mutex();
    }
    return err;
}

SE_THALES_EXPORT int
thalesEse_reset(struct thalesEse_ctx *ctx, void *atr, size_t r)
{
    int err = -1;
    if (ctx == NULL)
        return err;

    if(_thalesEse_lock_mutex() == 0){ //0 for success else mutex not available
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
        _thalesEse_unlock_mutex();
    }
    return err;
}

SE_THALES_EXPORT int
thalesEse_apdu_transmit(struct thalesEse_ctx *ctx, const void *apdu, int n, void *resp, int r)
{
    int err = -1;
    if (ctx == NULL)
        return err;
    if(_thalesEse_lock_mutex() == 0){ //0 for success else mutex not available
        if (!apdu || (n < 4) || !resp || (r < 2)) {
            errno = EINVAL;
            _thalesEse_unlock_mutex();
            return err;
        }
        r = isot1_transceive(&ctx->t1, apdu, n, resp, r);
        if (r < 0 || resp == NULL) {
            errno = -r;
            error("failed to read APDU response, %s\n", strerror(-r));
        } else if (r < 2) {
            error("APDU response too short, only %d bytes, needs 2 at least\n", r);
        }

        _thalesEse_unlock_mutex();

        if (r == -0xDEAD)
            return -0xDEAD;
        if (r < 2){
            ctx->check_alive = 1;
            return err;
        }
        else {
            return r;
        }
    }

    return err;
}

SE_THALES_EXPORT int
thalesEse_open(struct thalesEse_ctx *ctx)
{
    int err = -1;
    if (ctx == NULL)
        return err;
    if(_thalesEse_lock_mutex() == 0){ //0 for success else mutex not available
        info("eSE THALES: using %s\n", ctx->devNode);

        if (transport_setup(ctx) < 0) {
            error("failed to set up devnode.\n");
            transport_teardown(ctx);
            goto close;
        }
        ctx->check_alive = 0;

        isot1_bind(&ctx->t1, ESE_NAD_C & 0x0F, (ESE_NAD_C >> 4) & 0xFF);

        debug("fd: spi=%d\n", ctx->t1.file_descriptor);
        err = 0;

        close:
        _thalesEse_unlock_mutex();
    }

    return err;
}

int _thalesEse_getCPLC(struct thalesEse_ctx *ctx);
int _thalesEse_getCPLC(struct thalesEse_ctx *ctx)
{
    unsigned char apdu[5]= {0x80,0xCA,0x9F,0x7F,0x2D};
    unsigned char resp[258] = {0,};
    if (ctx == NULL)
        return -1;

    int result = isot1_transceive(&ctx->t1, apdu, sizeof(apdu), resp, sizeof(resp));
    if (result < 0) {
        errno = -result;
        error("failed to read APDU response, %s\n", strerror(-result));
    } else if (result < 2) {
        error("APDU response too short, only %d bytes, needs 2 at least\n", result);
    }
    if (result == -0xDEAD || result < 2)
        return -1;
    else
        return 0;

}

SE_THALES_EXPORT int
thalesEse_close(struct thalesEse_ctx *ctx)
{
    int status = -1;
    if (ctx == NULL)
        return status;
    if(_thalesEse_lock_mutex() == 0){ //0 for success else mutex not available
        status = 0;
        debug("thalesEse_close check_alive = %d\n", ctx->check_alive);
        if (ctx->check_alive == 1)
            if (_thalesEse_getCPLC(ctx) != 0)
                status = 0xDEAD;

        (void)isot1_release(&ctx->t1);
        (void)transport_teardown(ctx);
    #ifdef ENABLE_LOGGING
        //clear log buffer
        log_teardown(ctx);
    #endif /* ifdef ENABLE_LOGGING */
        free(ctx);
        _thalesEse_unlock_mutex();
    }
    return status;
}
