// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "AHAL_A3Module"

#include "ModuleA3.h"

#include <Log.h>

#include "AudioRouter.h"
#include "StreamA3.h"

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioMode;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core::a3y17lte {

ndk::ScopedAStatus ModuleA3::getTelephony(std::shared_ptr<ITelephony>* _aidl_return) {
    if (!mTelephony) mTelephony = ndk::SharedRefBase::make<TelephonyA3>();
    *_aidl_return = mTelephony.getInstance();
    return ndk::ScopedAStatus::ok();
}

TelephonyA3::TelephonyA3() {
    mTelecomConfig.voiceVolume =
            aidl::android::media::audio::common::Float{TelecomConfig::VOICE_VOLUME_MAX};
    mTelecomConfig.ttyMode = TelecomConfig::TtyMode::OFF;
    mTelecomConfig.isHacEnabled = aidl::android::media::audio::common::Boolean{false};
}

ndk::ScopedAStatus TelephonyA3::getSupportedAudioModes(std::vector<AudioMode>* _aidl_return) {
    *_aidl_return = {AudioMode::NORMAL, AudioMode::RINGTONE, AudioMode::IN_CALL,
                     AudioMode::IN_COMMUNICATION};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TelephonyA3::switchAudioMode(AudioMode in_mode) {
    LOG(INFO) << __func__ << ": " << static_cast<int>(in_mode);
    AudioRouter::getInstance().setCallActive(in_mode == AudioMode::IN_CALL);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TelephonyA3::setTelecomConfig(const TelecomConfig& in_config,
                                                 TelecomConfig* _aidl_return) {
    if (in_config.voiceVolume.has_value()) {
        const float volume = in_config.voiceVolume.value().value;
        if (volume < TelecomConfig::VOICE_VOLUME_MIN || volume > TelecomConfig::VOICE_VOLUME_MAX) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        mTelecomConfig.voiceVolume = in_config.voiceVolume;
        AudioRouter::getInstance().setVoiceVolume(volume);
    }
    if (in_config.ttyMode != TelecomConfig::TtyMode::UNSPECIFIED) {
        mTelecomConfig.ttyMode = in_config.ttyMode;
    }
    if (in_config.isHacEnabled.has_value()) mTelecomConfig.isHacEnabled = in_config.isHacEnabled;
    *_aidl_return = mTelecomConfig;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ModuleA3::createInputStream(StreamContext&& context,
                                               const SinkMetadata& sinkMetadata,
                                               const std::vector<MicrophoneInfo>& microphones,
                                               std::shared_ptr<StreamIn>* result) {
    return createStreamInstance<StreamInA3>(result, std::move(context), sinkMetadata,
                                            microphones);
}

ndk::ScopedAStatus ModuleA3::createOutputStream(
        StreamContext&& context, const SourceMetadata& sourceMetadata,
        const std::optional<AudioOffloadInfo>& offloadInfo, std::shared_ptr<StreamOut>* result) {
    return createStreamInstance<StreamOutA3>(result, std::move(context), sourceMetadata,
                                             offloadInfo);
}

// Period size: 20 ms (960 frames at 48 kHz, the only rate of the audio mixer), two periods.
int32_t ModuleA3::getNominalLatencyMs(const AudioPortConfig&) {
    return 20;
}

}  // namespace aidl::android::hardware::audio::core::a3y17lte
