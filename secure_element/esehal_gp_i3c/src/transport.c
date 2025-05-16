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

#define NSEC_PER_SEC  1000000000L
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_USEC 1000L

#define ESE_NAD 0x92

IBI_SETUP ibi_setupComponent;
void signal_abort_sleep(pid_t target_tid);

/* < 0 if t1 < t2,
 * > 0 if t1 > t2,
 *   0 if t1 == t2.
 */
static int
ts_compare(const struct timespec *t1, const struct timespec *t2)
{
    if (t1->tv_sec < t2->tv_sec)
        return -1;
    else if (t1->tv_sec > t2->tv_sec)
        return 1;
    else
        return t1->tv_nsec - t2->tv_nsec;
}

static uint32_t
div_uint64_rem(uint64_t dividend, uint32_t divisor, uint64_t *remainder)
{
    uint32_t r    = 0;

    /* Compiler will optimize to modulo on capable platform */
    while (dividend >= divisor)
        dividend -= divisor, r++;

    *remainder = dividend;
    return r;
}

static struct timespec
ts_add_ns(const struct timespec ta, uint64_t ns)
{
    time_t sec = ta.tv_sec +
                 div_uint64_rem(ta.tv_nsec + ns, NSEC_PER_SEC, &ns);
    struct timespec ts = { sec, ns };

    return ts;
}

static int
crc_length(struct t1_state *t1)
{
    int n = 0;

    switch (t1->chk_algo) {
        case CHECKSUM_LRC:
            n = 1;
            break;

        case CHECKSUM_CRC:
            n = 2;
            break;
    }
    return n;
}

int
transport_setup(struct se_gto_ctx *ctx)
{
    if (i3c_setup(ctx) < 0) {
        err("failed to set up se-gto.\n");
        return -1;
    }
    return 0;
}

int transport_teardown(struct se_gto_ctx *ctx)
{
    return i3c_teardown(ctx);
}

int
block_send(struct t1_state *t1, const void *block, size_t n)
{
    if (n < 6)
        return -EINVAL;

    return i3c_write(t1->fd, block, n);
}

void signal_handler(int signum) {
    //printf("Signal %d received, interrupting sleep.\n", signum);
	return;
}

/*struct thread_args {
    struct t1_state *t1;
    void *block;
    size_t n;
};*/

int
block_recv(struct t1_state *t1, void *block, size_t n)
{
    uint8_t  c;
    int      fd;
    uint8_t *s, i;
    int      len, max;
    long     bwt;
    int ret = 0;

    struct timespec ts, ts_timeout;

    // Set up the signal handler
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0; // No special flags
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    if (n < 6)
        return -EINVAL;

    fd = t1->fd;
    s  = block;

    bwt     = t1->bwt * (t1->wtx ? t1->wtx : 1);
    t1->wtx = 1;
    i = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    ts_timeout = ts_add_ns(ts, bwt * NSEC_PER_MSEC);

    do {
        ibi_setupComponent.polling_enabled = 1;
        ibi_setupComponent.ibiHandler = &signal_abort_sleep;
        ibi_setupComponent.mTid = syscall(SYS_gettid);

        ret = ibi_poll(&ibi_setupComponent);
        if (ret < 0) return ret;

        // Wait for 300ms
        ts = ts_add_ns(ts, bwt * NSEC_PER_MSEC);
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL)){
            if  (errno == 0)
                return -ETIMEDOUT;
            else
                break;
        }

        len = i3c_read(fd, &c, 1);
        if (len < 0)
            return len;

    } while (c != ESE_NAD);

    s[i++] = c;

    /* Minimal length is 4 + sizeof(checksum)2 */
    max = 3 + crc_length(t1);
    // Wait for 100us
    ibi_setupComponent.polling_enabled = 1;
    ret = ibi_poll(&ibi_setupComponent);
    if (ret < 0) return ret;

    ts = ts_add_ns(ts, 100 * NSEC_PER_USEC);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL)){
        if  (errno == 0)
            return -ETIMEDOUT;
        else
            break;
    }
    len = i3c_read(fd, s + 1, max);
    if (len < 0)
        return len;

    i += len;

    /* verify that buffer is large enough. */
    max += (s[3] | s[2] << 8);
    if ((size_t)max > n)
        return -ENOMEM;

    /* get block remaining if present */
    if ((s[3] | s[2] << 8)) {
        // Wait for 100us
        ibi_setupComponent.polling_enabled = 1;
        ret = ibi_poll(&ibi_setupComponent);
        if (ret < 0) return ret;

        ts = ts_add_ns(ts, 100 * NSEC_PER_USEC);
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL)){
            if  (errno == 0)
                return -ETIMEDOUT;
            else
                break;
        }
        len = i3c_read(fd, s + 6, (s[3] | s[2] << 8));
        if (len < 0)
            return len;
    }

    return max + 1;
}

/*void *block_recv_thread(void *arg) {
    // Set up the signal handler
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0; // No special flags
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    struct thread_args *args = (struct thread_args *)arg;

    // Access the parameters
    struct t1_state *t1 = args->t1;
    void *block = args->block;
    size_t n = args->n;

    // Perform some logic and compute a return value
    int result = 0; // Example result
    result = block_recv_internal(t1, block, n);
    // Free the arguments
    free(args);

    // Return the result
    pthread_exit((void *)(intptr_t)result);
}

int block_recv(struct t1_state *t1, void *block, size_t n){

    // Set up the arguments (use actual values for these fields)
    struct thread_args *args = malloc(sizeof(struct thread_args));
    args->t1 = t1;
    args->block = block;
    args->n = n;
    // Create a thread to send the signal
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, block_recv_thread, args) != 0) {
        perror("Failed to create thread");
        return -1;
    }

    // Wait for the thread to finish and get the return value
    void *retval;
    if (pthread_join(thread_id, &retval) != 0) {
        perror("pthread_join failed");
        return EXIT_FAILURE;
    }

    // Cast the return value back to int
    int result = (int)(intptr_t)retval;
    printf("Thread returned: %d\n", result);
    return result;
}*/

void signal_abort_sleep(pid_t target_tid) {
    // Send the SIGUSR1 signal to stop clock_nanosleep
    pid_t pid = getpid(); // Get current process ID
    syscall(SYS_tgkill, pid, target_tid, SIGUSR1); // Send SIGUSR1

    return;
}
