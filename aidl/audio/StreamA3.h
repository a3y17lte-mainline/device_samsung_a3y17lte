// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <vector>

#include "core-impl/StreamAlsa.h"

namespace aidl::android::hardware::audio::core::a3y17lte {

// Direct ALSA stream on PCM device 0 of the a3y17lte card (the I2S0 <-> audio mixer link),
// like the USB streams. The connected devices only change the mixer routing.
class StreamA3 : public StreamAlsa {
  public:
    StreamA3(StreamContext* context, const Metadata& metadata);

    // Methods of 'DriverInterface'.
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;

    // Overridden methods of 'StreamCommonImpl', called on a Binder thread.
    ndk::ScopedAStatus setConnectedDevices(const ConnectedDevices& devices) override;

  protected:
    std::vector<alsa::DeviceProfile> getDeviceProfiles() override;

    std::atomic<bool> mConnectedDevicesUpdated = false;
    std::atomic<bool> mHasDevices = false;  // read on the worker thread
};

class StreamInA3 final : public StreamIn, public StreamA3 {
  public:
    friend class ndk::SharedRefBase;
    StreamInA3(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SinkMetadata& sinkMetadata,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>& microphones);

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }
};

class StreamOutA3 final : public StreamOut, public StreamA3 {
  public:
    friend class ndk::SharedRefBase;
    StreamOutA3(StreamContext&& context,
                const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
                const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                        offloadInfo);

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }
};

}  // namespace aidl::android::hardware::audio::core::a3y17lte
