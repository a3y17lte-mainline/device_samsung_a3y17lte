// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "NfcHAL_A3"

#include "Nfc.h"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>

namespace aidl::android::hardware::nfc::a3y17lte {

using ::android::base::unique_fd;
using ndk::ScopedAStatus;

namespace {

constexpr const char* kDevice = "/dev/s3fwrn5";

// include/uapi/linux/s3fwrn5-raw.h of the a3y17lte kernel (not in the platform's headers):
// 0 = power off, 1 = power cycle into NCI mode.
#define S3FWRN5_RAW_IOC_POWER _IOW('S', 0x01, __u32)

constexpr size_t kMaxPacket = 3 + 255;  // NCI header + payload

// Proprietary FW_CFG (GID 0xF, OID 0x28): the reference clock, 0x12 = 26 MHz through the PLL.
// The response carries one status byte: 0 = done, 2 = clock mismatch (power cycle, send again).
constexpr uint8_t kFwCfgCmd[] = {0x2F, 0x28, 0x01, 0x12};
constexpr uint8_t kFwCfgRsp[] = {0x4F, 0x28};
constexpr uint8_t kFwCfgOk = 0x00;
constexpr int kFwCfgTimeoutMs = 1000;
constexpr int kFwCfgTries = 2;

std::string hex(const uint8_t* data, size_t length) {
    std::string out;
    for (size_t i = 0; i < length; i++) {
        ::android::base::StringAppendF(&out, "%02x", data[i]);
    }
    return out;
}

ScopedAStatus failed() {
    return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
}

}  // namespace

Nfc::Nfc()
    : mWakeFd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
      mDeathRecipient(AIBinder_DeathRecipient_new(onClientDeath)) {}

Nfc::~Nfc() {
    std::lock_guard lock(mLock);
    closeLocked();
}

void Nfc::onClientDeath(void* cookie) {
    LOG(WARNING) << "the NFC service died: switching the chip off";
    Nfc* nfc = static_cast<Nfc*>(cookie);
    std::lock_guard lock(nfc->mLock);
    nfc->closeLocked();
}

// Powers the chip up in NCI mode and gives it its clock configuration. Whatever else arrives
// meanwhile is dropped: the NCI host has not sent anything yet.
bool Nfc::powerOn() {
    for (int attempt = 0; attempt < kFwCfgTries; attempt++) {
        __u32 on = 1;
        if (ioctl(mFd.get(), S3FWRN5_RAW_IOC_POWER, &on) < 0) {
            PLOG(ERROR) << "power on";
            return false;
        }
        if (TEMP_FAILURE_RETRY(::write(mFd.get(), kFwCfgCmd, sizeof(kFwCfgCmd))) !=
            sizeof(kFwCfgCmd)) {
            PLOG(ERROR) << "FW_CFG write";
            continue;
        }

        const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(kFwCfgTimeoutMs);
        for (;;) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
            pollfd pfd = {mFd.get(), POLLIN, 0};
            if (left.count() <= 0 || TEMP_FAILURE_RETRY(poll(&pfd, 1, left.count())) <= 0) {
                LOG(ERROR) << "FW_CFG: no response";
                break;
            }
            uint8_t packet[kMaxPacket];
            const ssize_t length = TEMP_FAILURE_RETRY(read(mFd.get(), packet, sizeof(packet)));
            if (length < 4 || packet[0] != kFwCfgRsp[0] || packet[1] != kFwCfgRsp[1]) continue;
            if (packet[3] == kFwCfgOk) return true;
            LOG(ERROR) << "FW_CFG: status " << static_cast<int>(packet[3]);
            break;
        }
    }
    return false;
}

// Everything the client hears comes from this thread: the result of open and powerCycle, the
// events the Binder calls ask for, and the packets from the chip.
void Nfc::run(std::shared_ptr<INfcClientCallback> callback) {
    callback->sendEvent(NfcEvent::OPEN_CPLT, powerOn() ? NfcStatus::OK : NfcStatus::FAILED);

    for (;;) {
        pollfd fds[2] = {{mFd.get(), POLLIN, 0}, {mWakeFd.get(), POLLIN, 0}};
        if (TEMP_FAILURE_RETRY(poll(fds, 2, -1)) <= 0) continue;

        if (fds[1].revents & POLLIN) {
            eventfd_t count;
            eventfd_read(mWakeFd.get(), &count);

            std::vector<NfcEvent> events;
            bool powerCycle;
            {
                std::lock_guard lock(mQueueLock);
                if (mStop) return;
                events.swap(mEvents);
                powerCycle = mPowerCycle;
                mPowerCycle = false;
            }
            if (powerCycle) {
                callback->sendEvent(NfcEvent::OPEN_CPLT,
                                    powerOn() ? NfcStatus::OK : NfcStatus::FAILED);
            }
            for (NfcEvent event : events) callback->sendEvent(event, NfcStatus::OK);
            continue;
        }

        if (fds[0].revents & POLLIN) {
            uint8_t packet[kMaxPacket];
            const ssize_t length = TEMP_FAILURE_RETRY(read(mFd.get(), packet, sizeof(packet)));
            if (length <= 0) continue;
            if (mVerbose) LOG(INFO) << "rx " << hex(packet, length);
            callback->sendData(std::vector<uint8_t>(packet, packet + length));
        } else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            LOG(ERROR) << kDevice << " failed";
            callback->sendEvent(NfcEvent::ERROR, NfcStatus::ERR_TRANSPORT);
            return;
        }
    }
}

void Nfc::postEventLocked(NfcEvent event) {
    {
        std::lock_guard lock(mQueueLock);
        mEvents.push_back(event);
    }
    eventfd_write(mWakeFd.get(), 1);
}

void Nfc::closeLocked() {
    if (mThread.joinable()) {
        {
            std::lock_guard lock(mQueueLock);
            mStop = true;
        }
        eventfd_write(mWakeFd.get(), 1);
        mThread.join();
    }
    {
        std::lock_guard lock(mQueueLock);
        mStop = false;
        mPowerCycle = false;
        mEvents.clear();
    }
    eventfd_t count;
    eventfd_read(mWakeFd.get(), &count);

    mFd.reset();  // the kernel switches the chip off
    if (mCallback != nullptr) {
        AIBinder_unlinkToDeath(mCallback->asBinder().get(), mDeathRecipient.get(), this);
        mCallback = nullptr;
    }
}

ScopedAStatus Nfc::open(const std::shared_ptr<INfcClientCallback>& clientCallback) {
    if (clientCallback == nullptr) return failed();

    std::lock_guard lock(mLock);
    closeLocked();  // open while open starts again

    mFd.reset(::open(kDevice, O_RDWR | O_NONBLOCK | O_CLOEXEC));
    if (mFd.get() < 0) {
        PLOG(ERROR) << "open " << kDevice;
        return failed();
    }
    mCallback = clientCallback;
    AIBinder_linkToDeath(clientCallback->asBinder().get(), mDeathRecipient.get(), this);
    mThread = std::thread(&Nfc::run, this, clientCallback);
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::close(NfcCloseType) {
    std::shared_ptr<INfcClientCallback> callback;
    {
        std::lock_guard lock(mLock);
        if (mFd.get() < 0) return failed();
        callback = mCallback;
        closeLocked();
    }
    // Both close types end the same way: the chip is off (no card emulation without power).
    callback->sendEvent(NfcEvent::CLOSE_CPLT, NfcStatus::OK);
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::coreInitialized() {
    std::lock_guard lock(mLock);
    if (mFd.get() < 0) return failed();
    postEventLocked(NfcEvent::POST_INIT_CPLT);
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::preDiscover() {
    std::lock_guard lock(mLock);
    if (mFd.get() < 0) return failed();
    postEventLocked(NfcEvent::PRE_DISCOVER_CPLT);
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::powerCycle() {
    std::lock_guard lock(mLock);
    if (mFd.get() < 0) return failed();
    {
        std::lock_guard queueLock(mQueueLock);
        mPowerCycle = true;
    }
    eventfd_write(mWakeFd.get(), 1);
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::write(const std::vector<uint8_t>& data, int32_t* _aidl_return) {
    std::lock_guard lock(mLock);
    *_aidl_return = 0;
    if (mFd.get() < 0) return failed();
    if (mVerbose) LOG(INFO) << "tx " << hex(data.data(), data.size());

    const ssize_t written = TEMP_FAILURE_RETRY(::write(mFd.get(), data.data(), data.size()));
    if (written < 0) {
        PLOG(ERROR) << "write of " << data.size() << " bytes";
        return ScopedAStatus::ok();
    }
    *_aidl_return = static_cast<int32_t>(written);
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::factoryReset() {
    // Nothing is stored in the chip or by this HAL.
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::getConfig(NfcConfig* _aidl_return) {
    NfcConfig config;
    config.nfaPollBailOutMode = true;
    config.presenceCheckAlgorithm = PresenceCheckAlgorithm::ISO_DEP_NAK;

    // The RF protocol numbers of the S3NRN80, from the stock stack's NFA_PROPRIETARY_CFG
    // (00 81 82 80 8a 81 90 70 74 f4): ISO 15693 is 0x80 and MIFARE Classic 0x81, not the NCI
    // standard values. The stock table has one more byte (0x90) than AOSP knew fields for at
    // the time; the Chinese ID protocol stays unset.
    config.nfaProprietaryCfg.protocol18092Active = 0x00;
    config.nfaProprietaryCfg.protocolBPrime = static_cast<int8_t>(0x81);
    config.nfaProprietaryCfg.protocolDual = static_cast<int8_t>(0x82);
    config.nfaProprietaryCfg.protocol15693 = static_cast<int8_t>(0x80);
    config.nfaProprietaryCfg.protocolKovio = static_cast<int8_t>(0x8a);
    config.nfaProprietaryCfg.protocolMifare = static_cast<int8_t>(0x81);
    config.nfaProprietaryCfg.discoveryPollKovio = 0x70;
    config.nfaProprietaryCfg.discoveryPollBPrime = 0x74;
    config.nfaProprietaryCfg.discoveryListenBPrime = static_cast<int8_t>(0xf4);
    config.nfaProprietaryCfg.protocolChineseId = static_cast<int8_t>(0xff);

    // Everything is routed to the host: the eSE is behind the secure world and there is no
    // UICC connection (no modem).
    config.defaultRoute = 0x00;
    config.defaultIsoDepRoute = 0x00;
    config.defaultOffHostRoute = 0x00;
    config.defaultOffHostRouteFelica = 0x00;
    config.defaultSystemCodeRoute = 0x00;
    config.defaultSystemCodePowerState = 0x00;
    config.offHostESEPipeId = 0x00;
    config.offHostSIMPipeId = 0x00;
    config.maxIsoDepTransceiveLength = 0xFEFF;
    config.t4tNfceeEnable = false;

    *_aidl_return = config;
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::setEnableVerboseLogging(bool enable) {
    mVerbose = enable;
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::isVerboseLoggingEnabled(bool* _aidl_return) {
    *_aidl_return = mVerbose;
    return ScopedAStatus::ok();
}

ScopedAStatus Nfc::controlGranted(NfcStatus* _aidl_return) {
    *_aidl_return = NfcStatus::OK;
    return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::nfc::a3y17lte
