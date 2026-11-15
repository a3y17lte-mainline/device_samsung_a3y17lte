// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/power/BnPower.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace aidl::android::hardware::power::a3y17lte {

// CPU hints through the cpufreq policy limits (scaling_min_freq / scaling_max_freq) of the two
// Cortex-A53 clusters. schedutil keeps choosing the frequency inside the limits, and the
// kernel's thermal cooling still lowers the maximum, whatever is asked for here.
//
//   Boost INTERACTION     minimum = kBoostKhz for the given time
//   Mode LAUNCH           minimum = highest frequency while on (kLaunchMax at the longest)
//   (both also raise the GPU's devfreq minimum: boost frequency / highest frequency)
//   Mode LOW_POWER        maximum = kBoostKhz, boosts ignored
//   Mode INTERACTIVE off, DEVICE_IDLE on: boosts ignored
//
// No hint sessions (ADPF).
class Power : public BnPower {
  public:
    Power();
    ~Power() override;

    ndk::ScopedAStatus setMode(Mode type, bool enabled) override;
    ndk::ScopedAStatus isModeSupported(Mode type, bool* _aidl_return) override;
    ndk::ScopedAStatus setBoost(Boost type, int32_t durationMs) override;
    ndk::ScopedAStatus isBoostSupported(Boost type, bool* _aidl_return) override;
    ndk::ScopedAStatus createHintSession(int32_t tgid, int32_t uid,
                                         const std::vector<int32_t>& threadIds,
                                         int64_t durationNanos,
                                         std::shared_ptr<IPowerHintSession>* _aidl_return) override;
    ndk::ScopedAStatus createHintSessionWithConfig(
            int32_t tgid, int32_t uid, const std::vector<int32_t>& threadIds, int64_t durationNanos,
            SessionTag tag, SessionConfig* config,
            std::shared_ptr<IPowerHintSession>* _aidl_return) override;
    ndk::ScopedAStatus getHintSessionPreferredRate(int64_t* outNanoseconds) override;
    ndk::ScopedAStatus getSessionChannel(int32_t tgid, int32_t uid,
                                         ChannelConfig* _aidl_return) override;
    ndk::ScopedAStatus closeSessionChannel(int32_t tgid, int32_t uid) override;
    ndk::ScopedAStatus getSupportInfo(SupportInfo* _aidl_return) override;
    ndk::ScopedAStatus getCpuHeadroom(const CpuHeadroomParams& params,
                                      CpuHeadroomResult* _aidl_return) override;
    ndk::ScopedAStatus getGpuHeadroom(const GpuHeadroomParams& params,
                                      GpuHeadroomResult* _aidl_return) override;
    ndk::ScopedAStatus sendCompositionData(const std::vector<CompositionData>& in_data) override;
    ndk::ScopedAStatus sendCompositionUpdate(const CompositionUpdate& in_update) override;

  private:
    using Clock = std::chrono::steady_clock;

    bool loadFrequenciesLocked();
    bool loadGpuFrequenciesLocked();
    void applyLocked(Clock::time_point now);
    void run();

    std::mutex mLock;
    std::condition_variable mCv;
    std::thread mThread;
    bool mStop = false;

    // Hint state
    bool mInteractive = true;
    bool mDeviceIdle = false;
    bool mLowPower = false;
    bool mLaunch = false;
    Clock::time_point mLaunchEnd;
    Clock::time_point mBoostEnd;

    // cpufreq: limits of the hardware and what was written last (0 = nothing yet)
    int mMinKhz = 0;
    int mMaxKhz = 0;
    int mBoostKhz = 0;
    int mWrittenMin = 0;
    int mWrittenMax = 0;

    // GPU devfreq: the same for its minimum, in Hz
    int mGpuMinHz = 0;
    int mGpuMaxHz = 0;
    int mGpuBoostHz = 0;
    int mWrittenGpuMin = 0;
};

}  // namespace aidl::android::hardware::power::a3y17lte
