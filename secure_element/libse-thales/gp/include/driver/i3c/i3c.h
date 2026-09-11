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
 * Interface to transport to transparently send/receive data.
 *
 */

#ifndef SPI_H
#define SPI_H

typedef void (*ibi_handler)(pid_t target_tid);

typedef struct
{
	int                     polling_enabled;
	ibi_handler				ibiHandler;
	pid_t                   mTid;
}IBI_SETUP;

int i3c_setup(struct thalesEse_ctx *ctx);
int i3c_teardown(struct thalesEse_ctx *ctx);
int i3c_write(int fd, const void *buf, size_t count);
int i3c_read(int fd, void *buf, size_t count);
int ibi_poll(IBI_SETUP *setup);

#endif /* SPI_H */
