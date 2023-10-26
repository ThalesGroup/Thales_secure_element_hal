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
 * Low level interface to SPI/eSE driver.
 *
 */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
//#include <linux/se_gemalto.h>

#include "libse-gto-private.h"
#include "compiler.h"
#include "spi.h"

#include <linux/spi/spidev.h>

#define USE_OPEN_RETRY
#define MAX_RETRY_CNT 10

int
spi_set_speed(struct se_gto_ctx *ctx, uint32_t speed)
{
    int status = -1;
    //----- SET SPI BUS SPEED -----
    //unsigned int spi_speed;
    //spi_speed = 5000000;		//1000000 = 1MHz (1uS per bit)

    status = ioctl(ctx->t1.spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if(status < 0)
    {
      err("Could not set SPI speed (WR)...ioctl fail");
    }
    status = ioctl(ctx->t1.spi_fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if(status < 0)
    {
      err("Could not set SPI speed (RD)...ioctl fail");
    }
    warn("SPI HW Set speed status = %d", status);
    return status;
}

int
spi_setup(struct se_gto_ctx *ctx)
{
#ifdef USE_OPEN_RETRY
    int retryCnt = 0;

retry:
    ctx->t1.spi_fd = open(ctx->gtodev, O_RDWR);
    if (ctx->t1.spi_fd < 0) {
        err("cannot use %s for spi device, errno = 0x%x\n", ctx->gtodev, errno);
        if(errno == -EBUSY || errno == EBUSY) {
            retryCnt++;
            err("Retry open driver - retry cnt : %d\n", retryCnt);
            if(retryCnt < MAX_RETRY_CNT) {
                sleep(1);
                goto retry;
            }
        }
        return -1;
      }
#else
     ctx->t1.spi_fd = open(ctx->gtodev, O_RDWR);
     if (ctx->t1.spi_fd < 0) {
         err("cannot use %s for spi device\n", ctx->gtodev);
         return -1;
     }
#endif
    spi_set_speed(ctx, 5000000);
     return ctx->t1.spi_fd < 0;
}

int
spi_teardown(struct se_gto_ctx *ctx)
{
    if (ctx->t1.spi_fd >= 0)
        if (close(ctx->t1.spi_fd) < 0)
            warn("failed to close fd to %s, %s.\n", ctx->gtodev, strerror(errno));
    ctx->t1.spi_fd = -1;
    return 0;
}

int
spi_write(int fd, const void *buf, size_t count)
{
    ssize_t n;

    n = write(fd, buf, count);
    if ((int)n != n)
        return -EFAULT;

    return (int)n;
}

int
spi_read(int fd, void *buf, size_t count)
{
    ssize_t n;

    n = read(fd, buf, count);
    if ((int)n != n)
        return -EFAULT;

    return (int)n;
}
