// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "AHAL_A3Router"

#include "AudioRouter.h"

#include <Log.h>
#include <audio_route/audio_route.h>
#include <tinyalsa/asoundlib.h>

#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cmath>
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
    if (mCallActive) {
        audio_route_apply_path(mRoute, "call");
        if (mOutputPaths.empty()) audio_route_apply_path(mRoute, "earpiece");
        if (mOutputPaths.empty() || mOutputPaths == std::vector<std::string>{"earpiece"}) {
            audio_route_apply_path(mRoute, "call-earpiece");
        }
        if (mInputPaths.empty()) {
            audio_route_apply_path(mRoute, mHeadsetOutput ? "headset-mic" : "call-main-mic");
        }
    }
    audio_route_update_mixer(mRoute);
    if (mCallActive) applyVoiceVolumeLocked();
}

// "Modem Playback Volume" is 0 to 7, 7 = 0 dB. It is in no mixer path, so libaudioroute
// leaves it alone. The lowest steps are too quiet to be of use.
void AudioRouter::applyVoiceVolumeLocked() {
    if (mCard < 0) return;
    struct mixer* mixer = mixer_open(mCard);
    if (mixer == nullptr) return;
    if (struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, "Modem Playback Volume")) {
        mixer_ctl_set_value(ctl, 0, 2 + static_cast<int>(std::lround(mVoiceVolume * 5)));
    }
    mixer_close(mixer);
}

void AudioRouter::setCallActive(bool active) {
    std::lock_guard lock(mLock);
    if (mCallActive == active) return;
    mCallActive = active;
    LOG(INFO) << __func__ << ": " << active;
    applyLocked();
}

void AudioRouter::setVoiceVolume(float volume) {
    std::lock_guard lock(mLock);
    mVoiceVolume = std::min(1.0f, std::max(0.0f, volume));
    if (mCallActive) applyVoiceVolumeLocked();
}

void AudioRouter::setDevices(bool isInput, const std::vector<AudioDevice>& devices) {
    std::vector<std::string> paths;
    bool headset = false;
    for (const auto& device : devices) {
        headset |= device.type.type == AudioDeviceType::OUT_HEADSET;
        if (std::string path = pathFor(device); !path.empty()) {
            paths.push_back(path);
        } else {
            LOG(WARNING) << __func__ << ": no mixer path for " << device.toString();
        }
    }

    std::lock_guard lock(mLock);
    (isInput ? mInputPaths : mOutputPaths) = std::move(paths);
    if (!isInput) mHeadsetOutput = headset;
    applyLocked();
}

}  // namespace aidl::android::hardware::audio::core::a3y17lte
