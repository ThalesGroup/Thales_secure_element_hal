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
#ifndef ANDROID_HARDWARE_SECURE_ELEMENT_AIDL_SECUREELEMENT_H
#define ANDROID_HARDWARE_SECURE_ELEMENT_AIDL_SECUREELEMENT_H

#include <aidl/android/hardware/secure_element/BnSecureElement.h>
#include <android-base/hex.h>
#include <android-base/logging.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_ibinder_platform.h>
#include <algorithm>
#include <string>
#include <optional>
#include <variant>
#include <list>
#include <map>

#include "se-thales/libse-thales.h"

#define CONFIG_FILE "/vendor/etc/libse-thales-hal.conf"

#define CONFIG_KEY_DEVICE_NODE "DEV_NODE"
#define CONFIG_KEY_DEBUG "DEBUG_MODE"
#define CONFIG_KEY_FREQUENCY "FREQUENCY"

using aidl::android::hardware::secure_element::BnSecureElement;
using aidl::android::hardware::secure_element::ISecureElementCallback;
using aidl::android::hardware::secure_element::LogicalChannelResponse;
using android::base::HexString;
using ndk::ScopedAStatus;

namespace se {

struct DeathRecipientDeleter {
    void operator()(AIBinder_DeathRecipient* p) const noexcept {
        AIBinder_DeathRecipient_delete(p);
    }
};
using DeathRecipientPtr = std::unique_ptr<AIBinder_DeathRecipient, DeathRecipientDeleter>;
struct SecureElement;

struct DeathCookie {
    uint32_t        callerId_; // this value is unique
    SecureElement*  ptrRefSecureElement_; // never delete it
    pid_t           pid_;
    uid_t           uid_;
};

struct CallerInfo {
    ndk::SpAIBinder                         binder_;
    std::shared_ptr<ISecureElementCallback> clientCallback_;
    std::unique_ptr<DeathCookie>            deathCookie_; // If linkToDeath fails => null
};

struct ChannelInfo
{
    ChannelInfo(pid_t pid, uid_t uid) :
        pid_(pid), uid_(uid){};

    ChannelInfo(pid_t pid, uid_t uid, const std::vector<uint8_t>& aid ) :
        pid_(pid), uid_(uid), aid_(aid){};

    // Application Owner
    pid_t           pid_; 
    uid_t           uid_;

    // Empty if closed or not yet selected
    std::vector<uint8_t> aid_;
};

struct SecureElement : public BnSecureElement {
    SecureElement(const char* ese_name);
    ~SecureElement();

    ScopedAStatus init(const std::shared_ptr<ISecureElementCallback>& clientCallback) override;
    ScopedAStatus openLogicalChannel(const std::vector<uint8_t>& aid, int8_t p2, ::aidl::android::hardware::secure_element::LogicalChannelResponse* aidl_return) override;
    ScopedAStatus openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2, std::vector<uint8_t>* aidl_return) override;
    ScopedAStatus getAtr(std::vector<uint8_t>* aidl_return) override;
    ScopedAStatus transmit(const std::vector<uint8_t>& data, std::vector<uint8_t>* aidl_return) override;
    ScopedAStatus isCardPresent(bool* aidl_return) override;
    ScopedAStatus closeChannel(int8_t channelNumber) override;
    ScopedAStatus reset() override;

    private:

    struct thalesEse_ctx *ctx = nullptr;
    bool checkSeUp = false;
    uint8_t atr[64];
    uint8_t atr_size;
    std::string config_filename;
    std::map<int8_t, ChannelInfo> channelOpened_; // channel number - info
    int64_t start_time;
    std::optional<bool> lockActivated_; // deactivation or activation done when init() method is called for the first time
    std::optional<uid_t> lockUID_; // UID authorized when locked (if empty, only OMAPI, KEYMINT & Weaver are blocked)

    std::shared_ptr<ISecureElementCallback> clientCallback_;
    std::optional<bool> lastSecureElementStatusSend_; // last SE Status send by notify(); if empty, never send

    static uint32_t lastIdValue_; // Incremented at each init()
    std::list<CallerInfo>     callers_;

    // allocated on constructor / free automatically
    DeathRecipientPtr               callerDeathRecipient_; // shared with all application caller; 
    static void onCallerDied(void* ptrCookie);
    void registerCaller(const std::shared_ptr<ISecureElementCallback>& clientCallback, pid_t callerPid, uid_t callerUid);
    void cleanupCaller(uint32_t callerId, pid_t callerPid, uid_t callerUid);

    int initializeSE();
    int deinitializeSE();
    static int run_apdu(struct thalesEse_ctx *ctx, const uint8_t *apdu, uint8_t *resp, int n, int verbose);
    static int toint(char c);
    static void dump_bytes(const char* message, const uint8_t *bytes, int size);
    static std::string convertHexaToString(const std::vector<uint8_t>& vector);

    int resetSE();
#ifndef LEGACY
    int cipRequest();
#endif // LEGACY    
    int openConfigFile(int verbose);
    int parseConfigFile(FILE *f, int verbose);
    void notify(bool state, const char *message);
    int internalCloseChannel(int8_t channelNumber, bool searchAndRemoveEntry);
    std::optional<int> getPropertyValue(const std::string& propertyName);
    bool isHalLocked();

    ScopedAStatus _selectAID(const std::vector<uint8_t>& aid, uint8_t p2, size_t channelNumber, std::vector<uint8_t>& response);
};

} //se
#endif  // ANDROID_HARDWARE_SECURE_ELEMENT_AIDL_SECUREELEMENT_H
