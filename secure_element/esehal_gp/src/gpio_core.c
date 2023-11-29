#ifndef POLL_MODE
#include <linux/gpio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <time.h>
#include "libse-gto-private.h"

#include "gpio_core.h"

struct pollfd pfd;
int offset = 0;

typedef enum
{
    APP_OPT_GPIO_LIST,
    APP_OPT_GPIO_READ,
    APP_OPT_GPIO_WRITE,
    APP_OPT_GPIO_POLL,
    APP_OPT_UNKNOWN
} app_mode_t;

typedef struct
{
    char *dev;
    int offset;
    uint8_t val;
    app_mode_t mode;
} app_opt_t;

void gpio_write(const char *dev_name, int offset, uint8_t value)
{
    struct gpiohandle_request rq;
    struct gpiohandle_data data;
    int fd, ret;
    printf("\nWrite value %d to GPIO at offset %d (OUTPUT mode) on chip %s\n", value, offset, dev_name);
    fd = open(dev_name, O_RDONLY);
    if (fd < 0)
    {
        printf("\nUnabled to open %s: %s", dev_name, strerror(errno));
        return;
    }
    rq.lineoffsets[0] = offset;
    rq.flags = GPIOHANDLE_REQUEST_OUTPUT;
    rq.lines = 1;
    ret = ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &rq);
    close(fd);
    if (ret == -1)
    {
        printf("\nUnable to line handle from ioctl : %s", strerror(errno));
        return;
    }
    data.values[0] = value;
    ret = ioctl(rq.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    if (ret == -1)
    {
        printf("\nUnable to set line value using ioctl : %s", strerror(errno));
    }
    else
    {
         usleep(2000000);
    }
    close(rq.fd);
}

void gpio_read(const char *dev_name, int offset)
{
    struct gpiohandle_request rq;
    struct gpiohandle_data data;
    int fd, ret;
    fd = open(dev_name, O_RDONLY);
    if (fd < 0)
    {
        printf("\nUnabled to open %s: %s", dev_name, strerror(errno));
        return;
    }
    rq.lineoffsets[0] = offset;
    rq.flags = GPIOHANDLE_REQUEST_INPUT;
    rq.lines = 1;
    ret = ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &rq);
    close(fd);
    if (ret == -1)
    {
        printf("\nUnable to get line handle from ioctl : %s", strerror(errno));
        return;
    }
    ret = ioctl(rq.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
    if (ret == -1)
    {
        printf("\nUnable to get line value using ioctl : %s", strerror(errno));
    }
    else
    {
        printf("\nValue of GPIO at offset %d (INPUT mode) on chip %s: %d\n", offset, dev_name, data.values[0]);
    }
    close(rq.fd);
}

int
gpio_interrupt_setup(struct se_gto_ctx *ctx)
{
    int fd, ret;

    memset(&ctx->t1.rq, 0, sizeof(ctx->t1.rq));

    ctx->t1.gpio_interrupt_fd = open(ctx->interrupt_gpio_chipset, O_RDONLY);
    if (ctx->t1.gpio_interrupt_fd < 0)
    {
        return -1;
    }
	ctx->t1.rq.fd = -1;
	offset = ctx->interrupt_gpio_offset;
	return 0;
}

int
gpio_interrupt_teardown(struct se_gto_ctx *ctx)
{
    if (ctx->t1.rq.fd >= 0)
        if (close(ctx->t1.rq.fd) < 0)
            warn("failed to close rq.fd to %s, %s.\n", ctx->interrupt_gpio_chipset, strerror(errno));
    ctx->t1.rq.fd = -1;

    if (ctx->t1.gpio_interrupt_fd >= 0)
        if (close(ctx->t1.gpio_interrupt_fd) < 0)
            warn("failed to close gpio_interrupt_fd to %s, %s.\n", ctx->t1.gpio_interrupt_fd, strerror(errno));
    ctx->t1.gpio_interrupt_fd = -1;
    return 0;
}

int gpio_poll(struct t1_state *t1, int timeout)
{
    int ret;
    char buf[1024];
    struct timespec ts;

    int flags = fcntl(t1->rq.fd, F_GETFL);

    if (flags == -1) {
		t1->rq.lineoffset = offset;
		t1->rq.handleflags |= GPIOHANDLE_REQUEST_INPUT;
		t1->rq.eventflags |= GPIOEVENT_EVENT_RISING_EDGE;
		ret = ioctl(t1->gpio_interrupt_fd, GPIO_GET_LINEEVENT_IOCTL, &t1->rq);
		if (ret == -1)
		{
			printf("\nUnable to get line event from ioctl : %s\n", strerror(errno));
			return -1;
		}
    }

    pfd.fd = t1->rq.fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, timeout);

    if (ret == -1)
    {
        printf("\nError while polling event from GPIO: %s\n", strerror(errno));
        return -1;
    }
    else if (pfd.revents & POLLIN)
    {
		// Add a short delay to debounce the signal
		lseek(pfd.fd, 0, SEEK_SET);
        ssize_t s = read(pfd.fd, buf, sizeof(buf));
		if (s == -1) {
			perror("read");
			fprintf(stderr, "Error flushing interrupt file fd = %d : %s\n",pfd.fd,  strerror(errno));
		}
    }
    else if (ret == 0) {
        return -2;
    }
    return 0;
}
#endif