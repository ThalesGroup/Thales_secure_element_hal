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

#include "se-gto/libse-gto.h"
#include "SecureElement.h"


//#include "profile.h"
//#include "settings.h"

namespace android {
namespace hardware {
namespace secure_element {
namespace V1_1 {
namespace implementation {

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 0x04
#endif

#ifndef BASIC_CHANNEL
#define BASIC_CHANNEL 0x00
#endif

#ifndef LOG_HAL_LEVEL
#define LOG_HAL_LEVEL 4
#endif

static struct se_gto_ctx *ctx;

SecureElement::SecureElement(){
    nbrOpenChannel = 0;
    ctx = NULL;
}

void SecureElement::resetSE(){
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
}

sp<V1_0::ISecureElementHalCallback> SecureElement::internalClientCallback = nullptr;
sp<V1_1::ISecureElementHalCallback> SecureElement::internalClientCallback_v1_1 = nullptr;
int SecureElement::initializeSE() {

    //struct settings *settings;
    int n;

    ALOGD("SecureElement:%s start", __func__);

    if (se_gto_new(&ctx) < 0) {
        ALOGE("SecureElement:%s se_gto_new FATAL:%s", __func__,strerror(errno));

        return EXIT_FAILURE;
    }
    //settings = default_settings(ctx);
    se_gto_set_log_level(ctx, 4);
    
    openConfigFile(1);

    if (se_gto_open(ctx) < 0) {
        ALOGE("SecureElement:%s se_gto_open FATAL:%s", __func__,strerror(errno));
        return EXIT_FAILURE;
    }

    resetSE();

    checkSeUp = true;
    turnOffSE = false;


    if(internalClientCallback_v1_1 != nullptr) internalClientCallback_v1_1->onStateChange_1_1(true, "SE Initialized");
    else internalClientCallback->onStateChange(true);

    turnOffSE = true;

    ALOGD("SecureElement:%s end", __func__);
    return EXIT_SUCCESS;
}

Return<void> SecureElement::init(const sp<::android::hardware::secure_element::V1_0::ISecureElementHalCallback>& clientCallback) {

    ALOGD("SecureElement:%s start", __func__);
    if (clientCallback == nullptr) {
        ALOGD("SecureElement:%s clientCallback == nullptr", __func__);
        return Void();
    } else {
        internalClientCallback = clientCallback;
        internalClientCallback_v1_1 = nullptr;
        if (!internalClientCallback->linkToDeath(this, 0)) {
            ALOGE("SecureElement:%s: linkToDeath Failed", __func__);
        }
    }

    if (initializeSE() != EXIT_SUCCESS) {
        ALOGE("SecureElement:%s initializeSE Failed", __func__);
        clientCallback->onStateChange(false);
        return Void();
    }

    if (deinitializeSE() != SecureElementStatus::SUCCESS) {
        ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
    }
    ALOGD("SecureElement:%s end", __func__);

    return Void();
}

Return<void> SecureElement::init_1_1(const sp<::android::hardware::secure_element::V1_1::ISecureElementHalCallback>& clientCallback) {

    ALOGD("SecureElement:%s start", __func__);
    if (clientCallback == nullptr) {
        ALOGD("SecureElement:%s clientCallback == nullptr", __func__);
        return Void();
    } else {
        internalClientCallback = nullptr;
        internalClientCallback_v1_1 = clientCallback;
        if (!internalClientCallback_v1_1->linkToDeath(this, 0)) {
            ALOGE("SecureElement:%s: linkToDeath Failed", __func__);
        }
    }

    if (initializeSE() != EXIT_SUCCESS) {
        ALOGE("SecureElement:%s initializeSE Failed", __func__);
        clientCallback->onStateChange_1_1(false, "initializeSE Failed");
        return Void();
    }

    if (deinitializeSE() != SecureElementStatus::SUCCESS) {
        ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
    }
    ALOGD("SecureElement:%s end", __func__);

    return Void();
}

Return<void> SecureElement::getAtr(getAtr_cb _hidl_cb) {
    hidl_vec<uint8_t> response;
    response.resize(atr_size);
    memcpy(&response[0], atr, atr_size);
    _hidl_cb(response);
    return Void();
}

Return<bool> SecureElement::isCardPresent() {
    return true;
}

Return<void> SecureElement::transmit(const hidl_vec<uint8_t>& data, transmit_cb _hidl_cb) {

    uint8_t *apdu;
    uint8_t *resp;
    int status = 0 ;
    int apdu_len = data.size();
    int resp_len = 0;

    apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
    resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));

    hidl_vec<uint8_t> result;

    if (checkSeUp && nbrOpenChannel != 0) {
        if (apdu != NULL) {
            memcpy(apdu, data.data(), data.size());
            dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);
            resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        }

        if (resp_len < 0) {
            ALOGE("SecureElement:%s: transmit failed", __func__);
            if (deinitializeSE() != SecureElementStatus::SUCCESS) {
                ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
            }
        } else {
            dump_bytes("RESP: ", ':', resp, resp_len, stdout);
            result.resize(resp_len);
            memcpy(&result[0], resp, resp_len);
        }
    } else {
        ALOGE("SecureElement:%s: transmit failed! No channel is open", __func__);
    }
    _hidl_cb(result);
    if(apdu) free(apdu);
    if(resp) free(resp);
    return Void();
}

Return<void> SecureElement::openLogicalChannel(const hidl_vec<uint8_t>& aid, uint8_t p2, openLogicalChannel_cb _hidl_cb) {
    ALOGD("SecureElement:%s start", __func__);

    LogicalChannelResponse resApduBuff;
    resApduBuff.channelNumber = 0xff;
    memset(&resApduBuff, 0x00, sizeof(resApduBuff));

    if (!checkSeUp) {
        if (initializeSE() != EXIT_SUCCESS) {
            ALOGE("SecureElement:%s: Failed to re-initialise the eSE HAL", __func__);
            _hidl_cb(resApduBuff, SecureElementStatus::IOERROR);
            return Void();
        }
    }

    SecureElementStatus mSecureElementStatus = SecureElementStatus::IOERROR;

    uint8_t *apdu; //65536
    int apdu_len = 0;
    uint8_t *resp;
    int resp_len = 0;
    uint8_t index = 0;

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
        if (deinitializeSE() != SecureElementStatus::SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
        mSecureElementStatus = SecureElementStatus::IOERROR;
        ALOGD("SecureElement:%s Free memory after manage channel after ERROR", __func__);
        if(apdu) free(apdu);
        if(resp) free(resp);
        _hidl_cb(resApduBuff, mSecureElementStatus);
        return Void();
    } else if (resp[resp_len - 2] == 0x90 && resp[resp_len - 1] == 0x00) {
        resApduBuff.channelNumber = resp[0];
        nbrOpenChannel++;
        mSecureElementStatus = SecureElementStatus::SUCCESS;
    } else {
        if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = SecureElementStatus::CHANNEL_NOT_AVAILABLE;
        }else if (resp[resp_len - 2] == 0x68 && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = SecureElementStatus::CHANNEL_NOT_AVAILABLE;
        } else {
            mSecureElementStatus = SecureElementStatus::IOERROR;
        }
        ALOGD("SecureElement:%s Free memory after manage channel after ERROR", __func__);
        if(apdu) free(apdu);
        if(resp) free(resp);
        _hidl_cb(resApduBuff, mSecureElementStatus);
        return Void();
    }

    if(apdu) free(apdu);
    if(resp) free(resp);
    ALOGD("SecureElement:%s Free memory after manage channel", __func__);
    ALOGD("SecureElement:%s mSecureElementStatus = %d", __func__, (int)mSecureElementStatus);

    /*Start Sending select command after Manage Channel is successful.*/
    ALOGD("SecureElement:%s Sending selectApdu", __func__);

    mSecureElementStatus = SecureElementStatus::IOERROR;

    apdu_len = (int32_t)(5 + aid.size());
    resp_len = 0;
    apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
    resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));

    if (apdu != NULL && resp!=NULL) {
        index = 0;
        apdu[index++] = resApduBuff.channelNumber;
        apdu[index++] = 0xA4;
        apdu[index++] = 0x04;
        apdu[index++] = p2;
        apdu[index++] = aid.size();
        memcpy(&apdu[index], aid.data(), aid.size());
        dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);

        resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        ALOGD("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);
    }

    if (resp_len < 0) {
        ALOGE("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);
        if (deinitializeSE() != SecureElementStatus::SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
        mSecureElementStatus = SecureElementStatus::IOERROR;
    } else {
        dump_bytes("RESP: ", ':', resp, resp_len, stdout);

        if (resp[resp_len - 2] == 0x90 && resp[resp_len - 1] == 0x00) {
            resApduBuff.selectResponse.resize(resp_len);
            memcpy(&resApduBuff.selectResponse[0], resp, resp_len);
            mSecureElementStatus = SecureElementStatus::SUCCESS;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x80) {
            mSecureElementStatus = SecureElementStatus::IOERROR;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = SecureElementStatus::IOERROR;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x82) {
            mSecureElementStatus = SecureElementStatus::NO_SUCH_ELEMENT_ERROR;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x86) {
            mSecureElementStatus = SecureElementStatus::UNSUPPORTED_OPERATION;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x87) {
            mSecureElementStatus = SecureElementStatus::UNSUPPORTED_OPERATION;
        }
    }

    /*Check if SELECT command failed, close oppened channel*/
    if (mSecureElementStatus != SecureElementStatus::SUCCESS) {
        closeChannel(resApduBuff.channelNumber);
    }

    ALOGD("SecureElement:%s mSecureElementStatus = %d", __func__, (int)mSecureElementStatus);
    _hidl_cb(resApduBuff, mSecureElementStatus);

    ALOGD("SecureElement:%s Free memory after selectApdu", __func__);
    if(apdu) free(apdu);
    if(resp) free(resp);
    return Void();
}

Return<void> SecureElement::openBasicChannel(const hidl_vec<uint8_t>& aid, uint8_t p2, openBasicChannel_cb _hidl_cb) {
    hidl_vec<uint8_t> result;

    SecureElementStatus mSecureElementStatus = SecureElementStatus::IOERROR;

    uint8_t *apdu; //65536
    int apdu_len = 0;
    uint8_t *resp;
    int resp_len = 0;

    if (!checkSeUp) {
        if (initializeSE() != EXIT_SUCCESS) {
            ALOGE("SecureElement:%s: Failed to re-initialise the eSE HAL", __func__);
            _hidl_cb(result, SecureElementStatus::IOERROR);
            return Void();
        }
    }


    apdu_len = (int32_t)(5 + aid.size());
    resp_len = 0;
    apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
    resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));


    if (apdu != NULL) {
        uint8_t index = 0;
        apdu[index++] = 0x00;
        apdu[index++] = 0xA4;
        apdu[index++] = 0x04;
        apdu[index++] = p2;
        apdu[index++] = aid.size();
        memcpy(&apdu[index], aid.data(), aid.size());
        dump_bytes("CMD: ", ':', apdu, apdu_len, stdout);

        resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        ALOGD("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);
    }

    if (resp_len < 0) {
        if (deinitializeSE() != SecureElementStatus::SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
        mSecureElementStatus = SecureElementStatus::IOERROR;
    } else {
        dump_bytes("RESP: ", ':', resp, resp_len, stdout);

        if ((resp[resp_len - 2] == 0x90) && (resp[resp_len - 1] == 0x00)) {
            result.resize(resp_len);
            memcpy(&result[0], resp, resp_len);

            isBasicChannelOpen = true;
            nbrOpenChannel++;
            mSecureElementStatus = SecureElementStatus::SUCCESS;
        }
        else if (resp[resp_len - 2] == 0x68 && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = SecureElementStatus::CHANNEL_NOT_AVAILABLE;
        }
        else if (resp[resp_len - 2] == 0x6A && resp[resp_len - 1] == 0x81) {
            mSecureElementStatus = SecureElementStatus::CHANNEL_NOT_AVAILABLE;
        }
    }

    if ((mSecureElementStatus != SecureElementStatus::SUCCESS) && isBasicChannelOpen) {
      closeChannel(BASIC_CHANNEL);
    }

    ALOGD("SecureElement:%s mSecureElementStatus = %d", __func__, (int)mSecureElementStatus);
    _hidl_cb(result, mSecureElementStatus);

    ALOGD("SecureElement:%s Free memory after openBasicChannel", __func__);
    if(apdu) free(apdu);
    if(resp) free(resp);
    return Void();
}

Return<::android::hardware::secure_element::V1_0::SecureElementStatus> SecureElement::closeChannel(uint8_t channelNumber) {
    ALOGD("SecureElement:%s start", __func__);
    SecureElementStatus mSecureElementStatus = SecureElementStatus::FAILED;

    uint8_t *apdu; //65536
    int apdu_len = 10;
    uint8_t *resp;
    int resp_len = 0;

    if ((channelNumber < 0) || (channelNumber >= MAX_CHANNELS)) {
        ALOGE("SecureElement:%s Channel not supported", __func__);
        mSecureElementStatus = SecureElementStatus::FAILED;
    } else if (channelNumber == 0) {
        isBasicChannelOpen = false;
        mSecureElementStatus = SecureElementStatus::SUCCESS;
        nbrOpenChannel--;
    } else {
        apdu = (uint8_t*)malloc(apdu_len * sizeof(uint8_t));
        resp = (uint8_t*)malloc(65536 * sizeof(uint8_t));

        if (apdu != NULL) {
            uint8_t index = 0;

            apdu[index++] = channelNumber;
            apdu[index++] = 0x70;
            apdu[index++] = 0x80;
            apdu[index++] = channelNumber;
            apdu[index++] = 0x00;
            apdu_len = index;

            resp_len = se_gto_apdu_transmit(ctx, apdu, apdu_len, resp, 65536);
        }
        if (resp_len < 0) {
            mSecureElementStatus = SecureElementStatus::FAILED;
            if (deinitializeSE() != SecureElementStatus::SUCCESS) {
                ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
            }
        } else if ((resp[resp_len - 2] == 0x90) && (resp[resp_len - 1] == 0x00)) {
            mSecureElementStatus = SecureElementStatus::SUCCESS;
            nbrOpenChannel--;
        } else {
            mSecureElementStatus = SecureElementStatus::FAILED;
        }
        if(apdu) free(apdu);
        if(resp) free(resp);
    }

    if (nbrOpenChannel == 0 && isBasicChannelOpen == false) {
        ALOGD("SecureElement:%s All Channels are closed", __func__);
        if (deinitializeSE() != SecureElementStatus::SUCCESS) {
            ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
    }
    ALOGD("SecureElement:%s end", __func__);
    return mSecureElementStatus;
}

void
SecureElement::dump_bytes(const char *pf, char sep, const uint8_t *p, int n, FILE *out)
{
    const uint8_t *s = p;
    char *msg;
    int len = 0;
    int input_len = n;

    msg = (char*) malloc ( (pf ? strlen(pf) : 0) + input_len * 3 + 1);
    if(!msg) {
        errno = ENOMEM;
        return;
    }

    if (pf) {
        len += sprintf(msg , "%s" , pf);
        //len = len + 8;
    }
    while (input_len--) {
        len += sprintf(msg + len, "%02X" , *s++);
        //len = len + 2;
        if (input_len && sep) {
            len += sprintf(msg + len, ":");
            //len++;
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
            /*if (verbose)
                fputs(buf, stdout);*/
            continue;
        }

        pch = strtok (s," =;");
        if (strcmp("GTO_DEV", pch) == 0){
            pch = strtok (NULL, " =;");
            ALOGD("SecureElement:%s Defined node : %s", __func__, pch);
            if (strlen (pch) > 0 && strcmp("\n",pch) != 0 && strcmp("\0",pch) != 0 ) se_gto_set_gtodev(ctx, pch);
        }
        
    }
    return 0;
}

int
SecureElement::openConfigFile(int verbose)
{
    int   r;
    FILE *f;
    char filename[] = "/vendor/etc/libse-gto-hal.conf";

    /* filename is not NULL */
    ALOGD("SecureElement:%s Open Config file : %s", __func__, filename);
    f = fopen(filename, "r");
    if (f) {
        r = parseConfigFile(f, verbose);
        if (r == -1)
            perror(filename);
            ALOGE("SecureElement:%s Error parse %s Failed", __func__, filename);
        if (fclose(f) != 0)
            r = -1;
            ALOGE("SecureElement:%s Error close %s Failed", __func__, filename);
    } else {
        r = -1;
        ALOGE("SecureElement:%s Error open %s Failed", __func__, filename);
    }
    return r;
}

void SecureElement::serviceDied(uint64_t, const wp<IBase>&) {
  ALOGE("SecureElement:%s SecureElement serviceDied", __func__);
  SecureElementStatus mSecureElementStatus = deinitializeSE();
  if (mSecureElementStatus != SecureElementStatus::SUCCESS) {
    ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
  }
  if (internalClientCallback != nullptr) {
    internalClientCallback->unlinkToDeath(this);
    internalClientCallback = nullptr;
  }
  if (internalClientCallback_v1_1 != nullptr) {
    internalClientCallback_v1_1->unlinkToDeath(this);
    internalClientCallback_v1_1 = nullptr;
  }
}

Return<::android::hardware::secure_element::V1_0::SecureElementStatus>
SecureElement::deinitializeSE() {
    SecureElementStatus mSecureElementStatus = SecureElementStatus::FAILED;

    ALOGD("SecureElement:%s start", __func__);

    if(turnOffSE){
        if (se_gto_close(ctx) < 0) {
            mSecureElementStatus = SecureElementStatus::FAILED;
        } else {
            ctx = NULL;
            mSecureElementStatus = SecureElementStatus::SUCCESS;
            isBasicChannelOpen = false;
            nbrOpenChannel = 0;
        }
        checkSeUp = false;
        turnOffSE = false;
    }else{
        ALOGD("SecureElement:%s No need to deinitialize SE", __func__);
        mSecureElementStatus = SecureElementStatus::SUCCESS;
    }

    ALOGD("SecureElement:%s end", __func__);
    return mSecureElementStatus;
}

// Methods from ::android::hidl::base::V1_0::IBase follow.

//ISecureElement* HIDL_FETCH_ISecureElement(const char* /* name */) {
    //return new SecureElement();
//}
//
}  // namespace implementation
}  // namespace V1_0
}  // namespace secure_element
}  // namespace hardware
}  // namespace android
