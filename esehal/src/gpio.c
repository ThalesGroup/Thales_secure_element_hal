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
#include <linux/se_gemalto.h>

#include "libse-gto-private.h"
#include "gpio.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
 
#define IN  0
#define OUT 1
 
#define LOW  0
#define HIGH 1

//#define POUT 493  /* Hikey 960 board config == gpio493 -- GPIO_021 -- Pin 34 */
#define POUT 452  /* Hikey 960 board config == gpio452 -- GPIO_060 -- Pin 5 */
 
static int
GPIOExport(struct se_gto_ctx *ctx, int pin)
{
#define BUFFER_MAX 3
	char buffer[BUFFER_MAX];
	ssize_t bytes_written;
	int fd;
 
	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (-1 == fd) {
		err("Failed to open export for writing!\n");
		return(-1);
	}
 
	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return(0);
}
 
static int
GPIOUnexport(struct se_gto_ctx *ctx, int pin)
{
	char buffer[BUFFER_MAX];
	ssize_t bytes_written;
	int fd;
 
	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if (-1 == fd) {
		err("Failed to open unexport for writing!\n");
		return(-1);
	}
 
	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return(0);
}
 
static int
GPIODirection(struct se_gto_ctx *ctx, int pin, int dir)
{
	static const char s_directions_str[]  = "in\0out";
 
#define DIRECTION_MAX 35
	char path[DIRECTION_MAX];
	int fd;
 
	snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		err("Failed to open gpio direction for writing!\n");
		return(-1);
	}
 
	if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
		err("Failed to set direction!\n");
		return(-1);
	}
 
	close(fd);
	return(0);
}

static int
GPIOWrite(struct se_gto_ctx *ctx, int pin, int value)
{
#define VALUE_MAX 30
	static const char s_values_str[] = "01";
 
	char path[VALUE_MAX];
	int fd;
 
	snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		err("Failed to open gpio value for writing!\n");
		return(-1);
	}
 
	if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
		err("Failed to write value!\n");
		return(-1);
	}
 
	close(fd);
	return(0);
}

int
gpio_set_power_on(struct se_gto_ctx *ctx)
{
    
    /*
     * Enable GPIO pins
     */
    if (-1 == GPIOExport(ctx, POUT))
        return(1);
 
    /*
     * Set GPIO directions
     */
    if (-1 == GPIODirection(ctx, POUT, OUT))
        return(2);

    /*
     * Write GPIO value 0 to indicate switch power ON
     */
    if (-1 == GPIOWrite(ctx, POUT, 1))
        return(3);

    /*ctx->t1.spi_fd = open(ctx->gtodev, O_RDWR);
    if (ctx->t1.spi_fd < 0) {
        err("cannot use %s for spi device\n", ctx->gtodev);
        return -1;
    }
    return ctx->t1.spi_fd < 0;*/
    return 0;
}

int
gpio_set_power_off(struct se_gto_ctx *ctx)
{
    /*
     * Write GPIO value 0 to indicate switch power OFF
     */
    if (-1 == GPIOWrite(ctx, POUT, 0))
        return(3);

    /*
     * Disable GPIO pins
     */
    if (-1 == GPIOUnexport(ctx, POUT))
        return(4);

    /*if (ctx->t1.spi_fd >= 0)
        if (close(ctx->t1.spi_fd) < 0)
            warn("failed to close fd to %s, %s.\n", ctx->gtodev, strerror(errno));
    ctx->t1.spi_fd = -1;
    return 0;*/
    return 0;
}
