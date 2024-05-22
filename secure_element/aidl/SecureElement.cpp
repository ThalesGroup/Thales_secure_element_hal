/*****************************************************************************
 * Copyright ©2017-2023 Gemalto – a Thales Company. All rights Reserved.
 *
 * This copy is licensed under the Apache License, Version 2.0 (the "License");
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *     http://www.apache.org/licenses/LICENSE-2.0 or https://www.apache.org/licenses/LICENSE-2.0.html
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.

 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <limits.h>
#include <log/log.h>
#include <android-base/properties.h>
#include <dlfcn.h>

#include "se-gto/libse-gto.h"
#include "SecureElement.h"

#define VENDOR_LIB_PATH "/vendor/lib64/"
#define VENDOR_LIB_EXT ".so"

namespace se {

#ifndef BASIC_CHANNEL
#define BASIC_CHANNEL 0x00
#endif

#ifndef LOG_HAL_LEVEL
#define LOG_HAL_LEVEL 4
#endif

#ifndef SUCCESS
#define SUCCESS 0
#endif

#ifndef MAX_AID_LEN
#define MAX_AID_LEN 16
#endif

uint8_t getResponse[5] = {0x00, 0xC0, 0x00, 0x00, 0x00};
static struct se_gto_ctx *ctx;
bool debug_log_enabled = false;

SecureElement::SecureElement(const char* ese_name){
    nbrOpenChannel = 0;
    ctx = NULL;

    strncpy(ese_flag_name, ese_name, 4);
    if (strncmp(ese_flag_name, "eSE2", 4) == 0) {
        strncpy(config_filename, "/vendor/etc/libse-gto-hal2.conf", 31);
    } else {
        strncpy(config_filename, "/vendor/etc/libse-gto-hal.conf", 30);
    }
}

int SecureElement::resetSE(){
    int n;

    isBasicChannelOpen = false;
    nbrOpenChannel = 0;

    ALOGD("SecureElement:%s se_gto_reset start", __func__);
    n = se_gto_reset(ctx, atr, sizeof(atr));
    if (n >= 0) {
        atr_size = n;
        ALOGD("SecureElement:%s received ATR of %d bytes\n", __func__, n);
        dump_bytes("ATR: ", ':',  atr, n, stdout);
    } else {
        ALOGE("SecureElement:%s Failed to reset and get ATR: %s\n", __func__, strerror(errno));
    }

    return n;
}

int SecureElement::initializeSE() {

    int n;
    int ret = 0;

    ALOGD("SecureElement:%s start", __func__);

    if (checkSeUp) {
        ALOGD("SecureElement:%s Already initialized", __func__);
        ALOGD("SecureElement:%s end", __func__);
        return EXIT_SUCCESS;
    }

    if (se_gto_new(&ctx) < 0) {
        ALOGE("SecureElement:%s se_gto_new FATAL:%s", __func__,strerror(errno));

        return EXIT_FAILURE;
    }
    se_gto_set_log_level(ctx, 3);

    openConfigFile(1);

    if (se_gto_open(ctx) < 0) {
        ALOGE("SecureElement:%s se_gto_open FATAL:%s", __func__,strerror(errno));
        return EXIT_FAILURE;
    }

    ret = resetSE();

    if (ret < 0 && (strncmp(ese_flag_name, "eSE2", 4) == 0)) {
        sleep(6);
        ALOGE("SecureElement:%s retry resetSE", __func__);
        ret = resetSE();
    }
    if (ret < 0) {
        se_gto_close(ctx);
        ctx = NULL;
        return EXIT_FAILURE;
    }

    checkSeUp = true;

    ALOGD("SecureElement:%s end", __func__);
    return EXIT_SUCCESS;
}

ScopedAStatus SecureElement::init(const std::shared_ptr<ISecureElementCallback>& clientCallback) {

    ALOGD("SecureElement:%s start", __func__);
    if (clientCallback == nullptr) {
        ALOGE("SecureElement:%s clientCallback == nullptr", __func__);
        return ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    } else {
        internalClientCallback = clientCallback;
    }

    if (initializeSE() != EXIT_SUCCESS) {
        ALOGE("SecureElement:%s initializeSE Failed", __func__);
        clientCallback->onStateChange(false, "SE Initialized failed");
    } else {
        ALOGD("SecureElement:%s initializeSE Success", __func__);
        clientCallback->onStateChange(true, "SE Initialized");
    }

    ALOGD("SecureElement:%s end", __func__);

    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::getAtr(std::vector<uint8_t>* aidl_return) {
    std::vector<uint8_t> response;
    response.resize(atr_size);
    memcpy(&response[0], atr, atr_size);
    *aidl_return = response;
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::isCardPresent(bool* aidl_return) {
    *aidl_return = true;
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::transmit(const std::vector<uint8_t>& data, std::vector<uint8_t>* aidl_return) {

    uint8_t *apdu;
    uint8_t *resp;
    int apdu_len = data.size();
    int resp_len = 0;
    ScopedAStatus status = ScopedAStatus::fromServiceSpecificError(FAILED);

    apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
    resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));

    std::vector<uint8_t> result;

    if (checkSeUp && nbrOpenChannel != 0) {
        if (apdu != NULL) {
            memcpy(apdu, data.data(), data.size());
            dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);
            resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        }

        if (resp_len < 0) {
            ALOGE("SecureElement:%s: transmit failed", __func__);
            if (deinitializeSE() != SUCCESS) {
                ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
            }
        } else {
            dump_bytes("RESP: ", ':', resp, resp_len, stdout);
            result.resize(resp_len);
            memcpy(&result[0], resp, resp_len);
            status = ScopedAStatus::ok();
        }
    } else {
        ALOGE("SecureElement:%s: transmit failed! No channel is open", __func__);
        status = ScopedAStatus::fromServiceSpecificError(CHANNEL_NOT_AVAILABLE);
    }
    aidl_return->assign(result.begin(), result.end());
    if(apdu) free(apdu);
    if(resp) free(resp);
    return status;
}

ScopedAStatus SecureElement::openLogicalChannel(const std::vector<uint8_t>& aid, int8_t p2, ::aidl::android::hardware::secure_element::LogicalChannelResponse* aidl_return) {
    ALOGD("SecureElement:%s start", __func__);

    std::vector<uint8_t> resApduBuff;
    size_t ext_channelNumber = 0xff;
    size_t channelNumber = 0xff;
    memset(&resApduBuff, 0x00, sizeof(resApduBuff));

    if (internalClientCallback == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    if (!checkSeUp) {
        if (initializeSE() != EXIT_SUCCESS) {
            ALOGE("SecureElement:%s: Failed to re-initialise the eSE HAL", __func__);
            internalClientCallback->onStateChange(false, "SE Initialized failed");
            return ScopedAStatus::fromServiceSpecificError(IOERROR);
        }
    }

    if (aid.size() > MAX_AID_LEN) {
        ALOGE("SecureElement:%s: Bad AID size", __func__);
        return ScopedAStatus::fromServiceSpecificError(FAILED);
    }

    int mSecureElementStatus = IOERROR;

    uint8_t *apdu; //65536
    int apdu_len = 0;
    uint8_t *resp;
    int resp_len = 0;
    uint8_t index = 0;
    int getResponseOffset = 0;

    apdu_len = 5;
    apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
    resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));

    if (apdu != NULL && resp!=NULL) {
        index = 0;
        apdu[index++] = 0x00;
        apdu[index++] = 0x70;
        apdu[index++] = 0x00;
        apdu[index++] = 0x00;
        apdu[index++] = 0x01;

        dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);

        resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        ALOGD("SecureElement:%s Manage channel resp_len = %d", __func__,resp_len);
    }

    if (resp_len >= 0)
        dump_bytes("RESP: ", ':', resp, resp_len, stdout);


    if (resp_len < 0) {
        if (deinitializeSE() != SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
        mSecureElementStatus = IOERROR;
        ALOGD("SecureElement:%s Free memory after manage channel after ERROR", __func__);
        if(apdu) free(apdu);
        if(resp) free(resp);
        return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    } else if (resp[resp_len - 2] == 0x90 && resp[resp_len - 1] == 0x00) {
        channelNumber = resp[0];
        if(channelNumber > 0x03) {
          ext_channelNumber = 0x40 + channelNumber - 0x04;
        } else {
            ext_channelNumber = channelNumber;
        }
        nbrOpenChannel++;
        mSecureElementStatus = SUCCESS;
    } else {
        if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = CHANNEL_NOT_AVAILABLE;
        }else if (resp[resp_len - 2] == 0x68 && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = CHANNEL_NOT_AVAILABLE;
        } else {
            mSecureElementStatus = IOERROR;
        }
        ALOGD("SecureElement:%s Free memory after manage channel after ERROR", __func__);
        if(apdu) free(apdu);
        if(resp) free(resp);
        return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    }

    if(apdu) free(apdu);
    if(resp) free(resp);
    ALOGD("SecureElement:%s Free memory after manage channel", __func__);
    ALOGD("SecureElement:%s mSecureElementStatus = %d", __func__, (int)mSecureElementStatus);

    /*Start Sending select command after Manage Channel is successful.*/
    ALOGD("SecureElement:%s Sending selectApdu", __func__);

    mSecureElementStatus = IOERROR;

    apdu_len = (int32_t)(6 + aid.size());
    resp_len = 0;
    apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
    resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));

    if (apdu != NULL && resp!=NULL) {
        index = 0;
        apdu[index++] = ext_channelNumber;
        apdu[index++] = 0xA4;
        apdu[index++] = 0x04;
        apdu[index++] = p2;
        apdu[index++] = aid.size();
        memcpy(&apdu[index], aid.data(), aid.size());
        index += aid.size();
        apdu[index] = 0x00;

send_logical:
        dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);
        resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        ALOGD("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);
    }

    if (resp_len < 0) {
        ALOGE("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);
        if (deinitializeSE() != SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
        mSecureElementStatus = IOERROR;
    } else {
        dump_bytes("RESP: ", ':', resp, resp_len, stdout);

        if (resp[resp_len - 2] == 0x90 || resp[resp_len - 2] == 0x62 || resp[resp_len - 2] == 0x63) {
            resApduBuff.resize(getResponseOffset + resp_len);
            memcpy(&resApduBuff[getResponseOffset], resp, resp_len);
            mSecureElementStatus = SUCCESS;
        }
        else if (resp[resp_len - 2] == 0x61) {
            resApduBuff.resize(getResponseOffset + resp_len - 2);
            memcpy(&resApduBuff[getResponseOffset], resp, resp_len - 2);
            getResponseOffset += (resp_len - 2);
            getResponse[4] = resp[resp_len - 1];
            getResponse[0] = apdu[0];
            dump_bytes("getResponse CMD: ", ':', getResponse, 5, stdout);
            free(apdu);
            apdu_len = 5;
            apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
            memset(resp, 0, resp_len);
            memcpy(apdu, getResponse, apdu_len);
            apdu[0] = ext_channelNumber;
            goto send_logical;
        }
        else if (resp[resp_len - 2] == 0x6C) {
            resApduBuff.resize(getResponseOffset + resp_len - 2);
            memcpy(&resApduBuff[getResponseOffset], resp, resp_len - 2);
            getResponseOffset += (resp_len - 2);
            apdu[4] = resp[resp_len - 1];
            dump_bytes("case2 getResponse CMD: ", ':', apdu, 5, stdout);
            memset(resp, 0, resp_len);
            goto send_logical;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x80) {
            mSecureElementStatus = IOERROR;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = IOERROR;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x82) {
            mSecureElementStatus = NO_SUCH_ELEMENT_ERROR;
        }
        else if (resp[resp_len - 2] == 0x69 && resp[resp_len - 1] == 0x85) {
            mSecureElementStatus = NO_SUCH_ELEMENT_ERROR;
        }
        else if (resp[resp_len - 2] == 0x69 && resp[resp_len - 1] == 0x99) {
            mSecureElementStatus = NO_SUCH_ELEMENT_ERROR;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x86) {
            mSecureElementStatus = UNSUPPORTED_OPERATION;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x87) {
            mSecureElementStatus = UNSUPPORTED_OPERATION;
        }
    }

    /*Check if SELECT command failed, close oppened channel*/
    if (mSecureElementStatus != SUCCESS) {
        closeChannel(channelNumber);
    }

    ALOGD("SecureElement:%s mSecureElementStatus = %d", __func__, (int)mSecureElementStatus);
    *aidl_return = LogicalChannelResponse{
        .channelNumber = static_cast<int8_t>(channelNumber),
        .selectResponse = resApduBuff,
    };

    ALOGD("SecureElement:%s Free memory after selectApdu", __func__);
    if(apdu) free(apdu);
    if(resp) free(resp);

    if(mSecureElementStatus != SUCCESS) return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    else return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2, std::vector<uint8_t>* aidl_return) {
    std::vector<uint8_t> result;

    int mSecureElementStatus = IOERROR;

    uint8_t *apdu; //65536
    int apdu_len = 0;
    uint8_t *resp;
    int resp_len = 0;
    int getResponseOffset = 0;
    uint8_t index = 0;

    if (internalClientCallback == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (isBasicChannelOpen) {
        ALOGE("SecureElement:%s: Basic Channel already open", __func__);
        return ScopedAStatus::fromServiceSpecificError(CHANNEL_NOT_AVAILABLE);
    }

    if (!checkSeUp) {
        if (initializeSE() != EXIT_SUCCESS) {
            ALOGE("SecureElement:%s: Failed to re-initialise the eSE HAL", __func__);
            internalClientCallback->onStateChange(false, "SE Initialized failed");
            return ScopedAStatus::fromServiceSpecificError(IOERROR);
        }
    }

    if (aid.size() > MAX_AID_LEN) {
        ALOGE("SecureElement:%s: Bad AID size", __func__);
        return ScopedAStatus::fromServiceSpecificError(FAILED);
    }

    apdu_len = (int32_t)(6 + aid.size());
    resp_len = 0;
    apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
    resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));


    if (apdu != NULL) {
        index = 0;
        apdu[index++] = 0x00;
        apdu[index++] = 0xA4;
        apdu[index++] = 0x04;
        apdu[index++] = p2;
        apdu[index++] = aid.size();
        memcpy(&apdu[index], aid.data(), aid.size());
        index += aid.size();
        apdu[index] = 0x00;

send_basic:
        dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);
        resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        ALOGD("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);
    }

    if (resp_len < 0) {
        if (deinitializeSE() != SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
        mSecureElementStatus = IOERROR;
    } else {
        dump_bytes("RESP: ", ':', resp, resp_len, stdout);

        if (resp[resp_len - 2] == 0x90 || resp[resp_len - 2] == 0x62 || resp[resp_len - 2] == 0x63) {
            result.resize(getResponseOffset + resp_len);
            memcpy(&result[getResponseOffset], resp, resp_len);

            isBasicChannelOpen = true;
            nbrOpenChannel++;
            mSecureElementStatus = SUCCESS;
        }
        else if (resp[resp_len - 2] == 0x61) {
            result.resize(getResponseOffset + resp_len - 2);
            memcpy(&result[getResponseOffset], resp, resp_len - 2);
            getResponseOffset += (resp_len - 2);
            getResponse[4] = resp[resp_len - 1];
            getResponse[0] = apdu[0];
            dump_bytes("getResponse CMD: ", ':', getResponse, 5, stdout);
            free(apdu);
            apdu_len = 5;
            apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
            memset(resp, 0, resp_len);
            memcpy(apdu, getResponse, apdu_len);
            goto send_basic;
        }
        else if (resp[resp_len - 2] == 0x6C) {
            result.resize(getResponseOffset + resp_len - 2);
            memcpy(&result[getResponseOffset], resp, resp_len - 2);
            getResponseOffset += (resp_len - 2);
            apdu[4] = resp[resp_len - 1];
            dump_bytes("case2 getResponse CMD: ", ':', apdu, 5, stdout);
            apdu_len = 5;
            memset(resp, 0, resp_len);
            goto send_basic;
        }
        else if (resp[resp_len - 2] == 0x68 && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = CHANNEL_NOT_AVAILABLE;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = CHANNEL_NOT_AVAILABLE;
        }
    }

    if ((mSecureElementStatus != SUCCESS) && isBasicChannelOpen) {
      closeChannel(BASIC_CHANNEL);
    }

    ALOGD("SecureElement:%s mSecureElementStatus = %d", __func__, (int)mSecureElementStatus);
    *aidl_return = result;

    ALOGD("SecureElement:%s Free memory after openBasicChannel", __func__);
    if(apdu) free(apdu);
    if(resp) free(resp);
    if(mSecureElementStatus != SUCCESS) return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    else return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::closeChannel(int8_t channelNumber) {
    ALOGD("SecureElement:%s start", __func__);
    int mSecureElementStatus = FAILED;

    uint8_t *apdu; //65536
    int apdu_len = 10;
    uint8_t *resp;
    int resp_len = 0;

    if (!checkSeUp) {
        ALOGE("SecureElement:%s cannot closeChannel, HAL is deinitialized", __func__);
        mSecureElementStatus = FAILED;
        ALOGD("SecureElement:%s end", __func__);
        return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    }

    if (channelNumber < 0) {
        ALOGE("SecureElement:%s Channel not supported", __func__);
        mSecureElementStatus = FAILED;
    } else if (channelNumber == 0) {
        isBasicChannelOpen = false;
        mSecureElementStatus = SUCCESS;
        nbrOpenChannel--;
    } else {
        apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
        resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));

        if (apdu != NULL) {
            uint8_t index = 0;
            if(channelNumber > 0x03) {
              apdu[index++] = 0x40 + channelNumber - 0x04;
            } else {
                apdu[index++] = channelNumber;
            }
            apdu[index++] = 0x70;
            apdu[index++] = 0x80;
            apdu[index++] = channelNumber;
            apdu[index++] = 0x00;
            apdu_len = index;

            dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);
            resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
            if (resp_len >= 0)
                dump_bytes("RESP: ", ':', resp, resp_len, stdout);
        }
        if (resp_len < 0) {
            mSecureElementStatus = FAILED;
            if (deinitializeSE() != SUCCESS) {
                ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
            }
        } else if ((resp[resp_len - 2] == 0x90) && (resp[resp_len - 1] == 0x00)) {
            mSecureElementStatus = SUCCESS;
            nbrOpenChannel--;
        } else {
            mSecureElementStatus = FAILED;
        }
        if(apdu) free(apdu);
        if(resp) free(resp);
    }

    if (nbrOpenChannel == 0 && isBasicChannelOpen == false) {
        ALOGD("SecureElement:%s All Channels are closed", __func__);
        if (deinitializeSE() != SUCCESS) {
            ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
    }
    ALOGD("SecureElement:%s end", __func__);
    if(mSecureElementStatus != SUCCESS) return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    else return ScopedAStatus::ok();
}

void
SecureElement::dump_bytes(const char *pf, char sep, const uint8_t *p, int n, FILE *out)
{
    const uint8_t *s = p;
    char *msg;
    int len = 0;
    int input_len = n;

    if (!debug_log_enabled) return;

    msg = (char*) malloc ( (pf ? strlen(pf) : 0) + input_len * 3 + 1);
    if(!msg) {
        errno = ENOMEM;
        return;
    }

    if (pf) {
        len += sprintf(msg , "%s" , pf);
    }
    while (input_len--) {
        len += sprintf(msg + len, "%02X" , *s++);
        if (input_len && sep) {
            len += sprintf(msg + len, ":");
        }
    }
    sprintf(msg + len, "\n");
    ALOGD("SecureElement:%s ==> size = %d data = %s", __func__, n, msg);

    if(msg) free(msg);
}

int
SecureElement::toint(char c)
{
    if ((c >= '0') && (c <= '9'))
        return c - '0';

    if ((c >= 'A') && (c <= 'F'))
        return c - 'A' + 10;

    if ((c >= 'a') && (c <= 'f'))
        return c - 'a' + 10;

    return 0;
}

int
SecureElement::run_apdu(struct se_gto_ctx *ctx, const uint8_t *apdu, uint8_t *resp, int n, int verbose)
{
    int sw;

    if (verbose)
        dump_bytes("APDU: ", ':', apdu, n, stdout);


    n = se_gto_apdu_transmit(ctx, apdu, n, resp, sizeof(resp));
    if (n < 0) {
        ALOGE("SecureElement:%s FAILED: APDU transmit (%s).\n\n", __func__, strerror(errno));
        return -2;
    } else if (n < 2) {
        dump_bytes("RESP: ", ':', resp, n, stdout);
        ALOGE("SecureElement:%s FAILED: not enough data to have a status word.\n", __func__);
        return -2;
    }

    if (verbose) {
        sw = (resp[n - 2] << 8) | resp[n - 1];
        printf("%d bytes, SW=0x%04x\n", n - 2, sw);
        if (n > 2)
            dump_bytes("RESP: ", ':', resp, n - 2, stdout);
    }
    return 0;
}

int
SecureElement::parseConfigFile(FILE *f, int verbose)
{
    static char    buf[65536 * 2 + 2];

    int line;
    char * pch;

    line = 0;
    while (feof(f) == 0) {
        char *s;

        s = fgets(buf, sizeof buf, f);
        if (s == NULL)
            break;
        if (s[0] == '#') {
            continue;
        }

        pch = strtok(s," =;");
        if (strcmp("GTO_DEV", pch) == 0) {
            pch = strtok (NULL, " =;");
            ALOGD("SecureElement:%s Defined node : %s", __func__, pch);
            if (strlen(pch) > 0 && strcmp("\n", pch) != 0 && strcmp("\0", pch) != 0 ) {
                se_gto_set_gtodev(ctx, pch);
            }
        } else if (strcmp("GTO_DEBUG", pch) == 0) {
            pch = strtok(NULL, " =;");
            ALOGD("SecureElement:%s Log state : %s", __func__, pch);
            if (strlen(pch) > 0 && strcmp("\n", pch) != 0 && strcmp("\0", pch) != 0 ) {
                if (strcmp(pch, "enable") == 0) {
                    debug_log_enabled = true;
                    se_gto_set_log_level(ctx, 4);
                } else {
                    debug_log_enabled = false;
                    se_gto_set_log_level(ctx, 3);
                }
            }
        }
    }
    return 0;
}

int
SecureElement::openConfigFile(int verbose)
{
    int   r;
    FILE *f;


    /* filename is not NULL */
    ALOGD("SecureElement:%s Open Config file : %s", __func__, config_filename);
    f = fopen(config_filename, "r");
    if (f) {
        r = parseConfigFile(f, verbose);
        if (r == -1) {
            perror(config_filename);
            ALOGE("SecureElement:%s Error parse %s Failed", __func__, config_filename);
        }
        if (fclose(f) != 0) {
            r = -1;
            ALOGE("SecureElement:%s Error close %s Failed", __func__, config_filename);
        }
    } else {
        r = -1;
        ALOGE("SecureElement:%s Error open %s Failed", __func__, config_filename);
    }
    return r;
}

int SecureElement::deinitializeSE() {
    int mSecureElementStatus = FAILED;

    ALOGD("SecureElement:%s start", __func__);

    if(checkSeUp){
        if (se_gto_close(ctx) < 0) {
            mSecureElementStatus = FAILED;
            internalClientCallback->onStateChange(false, "SE Initialized failed");
        } else {
            ctx = NULL;
            mSecureElementStatus = SUCCESS;
            isBasicChannelOpen = false;
            nbrOpenChannel = 0;
        }
        checkSeUp = false;
    }else{
        ALOGD("SecureElement:%s No need to deinitialize SE", __func__);
        mSecureElementStatus = SUCCESS;
    }

    ALOGD("SecureElement:%s end", __func__);
    return mSecureElementStatus;
}

ScopedAStatus SecureElement::reset() {

    int status = FAILED;
    ALOGD("SecureElement:%s start", __func__);

    if (deinitializeSE() != SUCCESS) {
        ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
    }

    internalClientCallback->onStateChange(false, "reset the SE");

    if(initializeSE() == EXIT_SUCCESS) {
        status = SUCCESS;
        internalClientCallback->onStateChange(true, "SE Initialized");
    }

    ALOGD("SecureElement:%s end", __func__);

    if(status != SUCCESS) return ScopedAStatus::fromServiceSpecificError(status);
    else return ScopedAStatus::ok();
}

}
