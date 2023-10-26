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
 * T=1 implementation.
 *
 */

#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "iso7816_t1.h"
#include "checksum.h"
#include "transport.h"
#include <stdio.h>

#define T1_REQUEST_RESYNC     0x00
#define T1_REQUEST_IFS        0x01
#define T1_REQUEST_ABORT      0x02
#define T1_REQUEST_WTX        0x03
#define T1_REQUEST_CIP        0x04
#define T1_REQUEST_RELEASE    0x05 /* Custom RESET for SPI version */
#define T1_REQUEST_SWR        0x0F

#define MAX_RETRIES 3

#define MAX_WTX_ROUNDS 3

#define WTX_MAX_VALUE 1

static void
t1_init_recv_window(struct t1_state *t1, void *buf, size_t n)
{
    t1->recv.start = t1->recv.end = buf;
    t1->recv.size  = n;
}

static ptrdiff_t
t1_recv_window_free_size(struct t1_state *t1)
{
    return (ptrdiff_t)t1->recv.size - (t1->recv.end - t1->recv.start);
}

static void
t1_recv_window_append(struct t1_state *t1, const void *buf, int n)
{
    ptrdiff_t free = t1_recv_window_free_size(t1);

    if (n > free)
        n = (int)free;
    if (n > 0) {
        memcpy(t1->recv.end, buf, (size_t)n);
        t1->recv.end += n;
    }
}

static ptrdiff_t
t1_recv_window_size(struct t1_state *t1)
{
    return t1->recv.end - t1->recv.start;
}

static void
t1_close_recv_window(struct t1_state *t1)
{
    t1->recv.start = t1->recv.end;
    t1->recv.size  = 0;
}

static void
t1_init_send_window(struct t1_state *t1, const void *buf, size_t n)
{
    t1->send.start = buf;
    t1->send.end   = t1->send.start + n;
}

static ptrdiff_t
t1_send_window_size(struct t1_state *t1)
{
    return t1->send.end - t1->send.start;
}

static void
t1_close_send_window(struct t1_state *t1)
{
    t1->send.end = t1->send.start;
}

static int
do_chk(struct t1_state *t1, uint8_t *buf)
{
    int n = 4 + (buf[3] | buf[2] << 8);

    switch (t1->chk_algo) {
        case CHECKSUM_LRC:
            buf[n] = lrc8(buf, n);
            n++;
            break;

        case CHECKSUM_CRC: {
            uint16_t crc = crc16_x25(0xFFFF, buf, n);
            buf[n++] = (uint8_t)(crc >> 8);
            buf[n++] = (uint8_t)(crc);
            break;
        }
    }
    return n;
}

static int
chk_is_good(struct t1_state *t1, const uint8_t *buf)
{
    int n = 4 + (buf[3] | buf[2] << 8);
    int match;

    switch (t1->chk_algo) {
        case CHECKSUM_LRC:
            match = (buf[n] == lrc8(buf, n));
            break;

        case CHECKSUM_CRC: {
            uint16_t crc = crc16_x25(0xFFFF, buf, n);
            match = (crc == (buf[n + 1] | (buf[n] << 8)));
            break;
        }

        default:
            match = 0;
    }
    return match;
}

static int
write_iblock(struct t1_state *t1, uint8_t *buf)
{
    ptrdiff_t n = t1_send_window_size(t1);
    uint8_t   pcb;

    /* Card asking for more data whereas nothing is left.*/
    if (n <= 0)
        return -EBADMSG;

    if (n > t1->ifsc)
        n = t1->ifsc, pcb = 0x20;
    else
        pcb = 0;

    if (t1->send.next)
        pcb |= 0x40;

    buf[0] = t1->nad;
    buf[1] = pcb;
    buf[2] = (uint8_t)n >> 8;
    buf[3] = (uint8_t)n;
    memcpy(buf + 4, t1->send.start, (size_t)n);
    return do_chk(t1, buf);
}

static int
write_rblock(struct t1_state *t1, int n, uint8_t *buf)
{
    buf[0] = t1->nad;
    buf[1] = 0x80 | (n & 3);
    if (t1->recv.next)
        buf[1] |= 0x10;
    buf[2] = 0;
    buf[3] = 0;
    return do_chk(t1, buf);
}

static int
write_request(struct t1_state *t1, int request, uint8_t *buf)
{
    uint8_t tmp[2] = {0x00, 0x00};
    buf[0] = t1->nad;
    buf[1] = 0xC0 | request;

    request &= 0x1F;
    if (T1_REQUEST_IFS == request) {
        /* On response, resend card IFS, else this is request for device IFS */
        buf[2] = 0;
        buf[3] = 1;//todo : DONE
        if (buf[1] & 0x20) {
            tmp[1] = (uint8_t) t1->ifsc;
            tmp[0] = (uint8_t) t1->ifsc >> 8;
            if (tmp[0] == 0x00)
                buf[4] = t1->ifsc;
            else {
                buf[4] = tmp[0];
                buf[5] = tmp[1];
                buf[3] = 2;
            }
        }
        else {
            tmp[1] = (uint8_t) t1->ifsd;
            tmp[0] = (uint8_t) t1->ifsd >> 8;
            if (tmp[0] == 0x00)
                buf[4] = t1->ifsd;
            else {
                buf[4] = tmp[0];
                buf[5] = tmp[1];
                buf[3] = 2;
            }
        }
    } else if (T1_REQUEST_WTX == request) {
        buf[2] = 0;
        buf[3] = 1;
        buf[4] = t1->wtx;
    } else {
        buf[2] = 0;
        buf[3] = 0;
    }

    return do_chk(t1, buf);
}

static void
ack_iblock(struct t1_state *t1)
{
    ptrdiff_t n = t1_send_window_size(t1);

    if (n > t1->ifsc)
        n = t1->ifsc;
    t1->send.start += n;

    /* Next packet sequence number */
    t1->send.next ^= 1;
}

/* 0 if not more block, 1 otherwize */
static int
parse_iblock(struct t1_state *t1, uint8_t *buf)
{
    uint8_t pcb  = buf[1];
    uint8_t next = !!(pcb & 0x40);

    if (t1->recv.next == next) {
        t1->recv.next ^= 1;
        t1_recv_window_append(t1, buf + 4, (buf[3] | buf[2] << 8));
        t1->recv_size += (buf[3] | buf[2] << 8);
    }

    /* 1 if more to come */
    return !!(pcb & 0x20);
}

static int
parse_rblock(struct t1_state *t1, uint8_t *buf)
{
    int     r    = 0;
    uint8_t pcb  = buf[1];
    uint8_t next = !!(pcb & 0x10);

    switch (pcb & 0x2F) {
        case 0:
            if ((t1->send.next ^ next) != 0) {
                /* Acknowledge previous block */
                t1->retries = MAX_RETRIES;
                ack_iblock(t1);
            } else {
                t1->retries--;
                if (t1->retries <= 0) r = -ETIMEDOUT;
            }
            break;

        case 1:
            t1->retries--;
            t1->send.next = next;
            r = -EREMOTEIO;
            /* CRC error on previous block, will resend */
            break;

        case 2:
            /* Error */
            t1->state.halt = 1; r = -EIO;
            break;

        case 3:
            t1->retries--;
            r = -EREMOTEIO;
            t1->state.request = 1;
            t1->request       = T1_REQUEST_RESYNC;
            break;

        default:
            t1->state.halt = 1; r = -EOPNOTSUPP;
            break;
    }
    return r;
}

static int
parse_request(struct t1_state *t1, uint8_t *buf)
{
    int n = 0;

    uint8_t request = buf[1] & 0x3F;

    t1->request = request;
    switch (request) {
        case T1_REQUEST_RESYNC:
            if (buf[2] == 0 && buf[3] == 0) {
                t1->retries--;
                n = -EREMOTEIO;
                t1->state.request = 1;
                t1->request       = T1_REQUEST_RESYNC;
            } else
                n = -EBADMSG;
            break;

        case T1_REQUEST_IFS:
            if (buf[2] != 0 || buf[3] == 0 || buf[3] > 2 )
                n = -EBADMSG;
            else if ((buf[4] == 0 && buf[3] == 1) || (buf[4] >= 0x0F && buf[5] >= 0xFA && buf[3] == 2))
                n = -EBADMSG;
            else
                if (buf[3] == 2)
                    t1->ifsc = buf[5] | buf[4] << 8;
                else if (buf[3] == 1)
                    t1->ifsc = buf[4];
            break;

        case T1_REQUEST_ABORT:
            if (buf[2] == 0 && buf[3] == 0) {
                t1->state.aborted = 1;
                t1_close_send_window(t1);
                t1_close_recv_window(t1);
            } else
                n = -EBADMSG;
            break;

        case T1_REQUEST_WTX:
            if (buf[2] != 0x00 || buf[3] > 1) {
                n = -EBADMSG;
                break;
            } else if (buf[3] == 1) {
                t1->wtx = buf[4];
                if (t1->wtx_max_value)
                    if (t1->wtx > WTX_MAX_VALUE)
                        t1->wtx = WTX_MAX_VALUE;
                if (t1->wtx_max_rounds) {
                    t1->wtx_rounds--;
                    if (t1->wtx_rounds <= 0) {
                        t1->retries = 0;
                        n = -ETIMEDOUT;
                    }
                }
            }
            break;

        default:
            n = -EOPNOTSUPP;
            break;
    }

    /* Prepare response for next loop step */
    if (n == 0)
        t1->state.reqresp = 1;

    return n;
}

/* Find if ATR is changing IFSC value */
static void
parse_atr(struct t1_state *t1)
{
    const uint8_t *atr = t1->atr;
    size_t         n = t1->atr_length;
    int            plp_length = 0, iin_length = 0, ifsc_index = 0, bwt_index = 0;

    /*Fast way to get ifsc*/
    iin_length = (int) atr[1];
    plp_length = (int) atr[1 + 1 + iin_length + 1];
    ifsc_index = 1 + 1 + iin_length + 1 + 1 + plp_length + 1 + 2 ;
	bwt_index = 1 + 1 + iin_length + 1 + 1 + plp_length + 1 ;

    if (ifsc_index < t1->atr_length) t1->ifsc = (uint16_t) (atr[ifsc_index] << 8 | atr[ifsc_index + 1] );
	if (bwt_index < t1->atr_length) t1->bwt = (uint16_t) (atr[bwt_index] << 8 | atr[bwt_index + 1] );
	//printf("\nifsc = %d\n", t1->ifsc);
	//printf("\nbwt = %d\n", t1->bwt);
}

/* 1 if expected response, 0 if reemit I-BLOCK, negative value is error */
static int
parse_response(struct t1_state *t1, uint8_t *buf)
{
    int     r;
    uint8_t pcb = buf[1];

    r = 0;

    /* Not a response ? */
    if (pcb & 0x20) {
        pcb &= 0x1F;
        if (pcb == t1->request) {
            r = 1;
            switch (pcb) {
                case T1_REQUEST_IFS:
                    t1->need_ifsd_sync = 0;
/*                    if ((buf[2] != 1) && (buf[3] != t1->ifsd))
                        r = -EBADMSG;*/
                    if (buf[2] != 0 || buf[3] == 0 || buf[3] > 2 )
                        r = -EBADMSG;
                    else if ((buf[4] == 0 && buf[3] == 1) || (buf[4] >= 0x0F && buf[5] >= 0xFA && buf[3] == 2))
                        r = -EBADMSG;
                    break;

                case T1_REQUEST_SWR: //TODO
                    t1->need_reset = 0;
                    break;
                case T1_REQUEST_CIP: //TODO
                    t1->need_cip = 0;
                    if((buf[3] | buf[2] << 8) > 0 && (buf[3] | buf[2] << 8) <= sizeof(t1->atr)) {
                        t1->atr_length = (buf[3] | buf[2] << 8);
                        memcpy(t1->atr, buf + 4, t1->atr_length);
                        parse_atr(t1);
                    } else
                        r = -EBADMSG;
                    break;
                case T1_REQUEST_RESYNC:
                    t1->need_resync = 0;
                    t1->send.next = 0;
                    t1->recv.next = 0;
                    break;
                case T1_REQUEST_ABORT:

                default:
                    /* We never emitted those requests */
                    r = -EBADMSG;
                    break;
            }
        }
    }
    return r;
}

enum { T1_IBLOCK, T1_RBLOCK, T1_SBLOCK };

static int
block_kind(const uint8_t *buf)
{
    if ((buf[1] & 0x80) == 0)
        return T1_IBLOCK;
    else if ((buf[1] & 0x40) == 0)
        return T1_RBLOCK;
    else
        return T1_SBLOCK;
}

static int
read_block(struct t1_state *t1)
{
    int n;

    n = block_recv(t1, t1->buf, sizeof(t1->buf));

    if (n < 0)
        return n;
    else if (n < 3)
        return -EBADMSG;
    else {
        if (!chk_is_good(t1, t1->buf))
            return -EREMOTEIO;

        if (t1->buf[0] != t1->nadc)
            return -EBADMSG;

        if (t1->buf[2] >= 0x0F && t1->buf[3] >= 0xFA)
            return -EBADMSG;
    }

    return n;
}

static int
t1_loop(struct t1_state *t1)
{
    int len;
    int n = 0;

    /* Will happen on first run */
    if (t1->need_cip) {
        t1->state.request = 1;
        t1->request       = T1_REQUEST_CIP;
    } else if (t1->need_reset) {
        t1->state.request = 1;
        t1->request       = T1_REQUEST_SWR;
    }else if (t1->need_resync) {
        t1->state.request = 1;
        t1->request       = T1_REQUEST_RESYNC;
    }else if(t1->need_ifsd_sync){
        t1->state.request = 1;
        t1->request = T1_REQUEST_IFS;
        t1->ifsd    = 254;
    }

    while (!t1->state.halt && t1->retries) {
        if (t1->state.request)
            n = write_request(t1, t1->request, t1->buf);
        else if (t1->state.reqresp) {
            n = write_request(t1, 0x20 | t1->request, t1->buf);
            /* If response is not seen, card will repost request */
            t1->state.reqresp = 0;
        } else if (t1->state.badcrc)
            /* FIXME "1" -> T1_RBLOCK_CRC_ERROR */
            n = write_rblock(t1, 1, t1->buf);
        else if (t1->state.timeout)
            n = write_rblock(t1, 0, t1->buf);
        else if (t1_send_window_size(t1))
            n = write_iblock(t1, t1->buf);
        else if (t1->state.aborted)
            n = -EPIPE;
        else if (t1_recv_window_size(t1) >= 0)
            /* Acknowledges block received so far */
            n = write_rblock(t1, 0, t1->buf);
        else
            /* Card did not send an I-BLOCK for response */
            n = -EBADMSG;

        if (n < 0)
            break;

        len = block_send(t1, t1->buf, n);
        if (len < 0) {
            /* failure to send is permanent, give up immediately */
            n = len;
            break;
        }

        n = read_block(t1);
        if (n < 0) {
            t1->retries--;
            switch (n) {
                /* Error that trigger recovery */
                case -EREMOTEIO:
                    /* Emit checksum error R-BLOCK */
                    t1->state.badcrc = 1;
                    continue;

                case -ETIMEDOUT:
                    /* resend block */
                    t1->state.timeout = 1;
                    /* restore checksum failure error */
                    if (t1->state.badcrc)
                        n = -EREMOTEIO;
                    continue;

                /* Block read implementation failed */
                case -EBADMSG: /* fall through */

                /* Other errors are platform specific and not recoverable. */
                default:
                    t1->retries = 0;
                    continue;
            }
            /* Shall never reach this line */
            break;
        }

        if (t1->state.badcrc)
            if ((t1->buf[1] & 0xEF) == 0x81) {
                /* Resent bad checksum R-BLOCK when response is CRC failure. */
                t1->retries--;
                n = -EREMOTEIO;
                continue;
            }

        t1->state.badcrc  = 0;
        t1->state.timeout = 0;

        if (t1->state.request) {
            if (block_kind(t1->buf) == T1_SBLOCK) {
                n = parse_response(t1, t1->buf);
                switch (n) {
                    case 0:
                        /* Asked to emit same former I-BLOCK */
                        break;

                    case 1:
                        t1->state.request = 0;
                        /* Nothing to do ? leave */
                        if (t1_recv_window_free_size(t1) == 0)
                            t1->state.halt = 1, n = 0;
                        t1->retries = MAX_RETRIES;
                        if(t1->request       == T1_REQUEST_SWR) {
                            t1->state.request = 1;
                            t1->request = T1_REQUEST_CIP;
                            t1->need_cip = 1;
                        } else if(t1->request       == T1_REQUEST_CIP) {
                            t1->state.request = 1;
                            t1->request = T1_REQUEST_IFS;
                            t1->ifsd    = 254;
                            t1->need_ifsd_sync = 1;
                        }
                        continue;

                    default: /* Negative return is error */
                        t1->state.halt = 1;
                        continue;
                }
            }
            /* Re-emit request until response received */
            t1->retries--;
            n = -EBADE;
        } else {
            switch (block_kind(t1->buf)) {
                case T1_IBLOCK:
                    t1->retries = MAX_RETRIES;
                    if (t1_send_window_size(t1))
                        /* Acknowledges last IBLOCK sent */
                        ack_iblock(t1);
                    n = parse_iblock(t1, t1->buf);
                    if (t1->state.aborted)
                        continue;
                    if (t1->recv_size > t1->recv_max) {
                        /* Too much data received */
                        n = -EMSGSIZE;
                        t1->state.halt = 1;
                        continue;
                    }
                    if ((n == 0) && (t1_send_window_size(t1) == 0))
                        t1->state.halt = 1;
                    t1->wtx_rounds = t1->wtx_max_rounds;
                    break;

                case T1_RBLOCK:
                    n = parse_rblock(t1, t1->buf);
                    t1->wtx_rounds = t1->wtx_max_rounds;
                    break;

                case T1_SBLOCK:
                    n = parse_request(t1, t1->buf);
                    if (n == 0)
                        /* Send request response on next loop. */
                        t1->state.reqresp = 1;
                    else if ((n == -EBADMSG) || (n == -EOPNOTSUPP))
                        t1->state.halt = 1;
                    break;
            }
        }
    }
    return n;
}

static void
t1_clear_states(struct t1_state *t1)
{
    t1->state.halt    = 0;
    t1->state.request = 0;
    t1->state.reqresp = 0;
    t1->state.badcrc  = 0;
    t1->state.timeout = 0;
    t1->state.aborted = 0;

    t1->wtx     = 1;
    t1->retries = MAX_RETRIES;
    t1->request = 0xFF;

    t1->wtx_rounds = t1->wtx_max_rounds;

    t1->send.start = t1->send.end = NULL;
    t1->recv.start = t1->recv.end = NULL;
    t1->recv.size  = 0;

    t1->recv_size = 0;  /* Also count discarded bytes */
}

static void
t1_init(struct t1_state *t1)
{
    t1_clear_states(t1);

    t1->chk_algo = CHECKSUM_CRC;
    t1->ifsc     = 64;
    t1->ifsd     = 254;
    t1->bwt      = 300; /* milliseconds */

    t1->send.next = 0;
    t1->recv.next = 0;

    t1->need_reset  = 1;
    t1->need_resync = 0;
    t1->need_cip    = 1;
    t1->spi_fd      = -1;

    t1->wtx_max_rounds = MAX_WTX_ROUNDS;
    t1->wtx_max_value  = 1;

    t1->recv_max  = 65536 + 2; /* Maximum for extended APDU response */
    t1->recv_size = 0;
}

static void
t1_release(struct t1_state *t1)
{
    t1->state.halt = 1;
}

static void
t1_bind(struct t1_state *t1, int src, int dst)
{
    src &= 7;
    dst &= 7;

    t1->nad  = src | (dst << 4);
    t1->nadc = dst | (src << 4);
}

static int
t1_reset(struct t1_state *t1);

static int
t1_cip(struct t1_state *t1);

static int
t1_resync(struct t1_state *t1);

static int
t1_transceive(struct t1_state *t1, const void *snd_buf,
              size_t snd_len, void *rcv_buf, size_t rcv_len)
{
    int n, r;

start:
    t1_clear_states(t1);

    t1_init_send_window(t1, snd_buf, snd_len);
    t1_init_recv_window(t1, rcv_buf, rcv_len);


    n = t1_loop(t1);
    if (n == 0)
        /* Received APDU response */
        n = (int)t1_recv_window_size(t1);
    else if (n < 0  && t1->state.aborted != 1){
        if (!(t1->state.request == 1 && t1->request == T1_REQUEST_RESYNC) && !(t1->state.request == 1 && t1->request == T1_REQUEST_SWR)&& !(t1->request == T1_REQUEST_WTX))
        {
            /*Request Soft RESET to the secure element*/
            //t1_resync(t1);
            t1->need_resync = 1;
			t1->need_reset = 0;
			t1->need_cip = 0;
            goto start;
            /*if (n == 0)
                n = (int)t1_recv_window_size(t1);
            else if (n < 0 && t1->state.aborted != 1 && !(t1->state.request == 1 && t1->request == T1_REQUEST_SWR))
            {
                r = t1_reset(t1);
                if (r < 0) n = -0xDEAD;
            }*/
        } else
        {
            /*Request Soft RESET to the secure element*/
            if(!(t1->state.request == 1 && t1->request == T1_REQUEST_SWR)) {
				t1_close_send_window(t1);
                t1_close_recv_window(t1);
                r = t1_reset(t1);
                if (r < 0) n = -0xDEAD; /*Fatal error meaning eSE is not responding to reset*/
            }
        }
    }
    return n;
}

static int
t1_negotiate_ifsd(struct t1_state *t1, int ifsd)
{
    t1_clear_states(t1);
    t1->need_reset = 0;
    t1->need_resync = 0;
    t1->need_cip = 0;
    t1->state.request = 1;

    t1->request = T1_REQUEST_IFS;
    t1->ifsd    = ifsd;
    return t1_loop(t1);
}

static int
t1_reset(struct t1_state *t1)
{
    t1_clear_states(t1);
    t1->need_reset = 1;
    t1->need_resync = 0;
    t1->need_cip = 0;
    return t1_loop(t1);
}

static int
t1_cip(struct t1_state *t1)
{
    t1_clear_states(t1);
    t1->need_cip = 1;
    t1->need_reset = 0;
    t1->need_resync = 0;
    return t1_loop(t1);
}

static int
t1_resync(struct t1_state *t1)
{
    t1->state.halt    = 0;
    t1->state.request = 0;
    t1->state.reqresp = 0;
    t1->state.badcrc  = 0;
    t1->state.timeout = 0;
    t1->state.aborted = 0;

    t1->wtx     = 1;
    t1->retries = MAX_RETRIES;
    t1->request = 0xFF;

    t1->wtx_rounds = t1->wtx_max_rounds;

    t1->send.next = 0;
    t1->recv.next = 0;
    t1->need_resync = 1;
	t1->need_reset = 0;
    t1->need_cip = 0;

    return t1_loop(t1);
}

void
isot1_init(struct t1_state *t1)
{
    return t1_init(t1);
}

void
isot1_release(struct t1_state *t1)
{
    t1_release(t1);
}

void
isot1_bind(struct t1_state *t1, int src, int dst)
{
    t1_bind(t1, src, dst);
}

int
isot1_transceive(struct t1_state *t1, const void *snd_buf,
                 size_t snd_len, void *rcv_buf, size_t rcv_len)
{
    return t1_transceive(t1, snd_buf, snd_len, rcv_buf, rcv_len);
}

int
isot1_negotiate_ifsd(struct t1_state *t1, int ifsd)
{
    return t1_negotiate_ifsd(t1, ifsd);
}

int
isot1_reset(struct t1_state *t1)
{
    return t1_reset(t1);
}

int
isot1_cip(struct t1_state *t1)
{
    return t1_cip(t1);
}

int
isot1_resync(struct t1_state *t1)
{
    return t1_resync(t1);
}

int
isot1_get_atr(struct t1_state *t1, void *atr, size_t n)
{
    int r = 0;

    if (t1->need_cip)
        r = t1_cip(t1);
                printf("\nr = %d\n", r);

    if (r >= 0) {
        if (t1->atr_length <= n) {
            r = t1->atr_length;
            memcpy(atr, t1->atr, r);
        } else
            r = -EFAULT;
    }
    return r;
}
