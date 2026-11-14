// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/nfc/BnNfc.h>
#include <android-base/unique_fd.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace aidl::android::hardware::nfc::a3y17lte {

// NFC HAL for the Samsung S3NRN80: passes NCI packets between Android's NFC stack and the
// kernel's raw NCI device (/dev/s3fwrn5, s3fwrn5 with CONFIG_NFC_S3FWRN5_RAW). The kernel does
// the I2C framing, the interrupt, the power sequence and the 26 MHz reference clock.
//
// What is specific to the chip:
//  - after every power-up, before CORE_RESET, it needs its clock configuration (the
//    proprietary FW_CFG command), as the stock HAL sends it;
//  - MIFARE Classic and ISO 15693 tags come as proprietary RF protocols (getConfig).
//
// The bootloader mode of the chip is not used and not reachable: no firmware is ever written.
class Nfc : public BnNfc {
  public:
    Nfc();
    ~Nfc() override;

    ndk::ScopedAStatus open(const std::shared_ptr<INfcClientCallback>& clientCallback) override;
    ndk::ScopedAStatus close(NfcCloseType type) override;
    ndk::ScopedAStatus coreInitialized() override;
    ndk::ScopedAStatus factoryReset() override;
    ndk::ScopedAStatus getConfig(NfcConfig* _aidl_return) override;
    ndk::ScopedAStatus powerCycle() override;
    ndk::ScopedAStatus preDiscover() override;
    ndk::ScopedAStatus write(const std::vector<uint8_t>& data, int32_t* _aidl_return) override;
    ndk::ScopedAStatus setEnableVerboseLogging(bool enable) override;
    ndk::ScopedAStatus isVerboseLoggingEnabled(bool* _aidl_return) override;
    ndk::ScopedAStatus controlGranted(NfcStatus* _aidl_return) override;

  private:
    static void onClientDeath(void* cookie);

    void closeLocked();
    void postEventLocked(NfcEvent event);
    bool powerOn();
    void run(std::shared_ptr<INfcClientCallback> callback);

    std::mutex mLock;  // the state below, between Binder threads
    std::shared_ptr<INfcClientCallback> mCallback;
    ::android::base::unique_fd mFd;      // /dev/s3fwrn5; open = chip in use
    ::android::base::unique_fd mWakeFd;  // eventfd: something for the thread to do
    std::thread mThread;

    std::mutex mQueueLock;  // what the thread is asked to do
    std::vector<NfcEvent> mEvents;
    bool mPowerCycle = false;
    bool mStop = false;

    std::atomic<bool> mVerbose = false;
    ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;
};

}  // namespace aidl::android::hardware::nfc::a3y17lte
