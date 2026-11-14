// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <android-base/unique_fd.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace aidl::android::hardware::vibrator {

// Vibrator on a Linux input force-feedback device with FF_RUMBLE (regulator-haptic on the
// a3y17lte). The motor runs at one fixed voltage: on/off only, no amplitude control.
class Vibrator : public BnVibrator {
  public:
    Vibrator();
    ~Vibrator();

    ndk::ScopedAStatus getCapabilities(int32_t* _aidl_return) override;
    ndk::ScopedAStatus off() override;
    ndk::ScopedAStatus on(int32_t timeoutMs,
                          const std::shared_ptr<IVibratorCallback>& callback) override;
    ndk::ScopedAStatus perform(Effect effect, EffectStrength strength,
                               const std::shared_ptr<IVibratorCallback>& callback,
                               int32_t* _aidl_return) override;
    ndk::ScopedAStatus performVendorEffect(
            const VendorEffect& effect,
            const std::shared_ptr<IVibratorCallback>& callback) override;
    ndk::ScopedAStatus getSupportedEffects(std::vector<Effect>* _aidl_return) override;
    ndk::ScopedAStatus setAmplitude(float amplitude) override;
    ndk::ScopedAStatus setExternalControl(bool enabled) override;
    ndk::ScopedAStatus getCompositionDelayMax(int32_t* maxDelayMs) override;
    ndk::ScopedAStatus getCompositionSizeMax(int32_t* maxSize) override;
    ndk::ScopedAStatus getSupportedPrimitives(std::vector<CompositePrimitive>* supported) override;
    ndk::ScopedAStatus getPrimitiveDuration(CompositePrimitive primitive,
                                            int32_t* durationMs) override;
    ndk::ScopedAStatus compose(const std::vector<CompositeEffect>& composite,
                               const std::shared_ptr<IVibratorCallback>& callback) override;
    ndk::ScopedAStatus getSupportedAlwaysOnEffects(std::vector<Effect>* _aidl_return) override;
    ndk::ScopedAStatus alwaysOnEnable(int32_t id, Effect effect, EffectStrength strength) override;
    ndk::ScopedAStatus alwaysOnDisable(int32_t id) override;
    ndk::ScopedAStatus getResonantFrequency(float* resonantFreqHz) override;
    ndk::ScopedAStatus getQFactor(float* qFactor) override;
    ndk::ScopedAStatus getFrequencyResolution(float* freqResolutionHz) override;
    ndk::ScopedAStatus getFrequencyMinimum(float* freqMinimumHz) override;
    ndk::ScopedAStatus getBandwidthAmplitudeMap(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus getPwlePrimitiveDurationMax(int32_t* durationMs) override;
    ndk::ScopedAStatus getPwleCompositionSizeMax(int32_t* maxSize) override;
    ndk::ScopedAStatus getSupportedBraking(std::vector<Braking>* supported) override;
    ndk::ScopedAStatus composePwle(const std::vector<PrimitivePwle>& composite,
                                   const std::shared_ptr<IVibratorCallback>& callback) override;
    ndk::ScopedAStatus getFrequencyToOutputAccelerationMap(
            std::vector<FrequencyAccelerationMapEntry>* _aidl_return) override;
    ndk::ScopedAStatus getPwleV2PrimitiveDurationMaxMillis(int32_t* maxDurationMs) override;
    ndk::ScopedAStatus getPwleV2PrimitiveDurationMinMillis(int32_t* minDurationMs) override;
    ndk::ScopedAStatus getPwleV2CompositionSizeMax(int32_t* maxSize) override;
    ndk::ScopedAStatus composePwleV2(const CompositePwleV2& composite,
                                     const std::shared_ptr<IVibratorCallback>& callback) override;

  private:
    // One segment of a vibration: motor on or off for durationMs.
    struct Step {
        bool on;
        int32_t durationMs;
    };
    struct Job {
        std::vector<Step> steps;
        std::shared_ptr<IVibratorCallback> callback;
    };

    ndk::ScopedAStatus start(std::vector<Step> steps,
                             const std::shared_ptr<IVibratorCallback>& callback);
    bool openDeviceLocked();
    bool setMotorLocked(bool on);
    void worker();

    // Guards everything below except mThread.
    std::mutex mMutex;
    std::condition_variable mCv;
    ::android::base::unique_fd mFd;
    int16_t mEffectId = -1;
    // Bumped by every on/perform/off: a running job sees it change and stops.
    uint64_t mGeneration = 0;
    std::optional<Job> mPending;
    bool mExit = false;
    std::thread mThread;
};

}  // namespace aidl::android::hardware::vibrator
