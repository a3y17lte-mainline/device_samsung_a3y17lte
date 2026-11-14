// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "vibrator-a3y17lte"

#include "Vibrator.h"

#include <android-base/logging.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>

namespace aidl::android::hardware::vibrator {

namespace {

constexpr const char* kInputDir = "/dev/input";
constexpr const char* kDeviceName = "regulator-haptic";

// Effect lengths for an ERM motor at a fixed voltage. A motor this size barely spins up in less
// than ~10 ms. Tuned by feel on the phone.
constexpr int32_t kTickMs = 10;
constexpr int32_t kClickMs = 20;
constexpr int32_t kHeavyClickMs = 30;
constexpr int32_t kDoubleClickGapMs = 100;

ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

bool testBit(const unsigned long* bits, int bit) {
    constexpr int kLongBits = sizeof(long) * 8;
    return bits[bit / kLongBits] & (1UL << (bit % kLongBits));
}

int32_t scale(int32_t ms, EffectStrength strength) {
    switch (strength) {
        case EffectStrength::LIGHT:
            return ms * 3 / 4;
        case EffectStrength::STRONG:
            return ms * 5 / 4;
        default:
            return ms;
    }
}

}  // namespace

Vibrator::Vibrator() : mThread(&Vibrator::worker, this) {}

Vibrator::~Vibrator() {
    {
        std::lock_guard lock(mMutex);
        mExit = true;
        mGeneration++;
    }
    mCv.notify_all();
    mThread.join();
}

// The module is loaded by modalias and the event number changes between boots: look the
// device up by name on first use, and again after an error.
bool Vibrator::openDeviceLocked() {
    if (mFd.ok()) return true;

    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kInputDir), closedir);
    if (!dir) {
        PLOG(ERROR) << "opendir " << kInputDir;
        return false;
    }
    while (struct dirent* ent = readdir(dir.get())) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        std::string path = std::string(kInputDir) + "/" + ent->d_name;
        ::android::base::unique_fd fd(open(path.c_str(), O_RDWR | O_CLOEXEC));
        if (!fd.ok()) continue;

        char name[64] = {};
        if (ioctl(fd.get(), EVIOCGNAME(sizeof(name) - 1), name) < 0) continue;
        if (strcmp(name, kDeviceName) != 0) continue;

        unsigned long ffBits[(FF_CNT + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {};
        if (ioctl(fd.get(), EVIOCGBIT(EV_FF, sizeof(ffBits)), ffBits) < 0 ||
            !testBit(ffBits, FF_RUMBLE)) {
            LOG(ERROR) << path << ": " << kDeviceName << " without FF_RUMBLE";
            continue;
        }

        // replay.length 0: ff-memless plays the effect until it is stopped, so the timing
        // is ours. Closing the fd (service death) erases the effect and stops the motor.
        struct ff_effect effect = {};
        effect.type = FF_RUMBLE;
        effect.id = -1;
        effect.u.rumble.strong_magnitude = 0xffff;
        if (ioctl(fd.get(), EVIOCSFF, &effect) < 0) {
            PLOG(ERROR) << path << ": EVIOCSFF";
            continue;
        }

        LOG(INFO) << "using " << path;
        mFd = std::move(fd);
        mEffectId = effect.id;
        return true;
    }
    LOG(ERROR) << "no " << kDeviceName << " input device";
    return false;
}

bool Vibrator::setMotorLocked(bool on) {
    if (!openDeviceLocked()) return false;

    struct input_event ev = {};
    ev.type = EV_FF;
    ev.code = mEffectId;
    ev.value = on ? 1 : 0;
    if (write(mFd.get(), &ev, sizeof(ev)) != sizeof(ev)) {
        PLOG(ERROR) << "EV_FF write";
        mFd.reset();
        mEffectId = -1;
        return false;
    }
    return true;
}

void Vibrator::worker() {
    std::unique_lock lock(mMutex);
    for (;;) {
        mCv.wait(lock, [this]() { return mExit || mPending.has_value(); });
        if (mExit) break;

        Job job = std::move(*mPending);
        mPending.reset();
        const uint64_t generation = mGeneration;

        bool cancelled = false;
        for (const Step& step : job.steps) {
            setMotorLocked(step.on);
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(step.durationMs);
            if (mCv.wait_until(lock, deadline, [&]() {
                    return mGeneration != generation;
                })) {
                cancelled = true;
                break;
            }
        }
        // A cancelling off() has stopped the motor already, a new job starts it again.
        if (!cancelled) setMotorLocked(false);

        if (job.callback) {
            lock.unlock();
            (void)job.callback->onComplete();
            lock.lock();
        }
    }
    setMotorLocked(false);
}

ndk::ScopedAStatus Vibrator::start(std::vector<Step> steps,
                                   const std::shared_ptr<IVibratorCallback>& callback) {
    {
        std::lock_guard lock(mMutex);
        if (!openDeviceLocked()) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        mGeneration++;
        mPending = Job{std::move(steps), callback};
    }
    mCv.notify_all();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCapabilities(int32_t* _aidl_return) {
    *_aidl_return = IVibrator::CAP_ON_CALLBACK | IVibrator::CAP_PERFORM_CALLBACK;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::off() {
    {
        std::lock_guard lock(mMutex);
        mGeneration++;
        mPending.reset();
        setMotorLocked(false);
    }
    mCv.notify_all();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::on(int32_t timeoutMs,
                                const std::shared_ptr<IVibratorCallback>& callback) {
    if (timeoutMs <= 0) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    return start({{true, timeoutMs}}, callback);
}

ndk::ScopedAStatus Vibrator::perform(Effect effect, EffectStrength strength,
                                     const std::shared_ptr<IVibratorCallback>& callback,
                                     int32_t* _aidl_return) {
    if (strength != EffectStrength::LIGHT && strength != EffectStrength::MEDIUM &&
        strength != EffectStrength::STRONG) {
        return unsupported();
    }

    std::vector<Step> steps;
    switch (effect) {
        case Effect::TICK:
            steps = {{true, scale(kTickMs, strength)}};
            break;
        case Effect::CLICK:
            steps = {{true, scale(kClickMs, strength)}};
            break;
        case Effect::HEAVY_CLICK:
            steps = {{true, scale(kHeavyClickMs, strength)}};
            break;
        case Effect::DOUBLE_CLICK:
            steps = {{true, scale(kClickMs, strength)},
                     {false, kDoubleClickGapMs},
                     {true, scale(kClickMs, strength)}};
            break;
        default:
            return unsupported();
    }

    int32_t total = 0;
    for (const Step& step : steps) total += step.durationMs;

    auto status = start(std::move(steps), callback);
    if (status.isOk()) *_aidl_return = total;
    return status;
}

ndk::ScopedAStatus Vibrator::getSupportedEffects(std::vector<Effect>* _aidl_return) {
    *_aidl_return = {Effect::TICK, Effect::CLICK, Effect::HEAVY_CLICK, Effect::DOUBLE_CLICK};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getSupportedPrimitives(std::vector<CompositePrimitive>* supported) {
    supported->clear();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getSupportedAlwaysOnEffects(std::vector<Effect>* _aidl_return) {
    _aidl_return->clear();
    return ndk::ScopedAStatus::ok();
}

// Everything below needs a capability this motor does not have.

ndk::ScopedAStatus Vibrator::performVendorEffect(const VendorEffect&,
                                                 const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::setAmplitude(float) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::setExternalControl(bool) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getCompositionDelayMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getCompositionSizeMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPrimitiveDuration(CompositePrimitive, int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::compose(const std::vector<CompositeEffect>&,
                                     const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::alwaysOnEnable(int32_t, Effect, EffectStrength) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::alwaysOnDisable(int32_t) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getResonantFrequency(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getQFactor(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getFrequencyResolution(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getFrequencyMinimum(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getBandwidthAmplitudeMap(std::vector<float>*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwlePrimitiveDurationMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleCompositionSizeMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getSupportedBraking(std::vector<Braking>*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::composePwle(const std::vector<PrimitivePwle>&,
                                         const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getFrequencyToOutputAccelerationMap(
        std::vector<FrequencyAccelerationMapEntry>*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleV2PrimitiveDurationMaxMillis(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleV2PrimitiveDurationMinMillis(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleV2CompositionSizeMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::composePwleV2(const CompositePwleV2&,
                                           const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

}  // namespace aidl::android::hardware::vibrator
