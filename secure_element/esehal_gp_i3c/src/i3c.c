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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdbool.h>
#include <pthread.h>

#include "libse-gto-private.h"
#include "compiler.h"
#include "i3c.h"

#include <linux/spi/spidev.h>

#define USE_OPEN_RETRY
#define MAX_RETRY_CNT 10

#include <sys/time.h> // For gettimeofday
int readline(int fd, char *buf, size_t buf_size);
int serial_exec(int fd, const char *cmd, uint8_t *response, size_t *response_size);

void suppress_startup_message(int fd) {
    char temp_buf[256];
    char startup_buf[1024] = {0}; // Buffer to accumulate data
    int read_size = 1;
	int nbr_lines = 0;
	char byte; //to read '>'
    do {
		read_size = readline(fd, startup_buf, sizeof(startup_buf));
        nbr_lines++;
    } while(read_size >= 1 && nbr_lines <= 27 );
	read(fd, &byte, 1); // Read one byte at a time
}
struct se_gto_ctx *ctx;
int i3c_setup(struct se_gto_ctx *ctx_input) {
    struct termios options;

    // Open the serial port
    ctx = ctx_input;
    ctx->t1.fd = open(ctx->gtodev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (ctx->t1.fd == -1) {
        perror("[SERIAL] Error opening serial port");
        return -1;
    }

    // Configure the port non-blocking
    fcntl(ctx->t1.fd, F_SETFL, O_NONBLOCK);

    tcgetattr(ctx->t1.fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    tcsetattr(ctx->t1.fd, TCSANOW, &options);

    // Flush serial input buffer
    suppress_startup_message(ctx->t1.fd);
    /**Start by initializing the i3c line and affecting a dynamic address to the chip
	* This can be done by sending this commands to the Pico:
	* i3c_clk 1000
	* i3c_drivestrength 4
	* i3c_targetreset
	* i3c_rstdaa (x3 times)
	* i3c_sdr_ccc_direct_write 0x31 0x87 0x64 (This is SETDASA command to affect dynamic addr = 0x32 to static chip addr 0x31,
	* 0x64 is 0x32 at MSB and b0 = 0 for R/W). This command can be sent twice as sometimes it fails.
	**/
	uint8_t response[260];
	size_t response_size = sizeof(response);
	if (serial_exec(ctx->t1.fd, "i3c_clk 1000", response, &response_size) < 0) {
        perror("[SERIAL] Error i3c_clk 1000");
        return -1;
    }
	if (serial_exec(ctx->t1.fd, "i3c_drivestrength 4", response, &response_size) < 0) {
        perror("[SERIAL] Error i3c_drivestrength 4");
        return -1;
    }
	if (serial_exec(ctx->t1.fd, "i3c_targetreset", response, &response_size) < 0) {
        perror("[SERIAL] Error i3c_targetreset");
        return -1;
    }
	if (serial_exec(ctx->t1.fd, "i3c_rstdaa", response, &response_size) < 0) {
        perror("[SERIAL] Error i3c_rstdaa");
        return -1;
    }
	if (serial_exec(ctx->t1.fd, "i3c_rstdaa", response, &response_size) < 0) {
        perror("[SERIAL] Error i3c_rstdaa");
        return -1;
    }
	if (serial_exec(ctx->t1.fd, "i3c_rstdaa", response, &response_size) < 0) {
        perror("[SERIAL] Error i3c_rstdaa");
        return -1;
    }
	if (serial_exec(ctx->t1.fd, "i3c_sdr_ccc_direct_write 0x0C 0x87 0x64", response, &response_size) < 0){
		perror("[SERIAL] Error i3c_sdr_ccc_direct_write 0x0C 0x87 0x64, Address might be already set");
    }
	usleep(2000);
	/*response_size = sizeof(response);
    memset(&response[0], 0, sizeof(response));
	if (serial_exec(ctx->t1.fd, "i3c_sdr_ccc_direct_read 0x32 0x8C 3", response, &response_size) < 0) {
		perror("[SERIAL] Error i3c_sdr_ccc_direct_read 0x32 0x8C 3");
		return -1;
	}
    if (response_size == 3) dbg("[SERIAL] MRL  = %d\n", (response[0] << 8 | response[1]));

	response_size = sizeof(response);
    memset(&response[0], 0, sizeof(response));
	if (serial_exec(ctx->t1.fd, "i3c_sdr_ccc_direct_read 0x32 0x8B 2", response, &response_size) < 0) {
		perror("[SERIAL] Error i3c_sdr_ccc_direct_read 0x32 0x8B 2");
		return -1;
	}
    if (response_size == 2) dbg("[SERIAL] MWL  = %d\n", (response[0] << 8 | response[1]));

    if (serial_exec(ctx->t1.fd, "i3c_sdr_ccc_direct_write 0x32 0x81 0x03", response, &response_size) < 0) {
		perror("[SERIAL] Error i3c_sdr_ccc_direct_write 0x32 0x81 0x03");
		return -1;
	}
    if (serial_exec(ctx->t1.fd, "i3c_sdr_ccc_direct_write 0x32 0x80 0x01", response, &response_size) < 0) {
		perror("[SERIAL] Error i3c_sdr_ccc_direct_write 0x32 0x80 0x01");
		return -1;
	}*/

    return 0;
}

int i3c_teardown(struct se_gto_ctx *ctx) {
    if (ctx->t1.fd != -1) {
        close(ctx->t1.fd);
        ctx->t1.fd = -1;
    }
    return 0;
}

void byteArrayToHexString(const uint8_t *byteArray, size_t length, char *output, size_t outputSize) {
    // Ensure the output buffer is large enough
    if (outputSize < (length * 5)) { // Each byte needs "0xXY " (4 characters + null terminator)
        fprintf(stderr, "Output buffer too small.\n");
        return;
    }

    output[0] = '\0'; // Initialize output as an empty string

    for (size_t i = 0; i < length; i++) {
        char temp[6]; // Temporary buffer for each "0xXY " part
        snprintf(temp, sizeof(temp), "0x%02X,", byteArray[i]);
        strcat(output, temp); // Append to the output string
    }

    // Remove the trailing space
    size_t len = strlen(output);
    if (len > 0 && output[len - 1] == ' ') {
        output[len - 1] = '\0';
    }
}

uint8_t reveive_buffer [260];
size_t reveive_buffer_size = 260;

int i3c_write(int fd, const void *buf, size_t count) {
    char output[1500];
	char cmdstr[3000];

	reveive_buffer_size = sizeof(reveive_buffer);
    memset(&reveive_buffer[0], 0, sizeof(reveive_buffer));

    byteArrayToHexString(buf, count, output, sizeof(output));

    snprintf(cmdstr, sizeof(cmdstr), "i3c_sdr_write 0x32 %s", output);
	dbg("[SERIAL] Command %s\n", cmdstr);
	if (serial_exec(fd, cmdstr, reveive_buffer, &reveive_buffer_size) < 0) {
        perror("[SERIAL] Error i3c_sdr_write\n");
        return -1;
    }

    return (int)count;
}

int i3c_read(int fd, void *buf, size_t count) {
    int i = 0;
	uint8_t* buff_temp = (uint8_t*) buf;
	char cmdstr[3000];

	snprintf(cmdstr, sizeof(cmdstr), "i3c_sdr_read 0x32 %d",(int)count);
	dbg("[SERIAL] Command %s\n", cmdstr);
	if (serial_exec(fd, cmdstr, buff_temp, &count) < 0) {
        perror("[SERIAL] Error i3c_sdr_read\n");
        return -1;
    }

    return (int)count;
}

int readline(int fd, char *buf, size_t buf_size) {
    size_t total_read = 0;
    char byte;
    ssize_t bytes_read;

    while (total_read < buf_size - 1) { // Leave space for null terminator
        bytes_read = read(fd, &byte, 1); // Read one byte at a time
        if (bytes_read > 0) {
            buf[total_read++] = byte;
            if (byte == '\n') { // Check for newline
                break;
            }
        } else if (bytes_read < 0 && errno != EAGAIN) {
            perror("Error reading from serial port");
            return -1;
        }
    }

    buf[total_read] = '\0'; // Null-terminate the string


    return (int)total_read;
}

int parse_response(const char *resp, char *errorcode, size_t errorcode_size, uint8_t *returnvalues, size_t *returnvalues_count) {
    const char *error_end = strchr(resp, ',');
    size_t count = 0;

    if (error_end) {
        // Extract error code
        size_t len = error_end - resp + 1;
        if (len >= errorcode_size) {
            return -1; // Error code buffer too small
        }
        strncpy(errorcode, resp, len);
        errorcode[len] = '\0';

        // Extract return values
        const char *values = error_end + 1;
        if (*values == ',') {
            values++;
        }
        char *token;
        char value_buf[1500]; // 260bytes x 5 caracters ' 0x01, '
        strncpy(value_buf, values, sizeof(value_buf));
        token = strtok(value_buf, ",");
        while (token && count < *returnvalues_count) {
            returnvalues[count++] = (uint8_t)strtol(token, NULL, 0);
            token = strtok(NULL, ",");
        }
    } else {
        // No values, only error code
        if (strlen(resp) >= errorcode_size) {
            return -1; // Error code buffer too small
        }
        strncpy(errorcode, resp, errorcode_size - 1);
        errorcode[errorcode_size - 1] = '\0';
    }

    *returnvalues_count = count;
    return 0; // Success
}

int serial_exec(int fd, const char *cmd, uint8_t *response, size_t *response_size) {
    char cmdstr[3000];
    snprintf(cmdstr, sizeof(cmdstr), "@%s\r", cmd);
	dbg("[SERIAL] Command %s\n", cmdstr);

    // Write the command
    ssize_t written = write(fd, cmdstr, strlen(cmdstr));
    if (written < 0) {
        return -1; // Error writing
    }
    // Read the response line
	char line_buf[2000];
	char errorcode[100];
    int read_line_size = 0;
retry:
	read_line_size = readline(fd, line_buf, sizeof(line_buf));
    if (read_line_size < 0) {
        return -1; // Error reading
    } else if (read_line_size == 1){
        goto retry;
    }
dbg("[SERIAL] Response readline (%d) :  \n", read_line_size, line_buf);
dbg("[SERIAL] Response data (%d) = \n", *response_size);
	int ret = parse_response(line_buf, errorcode, sizeof(errorcode), response, response_size);
dbg("[SERIAL] Response errorcode ret = %d : %s\n", ret, errorcode);
dbg("[SERIAL] Response data (%d) = ", *response_size);
    for (int i = 0; i < *response_size; i++) {
        dbg("x%02x ", response[i]);
    }
    dbg("\n");
	if (ret < 0 || strstr(errorcode , "OK(0)") == NULL) {
        return -1; // Error reading
    }
    return 0; // Success
}

// Thread function to send the signal after a delay
void* run_ibi_polling(void* arg) {
	IBI_SETUP *setup = (IBI_SETUP*)arg;
    uint8_t response[260];
	size_t response_size = sizeof(response);
    int r = -1;

    //dbg("[SERIAL] Start IBI polling thread\n");

    do {
		usleep(50);
        r = serial_exec(ctx->t1.fd, "i3c_poll", response, &response_size);
        if (r >= 0) {
			setup->ibiHandler(setup->mTid);
            setup->polling_enabled = 0;
            break;
        }
    } while (setup->polling_enabled == 1);

    return NULL;
}

int ibi_poll(IBI_SETUP *setup){
    // Create a thread to send the signal
    pthread_t thread_id;

    if (pthread_create(&thread_id, NULL, run_ibi_polling, setup) != 0) {
        perror("Failed to create thread");
        return -1;
    }
    return 0;
}
