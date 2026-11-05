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
 * libse-thales to dialog with device T=1 protocol over SPI.
 *
 * This library is not thread safe on same context.
 */

#ifndef LIBSE_THALES_H
#define LIBSE_THALES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "libse-thales_version.h"

#define MUTEX_LOCK_TIMEOUT 5 //timeout for mutex locking

#undef LOG_TAG
#define LOG_TAG "THALES_GP_LIBSE"

#define FREQUENCY 5000000 //5MHz by default
#define FREQUENCY_MAX 20000000 //20MHz
#define FREQUENCY_MIN 1000000 //1MHz

#define DEFAULT_DEVICE_NODE "/dev/node"

/**
 * library user context - reads the config and system
 * environment, user variables, allows custom logging.
 *
 * Default struct thalesEse_ctx log to stderr file stream.
 * Content is opaque to user.
 */
struct thalesEse_ctx;

/** Function callback to display log line.
 * @s nul terminated string.
 */

typedef void thalesEse_log_fn (struct thalesEse_ctx *ctx, const char *s);


/********************************* Core *************************************/

/** Create se-thales library context.
 *
 * This fills in the default values. Device node for eSE GTO is "@c /dev/node".
 *
 * Use environment variable SE_THA_LOG to alter default log level globally.
 * SE_THA_LOG=n with n from 0 to 4, or choice of SE_THA_LOG to err, info, debug.
 *
 * @returns a new se-thales library context
 */
int thalesEse_new(struct thalesEse_ctx **ctx);

/** Allocates resources from environment and kernel.
 *
 * @c errno is set on error.
 *
 * @return -1 on error, 0 otherwise.
 */
int thalesEse_open(struct thalesEse_ctx *ctx);

/** Release resources.
 *
 * @c errno is set on error.
 * @return -1 on error, 0 otherwise.
 */
int thalesEse_close(struct thalesEse_ctx *ctx);

/******************************* Facilities *********************************/

 /** Returns libse version.
  *
  * @param version Pointer of char* which will store libse version.
  * @param size sizeof char* which will store libse version.
  */
void thalesEse_get_version(char* version, size_t size);

/** Returns current log level.
 *
 * @param ctx: se-thales library context
 *
 * @returns the current logging level
 **/
int thalesEse_get_log_level(struct thalesEse_ctx *ctx);

/**
 * Set the current logging level.
 *
 * @param ctx   se-thales library context
 * @param level the new logging level
 *
 * @c level controls which messages are logged:
 *   0 : error
 *   1 : warning
 *   2 : notice
 *   3 : info
 *   4 : debug
 **/
void thalesEse_set_log_level(struct thalesEse_ctx *ctx, int level);

/** Get current function callback for log entries.
 *
 * Use this function if you want to chain log entries and replace current
 * function by yours.
 *
 * @param ctx se-thales library context.
 *
 * @return current function for log string.
 */
thalesEse_log_fn *thalesEse_get_log_fn(struct thalesEse_ctx *ctx);

/** Set function callback for log entries.
 *
 * @param ctx se-thales library context.
 * @param fn Function to dump nul terminated string.
 */
void thalesEse_set_log_fn(struct thalesEse_ctx *ctx, thalesEse_log_fn *fn);

void *thalesEse_get_userdata(struct thalesEse_ctx *ctx);

/** Store custom userdata in the library context.
 *
 * @param ctx      se-thales library context
 * @param userdata data pointer
 **/
void thalesEse_set_userdata(struct thalesEse_ctx *ctx, void *userdata);

/**************************** HW configuration ******************************/

/** Returns current device node for eSE.
 *
 * Returned string must not be modified. Copy returned string if you need to
 * use it later.
 *
 * @param ctx se-thales library context.
 *
 * @returns nul terminated string.
 */
const char *thalesEse_get_devNode(struct thalesEse_ctx *ctx);

/** Set device node used for eSE.
 *
 * @c devNode is copied se-thales library. You can use a volatile string.
 *
 * @param ctx    se-thales library context.
 * @param devNode full path to device node.
 */
void thalesEse_set_devnode(struct thalesEse_ctx *ctx, const char *devNode);

/** Set frequency in Hz.
 *
 * @param ctx se-thales library context.
 * @param frequency frequency in Hz.
 */
void thalesEse_set_frequency(struct thalesEse_ctx *ctx, int frequency);


/****************************** APDU protocol *******************************/

/** Send reset command to Secure Element.
 *
 * @param ctx se-thales library context
 *
 * @c errno is set on error.
 *
 * @returns -1 on error.
 */
int thalesEse_reset(struct thalesEse_ctx *ctx);

/** Send cip command to Secure Element and return ATR bytes.
 *
 * @param ctx se-thales library context
 * @param atr byte buffer to receive ATR content
 * @param r   length of ATR byte buffer.
 *
 * @c errno is set on error.
 *
 * @returns number of bytes in @c atr buffer or -1 on error.
 */
int thalesEse_cip
(struct thalesEse_ctx *ctx, void *atr, size_t r);

/** Transmit APDU to Secure Element
 *
 * If needed to comply with request from command, multiple ISO7816 Get
 * Response can be emitted to collect the full response.
 *
 * @param ctx  se-thales library context
 * @param apdu APDU command to send
 * @param n    length of APDU command
 * @param resp Response buffer
 * @param r    length of response buffer.
 *
 * @c errno is set on error.
 *
 * @returns number of bytes filled in @c resp buffer. -1 on error.
 *
 * @resp buffer last two bytes are SW1 and SW2 respectively. Response length
 * will always be at least 2 bytes. Maximum response size will be 257 bytes.
 *
 * Slave timeout is used waiting for APDU response. Each Extension Time packet
 * will restart response timeout.
 */
int thalesEse_apdu_transmit(struct thalesEse_ctx *ctx, const void *apdu, int n, void *resp, int r);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ifndef LIBSE_THALES_H */
