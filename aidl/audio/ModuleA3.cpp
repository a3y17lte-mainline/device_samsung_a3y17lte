// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "AHAL_A3Module"

#include "ModuleA3.h"

#include <Log.h>

#include "StreamA3.h"

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core::a3y17lte {

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
