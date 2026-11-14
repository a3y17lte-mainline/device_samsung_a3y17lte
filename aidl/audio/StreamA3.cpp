// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "AHAL_A3Stream"

#include "StreamA3.h"

#include <Log.h>
#include <error/expected_utils.h>
#include <tinyalsa/asoundlib.h>

#include "AudioRouter.h"

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core::a3y17lte {

namespace {
constexpr int kPcmDevice = 0;  // I2S0 <-> audio mixer AP port
}  // namespace

StreamA3::StreamA3(StreamContext* context, const Metadata& metadata)
    : StreamAlsa(context, metadata, 1 /*readWriteRetries*/) {}

ndk::ScopedAStatus StreamA3::setConnectedDevices(const std::vector<AudioDevice>& devices) {
    if (mIsInput && devices.size() > 1) {
        LOG(ERROR) << __func__ << ": " << devices.size() << " devices for an input stream";
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    RETURN_STATUS_IF_ERROR(StreamCommonImpl::setConnectedDevices(devices));
    AudioRouter::getInstance().setDevices(mIsInput, devices);
    mHasDevices.store(!devices.empty(), std::memory_order_release);
    mConnectedDevicesUpdated.store(true, std::memory_order_release);
    return ndk::ScopedAStatus::ok();
}

::android::status_t StreamA3::transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                       int32_t* latencyMs) {
    if (mConnectedDevicesUpdated.load(std::memory_order_acquire)) {
        // 'setConnectedDevices' was called: I/O restarts with the new routing.
        *actualFrameCount = 0;
        *latencyMs = StreamDescriptor::LATENCY_UNKNOWN;
        return ::android::OK;
    }
    return StreamAlsa::transfer(buffer, frameCount, actualFrameCount, latencyMs);
}

// Every device is the same PCM: one proxy, whatever is connected.
std::vector<alsa::DeviceProfile> StreamA3::getDeviceProfiles() {
    mConnectedDevicesUpdated.store(false, std::memory_order_release);
    const int card = AudioRouter::getInstance().getCard();
    if (card < 0 || !mHasDevices.load(std::memory_order_acquire)) return {};
    return {alsa::DeviceProfile{.card = card,
                                .device = kPcmDevice,
                                .direction = mIsInput ? PCM_IN : PCM_OUT,
                                .isExternal = false}};
}

StreamInA3::StreamInA3(StreamContext&& context, const SinkMetadata& sinkMetadata,
                       const std::vector<MicrophoneInfo>& microphones)
    : StreamIn(std::move(context), microphones), StreamA3(&mContextInstance, sinkMetadata) {}

StreamOutA3::StreamOutA3(StreamContext&& context, const SourceMetadata& sourceMetadata,
                         const std::optional<AudioOffloadInfo>& offloadInfo)
    : StreamOut(std::move(context), offloadInfo), StreamA3(&mContextInstance, sourceMetadata) {}

}  // namespace aidl::android::hardware::audio::core::a3y17lte
