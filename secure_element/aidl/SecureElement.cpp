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
#include <vector>
#include <variant>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <limits.h>
#include <log/log.h>
#include <android-base/properties.h>
#include <android/binder_ibinder.h>
#include <android/binder_ibinder_platform.h>
#include <android-base/chrono_utils.h>
#include <android-base/parseint.h>
#include <private/android_filesystem_config.h>
#include <dlfcn.h>
#include <chrono>
#include <thread>

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

#ifndef MAX_APDU_SIZE
#define MAX_APDU_SIZE 65536
#endif

uint8_t _getResponse[5] = {0x00, 0xC0, 0x00, 0x00, 0x00};
uint8_t _openChannel[5] = {0x00, 0x70, 0x00, 0x00, 0x01};
uint8_t _closeChannel[5] = {0x00, 0x70, 0x80, 0x00, 0x00};

bool debug_log_enabled = false;

const std::string propertySEHalLock_ = "persist.vendor.sehal.lock";
const std::string propertySEHalLockDuration_ = "persist.vendor.sehal.lock_duration";
const std::string propertySEHalLockUID_ = "persist.vendor.sehal.lock_uid";

// Using Mutex to protect SecureElement API call & to synchronize notification thread execution
std::mutex mutexApi_; // to protect call to public API 
std::mutex mutexNotifyThread_; // to prevent multiple thread in // for sending notification

//////////////////////////////////////
// LockGuard for the SecureElement API
class SecureElementApiLockGuard
{
    private:
        std::lock_guard<std::mutex> lock_;

    public:
        SecureElementApiLockGuard( std::mutex& mutex) 
            : lock_(mutex)
        {
            ALOGD("SecureElement: SecureElement API locked");
        }

        ~SecureElementApiLockGuard()
        {
            ALOGD("SecureElement: unlock SecureElement API ...");
        }

        SecureElementApiLockGuard(const SecureElementApiLockGuard&) = delete;
        SecureElementApiLockGuard& operator=(const SecureElementApiLockGuard&) = delete;
};

//////////////////////////////////////
// LockGuard used by notification thread (only one thread executed at a time)
class NotificationThreadLockGuard
{
    private:
        std::lock_guard<std::mutex> lock_;

    public:
        NotificationThreadLockGuard( std::mutex& mutex) 
            : lock_(mutex)
        {
            ALOGD("SecureElement: notification thread locked");
        }

        ~NotificationThreadLockGuard()
        {
            ALOGD("SecureElement: notification thread unlocked ...");
        }

        NotificationThreadLockGuard(const NotificationThreadLockGuard&) = delete;
        NotificationThreadLockGuard& operator=(const NotificationThreadLockGuard&) = delete;
};



SecureElement::SecureElement(const char* ese_name){
    ALOGI("SecureElement:%s -- begin", __func__);
    
    nbrOpenChannel = 0;
    ctx = NULL;

    config_filename = CONFIG_FILE;

    start_time = duration_cast<std::chrono::seconds>(
            android::base::boot_clock::now().time_since_epoch()).count();

    ALOGI("SecureElement:%s -- end", __func__);    
}

int SecureElement::resetSE(){
    int n;

    isBasicChannelOpen = false;
    nbrOpenChannel = 0;

    ALOGD("SecureElement:%s thalesEse_reset start", __func__);
    n = thalesEse_reset(ctx);
    if (n >= 0) {
        ALOGD("SecureElement:%s Reset Successfull\n", __func__);
    } else {
        ALOGE("SecureElement:%s Failed to reset\n", __func__);
    }

    return n;
}

int SecureElement::cipRequest(){
    int n;

    isBasicChannelOpen = false;
    nbrOpenChannel = 0;

    ALOGD("SecureElement:%s thalesEse_cip start", __func__);
    n = thalesEse_cip(ctx, atr, sizeof(atr));
    if (n >= 0) {
        atr_size = n;
        ALOGD("SecureElement:%s received ATR (CIP) of %d bytes\n", __func__, n);
        dump_bytes("ATR (CIP): ", atr, n);
    } else {
        ALOGE("SecureElement:%s Failed to reset and get ATR (CIP): %s\n", __func__, strerror(errno));
    }

    return n;
}

bool SecureElement::isHalLocked(){

    ALOGD("SecureElement:%s checking if application caller shall be blocked...", __func__);
    bool accessLocked = true;

    uid_t uid = AIBinder_getCallingUid();
    ALOGD("SecureElement:%s caller UID : %d", __func__, uid);
    
    const char* ptrSid = AIBinder_getCallingSid();
    ALOGD("SecureElement:%s caller SID : %s", __func__, ptrSid ? ptrSid : "(null)");

    if(lockUID_.has_value() && uid == lockUID_.value())
    {
        if( uid == AID_ROOT)
            ALOGD("SecureElement:%s Called by SE Update Agent (ROOT)", __func__);
        else
            ALOGD("SecureElement:%s Called by SE Update Agent", __func__);

        return false;
    }

    ALOGI("SecureElement:%s Checking properties (caller is not SE Update Agent)...", __func__);
    std::optional<int> lock = getPropertyValue(propertySEHalLock_);
    std::optional<int> lockDuration = getPropertyValue(propertySEHalLockDuration_);

    if( lock.has_value() && lockDuration.has_value())
    {
        ALOGD("SecureElement:%s property[%s]=%d", __func__, propertySEHalLock_.c_str(), lock.value());
        ALOGD("SecureElement:%s property[%s]=%d", __func__, propertySEHalLockDuration_.c_str(), lockDuration.value());

        if( lock.value() == 1 && lockDuration.value() > 0)
        {
            int64_t now = duration_cast<std::chrono::seconds>(android::base::boot_clock::now().time_since_epoch()).count();
            if (now >= start_time + lockDuration.value()) 
            {
                ALOGD("SecureElement:%s SE HAL access not locked (lock duration exceeded)", __func__);
                accessLocked = false;
            }
            else
            {
                ALOGI("SecureElement:%s SE HAL access locked (lock duration not yet exceeded)", __func__);
            }
        }            
        else
        {
            ALOGD("SecureElement:%s SE HAL access not locked (lock deactivated by flag)", __func__);
            accessLocked = false;
        }
    }
    else
    {
        if( !lock.has_value())
            ALOGW("SecureElement:%s property[%s] is undefined, empty or not an integer", __func__, propertySEHalLock_.c_str());

        if( !lockDuration.has_value())
            ALOGW("SecureElement:%s property[%s] is undefined, empty or not an integer", __func__, propertySEHalLockDuration_.c_str());
        accessLocked = false;
    }

    return accessLocked;
}

ScopedAStatus SecureElement::_selectAID(const std::vector<uint8_t>& aid, uint8_t p2, size_t channelNumber, std::variant<::aidl::android::hardware::secure_element::LogicalChannelResponse*, std::vector<uint8_t>*> aidl_return)
{
    /*Start Sending select command after Manage Channel is successful.*/
    ALOGD("SecureElement:%s Sending selectApdu", __func__);

    size_t ext_channelNumber = 0xff;
    if(channelNumber > 0x03) {
        ext_channelNumber = 0x40 + channelNumber - 0x04;
    } else {
        ext_channelNumber = channelNumber;
    }
    nbrOpenChannel++;

    int mSecureElementStatus = IOERROR;

    std::vector<uint8_t> cmdApdu;
    std::vector<uint8_t> respApdu(MAX_APDU_SIZE);
    std::vector<uint8_t> response;
    int resp_len = 0;
    int getResponseOffset = 0;
    uint16_t sw = 0;


    cmdApdu.push_back(ext_channelNumber);
    cmdApdu.push_back(0xA4);
    cmdApdu.push_back(0x04);
    cmdApdu.push_back(p2);
    cmdApdu.push_back(aid.size());
    cmdApdu.insert(cmdApdu.end(), aid.begin(), aid.end());
    cmdApdu.push_back(0x00);

send:
    //reset
    resp_len = 0;
    respApdu.resize(MAX_APDU_SIZE);

    dump_bytes("CMD: ", cmdApdu.data(), cmdApdu.size());
    resp_len = thalesEse_apdu_transmit(ctx, cmdApdu.data(), cmdApdu.size(), respApdu.data(), respApdu.size());
    ALOGD("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);

    if (resp_len < 0 || resp_len > respApdu.size())
    {
        ALOGE("SecureElement:%s selectApdu resp_len = %d", __func__,resp_len);
        if (deinitializeSE() != SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
        mSecureElementStatus = IOERROR;
        return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    }
    else
    {
        respApdu.resize(resp_len);
        sw = (respApdu.at(respApdu.size() - 2) << 8) + respApdu.at(respApdu.size() - 1);
        dump_bytes("RESP: ", respApdu.data(), respApdu.size());

        if (respApdu[respApdu.size() - 2] == 0x90 || respApdu[respApdu.size() - 2] == 0x62 || respApdu[respApdu.size() - 2] == 0x63) {
            response.resize(getResponseOffset + respApdu.size());
            for (size_t i = 0; i < respApdu.size(); i++) {
                response.push_back(respApdu.at(i));
            }
            mSecureElementStatus = SUCCESS;
        }
        else if ( respApdu[respApdu.size() - 2] == 0x61 ||  respApdu[respApdu.size() - 2] == 0x6C)
        {
            response.resize(getResponseOffset + respApdu.size() - 2);
            for (size_t i = 0; i < (respApdu.size()-2); i++) {
                response.push_back(respApdu.at(i));
            }

            getResponseOffset += (respApdu.size() - 2);
            _getResponse[4] = respApdu[respApdu.size() - 1];
            _getResponse[0] = cmdApdu[0];

            dump_bytes("getResponse CMD: ", _getResponse, 5);

            cmdApdu.clear();
            for (size_t i = 0; i < sizeof(_getResponse); i++) {
                cmdApdu.push_back(_getResponse[i]);
            }
            cmdApdu.at(0) = ext_channelNumber;

            if( respApdu[respApdu.size() - 2] == 0x6C)
            {
                cmdApdu.at(4) = respApdu[respApdu.size()  - 1];
                dump_bytes("case2 getResponse CMD: ", cmdApdu.data(), cmdApdu.size());
            }
            else
                dump_bytes("getResponse CMD: ", cmdApdu.data(), cmdApdu.size());

            goto send;
        }
        else if (sw == 0x6A80 || sw == 0x6A81)
            mSecureElementStatus = IOERROR;
        else if (sw == 0x6A82 || sw == 0x6985 || sw == 0x6999)
            mSecureElementStatus = NO_SUCH_ELEMENT_ERROR;
        else if (sw == 0x6A86 ||sw == 0x6A87)
            mSecureElementStatus = UNSUPPORTED_OPERATION;
    }

    /*Check if SELECT command failed, close oppened channel*/
    if (mSecureElementStatus != SUCCESS) {
        internalCloseChannel(channelNumber);
        return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    }

    ALOGD("SecureElement:%s mSecureElementStatus = %d", __func__, (int)mSecureElementStatus);
    if(std::holds_alternative<::aidl::android::hardware::secure_element::LogicalChannelResponse*> (aidl_return)){
        ::aidl::android::hardware::secure_element::LogicalChannelResponse* val = std::get<::aidl::android::hardware::secure_element::LogicalChannelResponse*>(aidl_return);
      *val = LogicalChannelResponse{
          .channelNumber = static_cast<int8_t>(channelNumber),
          .selectResponse = response,
      };
    }
    else if(std::holds_alternative<std::vector<uint8_t>*> (aidl_return)){
        std::vector<uint8_t>* val = std::get<std::vector<uint8_t>*>(aidl_return);
        *val = response;
    }


    if(ext_channelNumber == 0)
        isBasicChannelOpen = true;

    if(mSecureElementStatus != SUCCESS)
        return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    else
        return ScopedAStatus::ok();
}


int SecureElement::initializeSE() {
    ALOGD("SecureElement:%s -- begin", __func__);

    if (checkSeUp) {
        ALOGD("SecureElement:%s Already initialized", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return EXIT_SUCCESS;
    }

    // Intialize lock if nedeed
    if(!lockActivated_.has_value())
    {
        ALOGI("SecureElement:%s Check eSE HAL properties to activate SE Lock mechanism ...", __func__);

        std::optional<int> lock = getPropertyValue(propertySEHalLock_);
        std::optional<int> lockDuration = getPropertyValue(propertySEHalLockDuration_);
        std::optional<int> lockUID = getPropertyValue(propertySEHalLockUID_);

        if( !lock.has_value() || !lockDuration.has_value() || !lockUID.has_value() )
        {
            if( !lock.has_value() )
                ALOGI("SecureElement:%s property [%s] is undefined, empty or not an integer", __func__, propertySEHalLock_.c_str());

            if( !lockDuration.has_value() )
                ALOGI("SecureElement:%s property [%s] is undefined, empty or not an integer", __func__, propertySEHalLockDuration_.c_str());

            if( !lockUID.has_value() )
                ALOGI("SecureElement:%s Property [%s] is undefined, empty or not an integer", __func__, propertySEHalLockUID_.c_str());

            ALOGI("SecureElement:%s SE HAL lock mechanism deactivated", __func__);
            lockActivated_ = false;
        }
        else
        {

            ALOGD("SecureElement:%s property[%s]=%d", __func__, propertySEHalLock_.c_str(), lock.value());
            ALOGD("SecureElement:%s property[%s]=%d", __func__, propertySEHalLockDuration_.c_str(), lockDuration.value());
            ALOGD("SecureElement:%s Lock UID defined by property [%s]: %d", __func__, propertySEHalLockUID_.c_str(), lockUID.value());

            ALOGI("SecureElement:%s SE HAL lock mechanism activated", __func__);


            lockActivated_ = true;

            ALOGD("SecureElement:%s Automatic lock configuration ...", __func__);

            if( lockDuration.value() < 5)
            {
                ALOGD("SecureElement:%s Set property[%s]=to '5'", __func__, propertySEHalLockDuration_.c_str());
                android::base::SetProperty( propertySEHalLockDuration_, "5");
            }

            if( lock.value() != 1 )
            {
                ALOGD("SecureElement:%s Set property[%s]=to '1'", __func__, propertySEHalLock_.c_str());
                android::base::SetProperty( propertySEHalLock_, "1");
            }

            lockUID_ = lockUID;
        }
    }

    if (thalesEse_new(&ctx) < 0) {
        ALOGE("SecureElement:%s thalesEse_new FATAL:%s", __func__,strerror(errno));
        ALOGD("SecureElement:%s -- end", __func__);
        return EXIT_FAILURE;
    }
    thalesEse_set_log_level(ctx, 3);

    openConfigFile(1);

    if (thalesEse_open(ctx) < 0) {
        ALOGE("SecureElement:%s thalesEse_open FATAL:%s", __func__,strerror(errno));
        ALOGD("SecureElement:%s -- end", __func__);
        return EXIT_FAILURE;
    }

    if (cipRequest() < 0) {
        thalesEse_close(ctx);
        ctx = NULL;
        ALOGD("SecureElement:%s -- end", __func__);
        return EXIT_FAILURE;
    }

    checkSeUp = true;

    ALOGD("SecureElement:%s -- end", __func__);
    return EXIT_SUCCESS;
}

ScopedAStatus SecureElement::init(const std::shared_ptr<ISecureElementCallback>& clientCallback) {

    ALOGD("SecureElement:%s -- begin", __func__);
    const SecureElementApiLockGuard lockApi(mutexApi_);
    
    if (clientCallback == nullptr) {
        ALOGE("SecureElement:%s clientCallback == nullptr", __func__);
        return ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    
    // Check if OMAPI caller (to save callback)
    if (AIBinder_getCallingUid() == AID_SECURE_ELEMENT)
    {
        LOG(INFO) << "SecureElement:" << __func__ << " Called by Secure Element (OMAPI) -> saving callback ";
        clientCallback_ = clientCallback;
        lastSecureElementStatusSend_.reset();
    }    

    if(!checkSeUp)
    {
        // Not yet initialized
        if (initializeSE() != EXIT_SUCCESS) {
            ALOGE("SecureElement:%s initializeSE Failed", __func__);
            notify(false, "SE Initialized failed");
        } else {
            ALOGD("SecureElement:%s initializeSE Success", __func__);
            notify(true, "SE Initialized");
        }
    } 
    else 
    {
        // Already initialized, check if client just registered
        ALOGD("SecureElement:%s already initialized", __func__);
        if( clientCallback_ == clientCallback )
        {
            // Client just registered => send notification
            notify(true, "SE (already) Initialized");
        }
    }

    ALOGD("SecureElement:%s -- end", __func__);
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::getAtr(std::vector<uint8_t>* aidl_return) {
    const SecureElementApiLockGuard lockApi(mutexApi_);

    std::vector<uint8_t> response;
    response.resize(atr_size);
    memcpy(&response[0], atr, atr_size);
    *aidl_return = response;

    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::isCardPresent(bool* aidl_return) {
    const SecureElementApiLockGuard lockApi(mutexApi_);

    *aidl_return = true;

    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::transmit(const std::vector<uint8_t>& data, std::vector<uint8_t>* aidl_return) {

    ALOGD("SecureElement:%s -- begin", __func__);
    const SecureElementApiLockGuard lockApi(mutexApi_);

    if( lockActivated_.has_value() && lockActivated_.value() && isHalLocked()) 
    {
        ALOGW("SecureElement:%s SE HAL access locked --> Returning IOERROR", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(IOERROR, "eSE HAL access locked for SE Update");
    }

    int resp_len = 0;
    ScopedAStatus status = ScopedAStatus::fromServiceSpecificError(FAILED);

    uint8_t resp[MAX_APDU_SIZE] = {0};

    std::vector<uint8_t> result;

    if (checkSeUp && nbrOpenChannel != 0) {
        dump_bytes("CMD: ", data.data(), data.size());
        resp_len = thalesEse_apdu_transmit(ctx, data.data(), data.size(), resp, sizeof(resp));

        if (resp_len < 0) {
            ALOGE("SecureElement:%s: transmit failed", __func__);
            if (deinitializeSE() != SUCCESS) {
                ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
            }
        } else {
            dump_bytes("RESP: ", resp, resp_len);
            result.resize(resp_len);
            memcpy(&result[0], resp, resp_len);
            status = ScopedAStatus::ok();
        }
    } else {
        ALOGE("SecureElement:%s: transmit failed! No channel is open", __func__);
        status = ScopedAStatus::fromServiceSpecificError(CHANNEL_NOT_AVAILABLE);
    }
    aidl_return->assign(result.begin(), result.end());
    return status;
}

ScopedAStatus SecureElement::openLogicalChannel(const std::vector<uint8_t>& aid, int8_t p2, ::aidl::android::hardware::secure_element::LogicalChannelResponse* aidl_return) {
    ALOGD("SecureElement:%s -- begin", __func__);
    const SecureElementApiLockGuard lockApi(mutexApi_);

    std::vector<uint8_t> respApdu(MAX_APDU_SIZE);
    int resp_len = 0;
    size_t channelNumber = 0xff;

    int mSecureElementStatus = IOERROR;

    if( lockActivated_.has_value() && lockActivated_.value() && isHalLocked()) 
    {
        ALOGW("SecureElement:%s SE HAL access locked --> Returning IOERROR", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(IOERROR, "eSE HAL access locked for SE Update");
    }  
    
    if (!checkSeUp) {
        if (initializeSE() != EXIT_SUCCESS) {
            ALOGE("SecureElement:%s: Failed to re-initialise the eSE HAL", __func__);
            notify(false, "SE Initialized failed");
            return ScopedAStatus::fromServiceSpecificError(IOERROR);
        }
    }

    if (aid.size() > MAX_AID_LEN) {
        ALOGE("SecureElement:%s: Bad AID size", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificError(FAILED);
    }

    dump_bytes("CMD: ", _openChannel, sizeof(_openChannel));

    resp_len = thalesEse_apdu_transmit(ctx, _openChannel, sizeof(_openChannel), respApdu.data(), respApdu.size());
    ALOGD("SecureElement:%s Manage channel resp_len = %d", __func__,resp_len);

    if (resp_len >= 0 && resp_len <= respApdu.size()){
        //no overflow, OK
        respApdu.resize(resp_len);
        dump_bytes("RESP: ", respApdu.data(), respApdu.size());
    }
    else
    {
        //invalid response or overflow
        if (deinitializeSE() != SUCCESS) {
             ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }

        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }

    uint16_t sw = (respApdu.at(respApdu.size() - 2) << 8) + respApdu.at(respApdu.size() - 1);
    if (sw == 0x9000)
    {
        channelNumber = respApdu.at(0);
        ALOGD("SecureElement:%s Logical channel #%d opened", __func__, static_cast<int>(channelNumber));
        mSecureElementStatus = SUCCESS;
    }
    else
    {
        ALOGW("SecureElement:%s Unable to open a logical channel!", __func__);
        if (sw == 0x6A81 || sw == 0x6881)
            mSecureElementStatus = CHANNEL_NOT_AVAILABLE;
        else
            mSecureElementStatus = IOERROR;
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificError(mSecureElementStatus);
    }

    ScopedAStatus status = SecureElement::_selectAID(aid, p2, channelNumber, aidl_return);

    ALOGD("SecureElement:%s -- end", __func__);
    return status;
}

ScopedAStatus SecureElement::openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2, std::vector<uint8_t>* aidl_return) {
    ALOGD("SecureElement:%s -- begin", __func__);
    const SecureElementApiLockGuard lockApi(mutexApi_);

    if( lockActivated_.has_value() && lockActivated_.value() && isHalLocked()) 
    {
        ALOGW("SecureElement:%s SE HAL access locked --> Returning IOERROR", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(IOERROR, "eSE HAL access locked for SE Update");
    }  
        
    if (isBasicChannelOpen) {
        ALOGE("SecureElement:%s: Basic Channel already open", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificError(CHANNEL_NOT_AVAILABLE);
    }

    if (!checkSeUp) {
        if (initializeSE() != EXIT_SUCCESS) {
            ALOGE("SecureElement:%s: Failed to re-initialise the eSE HAL", __func__);
            notify(false, "SE Initialized failed");
            ALOGD("SecureElement:%s -- end", __func__);
            return ScopedAStatus::fromServiceSpecificError(IOERROR);
        }
    }

    if (aid.size() > MAX_AID_LEN) {
        ALOGE("SecureElement:%s: Bad AID size", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificError(FAILED);
    }

    ScopedAStatus status = _selectAID(aid, p2, BASIC_CHANNEL, aidl_return);
    ALOGD("SecureElement:%s -- end", __func__);

    return status;
}

ScopedAStatus SecureElement::closeChannel(int8_t channelNumber) {
    ALOGD("SecureElement:%s -- begin", __func__);
    const SecureElementApiLockGuard lockApi(mutexApi_);

    if( lockActivated_.has_value() && lockActivated_.value() && isHalLocked()) 
    {
        ALOGW("SecureElement:%s SE HAL access locked --> Returning IOERROR", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(IOERROR, "eSE HAL access locked for SE Update");
    }

    if (!checkSeUp) {
        ALOGE("SecureElement:%s cannot closeChannel, HAL is deinitialized", __func__);
        ALOGD("SecureElement:%s end", __func__);
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(FAILED, "eSE HAL is deinitialized");
    }

    int closeChannelStatus = internalCloseChannel(channelNumber);

    ALOGD("SecureElement:%s -- end", __func__);
    if(closeChannelStatus != SUCCESS)
        return ScopedAStatus::fromServiceSpecificError(closeChannelStatus);
    else
        return ScopedAStatus::ok();
}

int SecureElement::internalCloseChannel(int8_t channelNumber)
{
    ALOGD("SecureElement:%s -- begin", __func__);


    int closeChannelStatus = FAILED;

    uint8_t *apdu = nullptr; //65536
    int apdu_len = 10;
    uint8_t *resp = nullptr;
    int resp_len = 0;

    if (channelNumber < 0) {
        ALOGE("SecureElement:%s Channel not supported", __func__);
        closeChannelStatus = FAILED;
    } else if (channelNumber == 0) {
        isBasicChannelOpen = false;
        closeChannelStatus = SUCCESS;
        nbrOpenChannel--;
    } else {
        ALOGD("SecureElement:%s Closing logical channel #%d ...", __func__, channelNumber);

        std::vector<uint8_t> cmd(_closeChannel, _closeChannel+5);
        std::vector<uint8_t> resp(10);

        cmd.at(3) = channelNumber;

        dump_bytes("CMD: ", cmd.data(), cmd.size());
        int resp_len = thalesEse_apdu_transmit(ctx, cmd.data(), cmd.size(), resp.data(), resp.size());
        if (resp_len >= 0){
            resp.resize(resp_len);
            dump_bytes("RESP: ", resp.data(), resp.size());

            if ((resp[resp_len - 2] == 0x90) && (resp[resp_len - 1] == 0x00)) {
                closeChannelStatus = SUCCESS;
                ALOGD("SecureElement:%s Logical channel #%d closed successfully", __func__, channelNumber);
                nbrOpenChannel--;
            } else {
                closeChannelStatus = FAILED;
                ALOGW("SecureElement:%s Closing logical channel #%d failed", __func__, channelNumber);
            }
        }
        else if (resp_len < 0) {
            closeChannelStatus = FAILED;
            ALOGW("SecureElement:%s Closing logical channel #%d failed (unable to transmit APDU)", __func__, channelNumber);
            if (deinitializeSE() != SUCCESS) {
                ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
            }
        }
    }

    if (nbrOpenChannel == 0 && isBasicChannelOpen == false) {
        ALOGD("SecureElement:%s All Channels are closed", __func__);
        if (deinitializeSE() != SUCCESS) {
            ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
        }
    }
    ALOGD("SecureElement:%s -- end", __func__);    
    
    return closeChannelStatus;
}   

void
SecureElement::notify(bool state, const char *message)
{
    ALOGD("SecureElement:%s -- begin", __func__);

    ALOGD("SecureElement:%s state:'%s', message:'%s'", __func__, (state ? "True":"False"), message);

    if( clientCallback_ != nullptr)
    {
        if( !lastSecureElementStatusSend_.has_value() || lastSecureElementStatusSend_.value() != state)
        {
            // state has been changed => send it
            ALOGD("SecureElement:%s manage asynchronous client callback ...", __func__);
            auto callback = clientCallback_;

            std::thread([callback, state, message]() 
            {
                ALOGD("SecureElement: Notification thread started");

                // Prevent notification from multiple thread in //
                const NotificationThreadLockGuard lockNotification(mutexNotifyThread_);

                ALOGD("SecureElement:Notification thread -> Calling onStateChange(%s,'%s') ...", (state ? "True":"False"), message);
                callback->onStateChange(state, message);
                ALOGD("SecureElement:Notification thread -> onStateChange() terminated");

                ALOGD("SecureElement: Notification thread terminated");
            }).detach();

            // Save it
            lastSecureElementStatusSend_ = state;
        }
        else
            ALOGD("SecureElement:%s SecureElement state not updated since last notify() -> do not send it.", __func__);
    }
    else
        ALOGD("SecureElement:%s No callback registered, no notification sent", __func__);

    ALOGD("SecureElement:%s -- end", __func__);
}

void
SecureElement::dump_bytes(const char* message, const uint8_t *bytes, int size)
{
    if (!debug_log_enabled) return;

    if (bytes == nullptr || size <= 0) return;

    std::string result;
    result.reserve(size * 3 + strlen(message));

    result += message;
    result += " :";
    char buffer[4];
    for (int i = 0; i < size; ++i) {
        snprintf(buffer, sizeof(buffer), "%02X%s", bytes[i], (i < size - 1) ? ":" : "");
        result += buffer;
    }

    ALOGD("SecureElement:%s ==> size = %d data = %s", __func__, size, result.c_str());
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
SecureElement::run_apdu(struct thalesEse_ctx *ctx, const uint8_t *apdu, uint8_t *resp, int n, int verbose)
{
    int sw;

    if (verbose)
        dump_bytes("APDU: ", apdu, n);


    n = thalesEse_apdu_transmit(ctx, apdu, n, resp, sizeof(resp));
    if (n < 0) {
        ALOGE("SecureElement:%s FAILED: APDU transmit (%s).\n\n", __func__, strerror(errno));
        return -2;
    } else if (n < 2) {
        dump_bytes("RESP: ", resp, n);
        ALOGE("SecureElement:%s FAILED: not enough data to have a status word.\n", __func__);
        return -2;
    }

    if (verbose) {
        sw = (resp[n - 2] << 8) | resp[n - 1];
        printf("%d bytes, SW=0x%04x\n", n - 2, sw);
        if (n > 2)
            dump_bytes("RESP: ", resp, n - 2);
    }
    return 0;
}

// Helper function: trim whitespace from both ends
static std::string trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(),
                                   [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(str.rbegin(), str.rend(),
                                 [](unsigned char c) { return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

// Helper function: split key=value, handling multiple separators
static bool splitKeyValue(const std::string& line, std::string& key, std::string& value) {
    // Find first separator (space, =, or ;)
    auto sep_pos = line.find_first_of(" =;");
    if (sep_pos == std::string::npos) {
        return false;
    }

    key = trim(line.substr(0, sep_pos));

    // Find start of value (skip all separators)
    auto value_start = line.find_first_not_of(" =;", sep_pos);
    if (value_start == std::string::npos) {
        return false;
    }

    // Find end of value (before trailing separators)
    auto value_end = line.find_last_not_of(" =;\r\n");
    value = line.substr(value_start, value_end - value_start + 1);

    return !key.empty() && !value.empty();
}

int
SecureElement::parseConfigFile(FILE *f, int verbose)
{
    char buffer[1024];
    int line_num = 0;

    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        line_num++;

        std::string line(buffer);
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::string key, value;
        if (!splitKeyValue(line, key, value)) {
            ALOGW("SecureElement:%s Line %d: Invalid format", __func__, line_num);
            continue;
        }

        // Process configuration keys
        if (key == CONFIG_KEY_DEVICE_NODE) {
            ALOGD("SecureElement:%s Defined node: %s", __func__, value.c_str());

            if (value.length() > 0 && value.length() < 256) {
                thalesEse_set_devnode(ctx, value.c_str());
            } else {
                ALOGE("SecureElement:%s Line %d: Invalid DEV_NODE value length: %zu",
                      __func__, line_num, value.length());
            }

        } else if (key == CONFIG_KEY_DEBUG) {
            ALOGD("SecureElement:%s Log state: %s", __func__, value.c_str());

            if (value == "enable") {
                debug_log_enabled = true;
                thalesEse_set_log_level(ctx, 4);
            } else if (value == "disable") {
                debug_log_enabled = false;
                thalesEse_set_log_level(ctx, 3);
            } else {
                ALOGW("SecureElement:%s Line %d: Unknown DEBUG_MODE value '%s'",
                      __func__, line_num, value.c_str());
            }

        } else if (key == CONFIG_KEY_FREQUENCY) {
            size_t pos = 0;
            int result = std::stoi(value, &pos);

            if (pos == value.length()) {
                ALOGD("SecureElement:%s Frequency: %s", __func__, value.c_str());
                thalesEse_set_frequency(ctx, result);
            } else {
                ALOGW("SecureElement:%s Line %d: Unknown FREQUENCY value '%s'",
                      __func__, line_num, value.c_str());
            }

        } else {
            ALOGW("SecureElement:%s Line %d: Unknown key '%s'",
                  __func__, line_num, key.c_str());
        }
    }

    if (ferror(f)) {
        ALOGE("SecureElement:%s Error reading config file", __func__);
        return -1;
    }

    return 0;
}


int
SecureElement::openConfigFile(int verbose)
{
    int   r;
    FILE *f;


    /* filename is not NULL */
    ALOGD("SecureElement:%s Open Config file : %s", __func__, config_filename.c_str());
    f = fopen(config_filename.c_str(), "r");
    if (f) {
        r = parseConfigFile(f, verbose);
        if (r == -1) {
            perror(config_filename.c_str());
            ALOGE("SecureElement:%s Error parse %s Failed", __func__, config_filename.c_str());
        }
        if (fclose(f) != 0) {
            r = -1;
            ALOGE("SecureElement:%s Error close %s Failed", __func__, config_filename.c_str());
        }
    } else {
        r = -1;
        ALOGE("SecureElement:%s Error open %s Failed", __func__, config_filename.c_str());
    }
    return r;
}

int SecureElement::deinitializeSE() {
    int mSecureElementStatus = FAILED;

    ALOGD("SecureElement:%s start", __func__);

    if(checkSeUp){
        if (thalesEse_close(ctx) < 0) {
            mSecureElementStatus = FAILED;
            notify(false, "SE Initialized failed");
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

    ALOGD("SecureElement:%s -- begin", __func__);
    const SecureElementApiLockGuard lockApi(mutexApi_);

    if( lockActivated_.has_value() && lockActivated_.value() && isHalLocked()) 
    {
        ALOGW("SecureElement:%s SE HAL access locked --> Returning IOERROR", __func__);
        ALOGD("SecureElement:%s -- end", __func__);
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(IOERROR, "eSE HAL access locked for SE Update");
    }

    int status = FAILED;

    if (deinitializeSE() != SUCCESS) {
        ALOGE("SecureElement:%s deinitializeSE Failed", __func__);
    }
    notify(false, "reset the SE");

    if (initializeSE() == EXIT_SUCCESS) {
        notify(true, "SE Initialized");
        status = SUCCESS;
    }

    ALOGD("SecureElement:%s --end", __func__);

    if(status != SUCCESS) 
        return ScopedAStatus::fromServiceSpecificError(status);
    else
        return ScopedAStatus::ok();
}

std::optional<int> SecureElement::getPropertyValue(const std::string& propertyName)
{
    std::optional<int> propertyValue;
    
    std::string propertyValueAsString = android::base::GetProperty(propertyName, "");
    if( propertyValueAsString.empty())
    {
        ALOGV("SecureElement:%s property[%s] is empty", __func__, propertyName.c_str());
    }
    else
    {
        ALOGV("SecureElement:%s property[%s]=%s", __func__, propertyName.c_str(), propertyValueAsString.c_str());
        int intValue = 0;
        if( android::base::ParseInt(propertyValueAsString, &intValue, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()))
            propertyValue = intValue;
        else
            ALOGW("SecureElement:%s property[%s] with '%s' value is not an integer value", __func__, propertyName.c_str(), propertyValueAsString.c_str());
    }

    return propertyValue;
}

}
