// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "LightsA3"

#include "Lights.h"

#include <android-base/file.h>
#include <android-base/logging.h>

#include <chrono>

using namespace std::chrono_literals;

namespace aidl::android::hardware::light {

namespace {

constexpr const char* kButtons = "/sys/class/leds/tm2-touchkey/brightness";
constexpr int kButtonsId = 0;

// The touch-key driver is a module that ueventd loads by modalias: the LED may appear after
// this service has started.
constexpr int kStartRetries = 50;
constexpr auto kStartRetryDelay = 200ms;

}  // namespace

Lights::Lights() : mStartThread(&Lights::turnOffAtStart, this) {}

Lights::~Lights() {
    if (mStartThread.joinable()) mStartThread.join();
}

bool Lights::writeButtons(bool on) {
    return ::android::base::WriteStringToFile(on ? "1" : "0", kButtons);
}

// The kernel driver turns the LED on at probe. Android's default is off until the framework
// asks for it (user activity, with its own timeout).
void Lights::turnOffAtStart() {
    for (int i = 0; i < kStartRetries; i++) {
        {
            std::lock_guard lock(mLock);
            if (mStateSet) return;  // the framework's state wins
            if (writeButtons(false)) return;
        }
        std::this_thread::sleep_for(kStartRetryDelay);
    }
    LOG(WARNING) << kButtons << " did not appear";
}

ndk::ScopedAStatus Lights::setLightState(int32_t id, const HwLightState& state) {
    if (id != kButtonsId) return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);

    // On/off only: any colour other than black is on.
    const bool on = (state.color & 0x00ffffff) != 0;
    std::lock_guard lock(mLock);
    mStateSet = true;
    if (!writeButtons(on)) {
        PLOG(ERROR) << "write " << kButtons;
        return ndk::ScopedAStatus::fromServiceSpecificError(EIO);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::getLights(std::vector<HwLight>* _aidl_return) {
    _aidl_return->push_back(HwLight{.id = kButtonsId, .ordinal = 0, .type = LightType::BUTTONS});
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::light
