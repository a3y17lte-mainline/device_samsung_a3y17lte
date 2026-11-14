// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "core-impl/Module.h"

namespace aidl::android::hardware::audio::core::a3y17lte {

// The "default" (primary) module: speaker, earpiece, wired headset/headphones and the three
// microphones, all on the a3y17lte card. AOSP's ModulePrimary is written for the emulator
// (paced, stubbed I/O) and final, so this mirrors it with real ALSA streams.
class ModuleA3 final : public Module {
  public:
    explicit ModuleA3(std::unique_ptr<Configuration>&& config)
        : Module(Type::DEFAULT, std::move(config)) {}

  protected:
    ndk::ScopedAStatus createInputStream(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SinkMetadata& sinkMetadata,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>& microphones,
            std::shared_ptr<StreamIn>* result) override;
    ndk::ScopedAStatus createOutputStream(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                    offloadInfo,
            std::shared_ptr<StreamOut>* result) override;
    int32_t getNominalLatencyMs(
            const ::aidl::android::media::audio::common::AudioPortConfig& portConfig) override;
};

}  // namespace aidl::android::hardware::audio::core::a3y17lte
