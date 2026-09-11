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
 * eSE Gemalto kernel driver transport.
 *
 */
#ifdef I3C

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/syscall.h>

#include "iso7816_t1.h"
#include "transport.h"
#include "i3c.h"
#include "log.h"

#include "libse-thales-private.h"

int
transport_setup(struct thalesEse_ctx *ctx)
{
    // Open dev node
    info("eSE device node: %s\n", ctx->devNode);
    ctx->t1.file_descriptor = open(ctx->devNode, O_RDWR);
    if (ctx->t1.file_descriptor == 0)
        debug("eSE set-up success.\n");
    else
        error("eSE set-up failed.\n");

    return ctx->t1.file_descriptor;
}

int transport_teardown(struct thalesEse_ctx *ctx)
{
   info("eSE teardown %s\n", ctx->devNode);
   int result = 0;
   if (ctx->t1.file_descriptor != -1) {
        result = close(ctx->t1.file_descriptor);
        ctx->t1.file_descriptor = -1;
    }
   return result;
}

int
block_send(struct t1_state *t1, const void *block, size_t n)
{
    if (n < 6)
    {
        return -EINVAL;
    }
    return write(t1->file_descriptor, block, n);
}

int
block_recv(struct t1_state *t1, void *block, size_t n)
{
    int ret = 0;

    // read frame
    ret = read(t1->file_descriptor, block, n);
    // Return specific errno code if error
    if (ret<0)
    {
       ret = -errno;
    }
    return ret;
}
#endif //I3C