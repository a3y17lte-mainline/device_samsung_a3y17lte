// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "AHAL_A3Router"

#include "AudioRouter.h"

#include <Log.h>
#include <audio_route/audio_route.h>

#include <unistd.h>

#include <climits>
#include <cstdio>

using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioDeviceType;

namespace aidl::android::hardware::audio::core::a3y17lte {

namespace {

constexpr const char* kCardLink = "/proc/asound/a3y17lte";  // -> "card<N>"
constexpr const char* kMixerPaths = "/vendor/etc/mixer_paths.xml";

// mixer_paths.xml path for a device, empty for devices this card does not serve.
std::string pathFor(const AudioDevice& device) {
    switch (device.type.type) {
        case AudioDeviceType::OUT_SPEAKER:
            return "speaker";
        case AudioDeviceType::OUT_SPEAKER_EARPIECE:
            return "earpiece";
        case AudioDeviceType::OUT_HEADSET:
        case AudioDeviceType::OUT_HEADPHONE:
            return "headphones";
        case AudioDeviceType::IN_MICROPHONE:
            return "main-mic";
        case AudioDeviceType::IN_MICROPHONE_BACK:
            return "second-mic";
        case AudioDeviceType::IN_HEADSET:
            return "headset-mic";
        default:
            return "";
    }
}

}  // namespace

AudioRouter& AudioRouter::getInstance() {
    static AudioRouter instance;
    return instance;
}

int AudioRouter::findCardLocked() {
    char target[PATH_MAX] = {};
    ssize_t len = readlink(kCardLink, target, sizeof(target) - 1);
    int card;
    if (len <= 0 || sscanf(target, "card%d", &card) != 1) return -1;
    return card;
}

int AudioRouter::getCard() {
    std::lock_guard lock(mLock);
    if (mCard < 0) mCard = findCardLocked();
    return mCard;
}

// The card comes from modules (codec, mixer, amplifier, graph card), which may still be
// loading when the service starts: initialise on first use, retry until it is there.
bool AudioRouter::initLocked() {
    if (mRoute != nullptr) return true;
    if (mCard < 0) mCard = findCardLocked();
    if (mCard < 0) {
        LOG(WARNING) << __func__ << ": sound card a3y17lte not registered yet";
        return false;
    }
    mRoute = audio_route_init(mCard, kMixerPaths);
    if (mRoute == nullptr) {
        LOG(ERROR) << __func__ << ": audio_route_init(" << mCard << ", " << kMixerPaths
                   << ") failed";
        return false;
    }
    LOG(INFO) << __func__ << ": card " << mCard << ", " << kMixerPaths;
    return true;
}

void AudioRouter::applyLocked() {
    if (!initLocked()) return;
    audio_route_reset(mRoute);
    for (const auto& path : mOutputPaths) audio_route_apply_path(mRoute, path.c_str());
    for (const auto& path : mInputPaths) audio_route_apply_path(mRoute, path.c_str());
    audio_route_update_mixer(mRoute);
}

void AudioRouter::setDevices(bool isInput, const std::vector<AudioDevice>& devices) {
    std::vector<std::string> paths;
    for (const auto& device : devices) {
        if (std::string path = pathFor(device); !path.empty()) {
            paths.push_back(path);
        } else {
            LOG(WARNING) << __func__ << ": no mixer path for " << device.toString();
        }
    }

    std::lock_guard lock(mLock);
    (isInput ? mInputPaths : mOutputPaths) = std::move(paths);
    applyLocked();
}

}  // namespace aidl::android::hardware::audio::core::a3y17lte
