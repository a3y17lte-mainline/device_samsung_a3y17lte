// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "PowerHAL_A3"

#include "Power.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include <algorithm>
#include <string>

namespace aidl::android::hardware::power::a3y17lte {

using namespace std::chrono_literals;
using ndk::ScopedAStatus;

namespace {

// Both clusters are the same cores with the same OPP table: one set of numbers for both.
constexpr const char* kPolicies[] = {
        "/sys/devices/system/cpu/cpufreq/policy0",
        "/sys/devices/system/cpu/cpufreq/policy4",
};

constexpr int kBoostFloorKhz = 1000000;  // boost to the first frequency at or above this

// The GPU (Panfrost devfreq, simple_ondemand). Since the display planes took composition off
// the GPU, an app's own drawing is too little load to raise the frequency, and its frames get
// slow. Measured with a fixed minimum (list scroll in Settings, three runs each; janky frames,
// 95th percentile frame time, energy per frame by the OPPs' voltage squared):
//   343 MHz 15.3% 57 ms 1.00    545 MHz 13.4% 49 ms 1.00    676 MHz 11.0% 55 ms 1.17
//   728 MHz 10.4% 59 ms 1.36    845 MHz  6.3% 34 ms 1.52   (1001 MHz, one run: 7.3%, 1.80)
// Flings over a long web page only react to 845 MHz too (median 28 -> 21 ms). So:
//   - always: 545 MHz, the highest frequency at the lowest voltage: the same work for the same
//     energy, only sooner;
//   - interaction boosts: 845 MHz, the one step that removes the slow frames;
//   - launches: the highest.
// The launcher's drawer, the notification shade and cold starts did not react to any of it.
// Frequencies in Hz.
constexpr const char* kGpu = "/sys/class/devfreq/11400000.gpu";
constexpr int kGpuBaseCeilHz = 545000000;    // idle: the last frequency at or below this
constexpr int kGpuBoostFloorHz = 800000000;  // boost: the first frequency at or above this
constexpr int kDefaultBoostMs = 100;
constexpr int kMaxBoostMs = 2000;
constexpr auto kLaunchMax = 5s;  // in case the framework never ends a launch

bool isSupported(Mode mode) {
    switch (mode) {
        case Mode::LOW_POWER:
        case Mode::LAUNCH:
        case Mode::INTERACTIVE:
        case Mode::DEVICE_IDLE:
            return true;
        default:
            return false;
    }
}

bool isSupported(Boost boost) {
    return boost == Boost::INTERACTION;
}

template <class E>
int64_t supportMask() {
    int64_t mask = 0;
    for (E value : ndk::enum_range<E>()) {
        if (isSupported(value)) mask |= INT64_C(1) << static_cast<int>(value);
    }
    return mask;
}

void writeLimit(const char* file, int khz) {
    for (const char* policy : kPolicies) {
        const std::string path = std::string(policy) + "/" + file;
        if (!::android::base::WriteStringToFile(std::to_string(khz), path)) {
            PLOG(WARNING) << "write " << khz << " to " << path;
        }
    }
}

}  // namespace

Power::Power() : mThread(&Power::run, this) {}

Power::~Power() {
    {
        std::lock_guard lock(mLock);
        mStop = true;
    }
    mCv.notify_all();
    mThread.join();
}

// cpufreq-dt registers after the regulators: the table may not be there at the first hint.
bool Power::loadFrequenciesLocked() {
    if (mMaxKhz != 0) return true;

    std::string text;
    if (!::android::base::ReadFileToString(
                std::string(kPolicies[0]) + "/scaling_available_frequencies", &text)) {
        return false;
    }
    std::vector<int> table;
    for (const std::string& word : ::android::base::Tokenize(text, " \n")) {
        int khz;
        if (::android::base::ParseInt(word, &khz) && khz > 0) table.push_back(khz);
    }
    if (table.empty()) return false;
    std::sort(table.begin(), table.end());

    mMinKhz = table.front();
    mMaxKhz = table.back();
    auto boost = std::lower_bound(table.begin(), table.end(), kBoostFloorKhz);
    mBoostKhz = boost != table.end() ? *boost : mMaxKhz;
    LOG(INFO) << "cpufreq " << mMinKhz << "-" << mMaxKhz << " kHz, boost " << mBoostKhz;
    return true;
}

// The Panfrost module may load after the first hint, like cpufreq-dt.
bool Power::loadGpuFrequenciesLocked() {
    if (mGpuMaxHz != 0) return true;

    std::string text;
    if (!::android::base::ReadFileToString(std::string(kGpu) + "/available_frequencies", &text)) {
        return false;
    }
    std::vector<int> table;
    for (const std::string& word : ::android::base::Tokenize(text, " \n")) {
        int hz;
        if (::android::base::ParseInt(word, &hz) && hz > 0) table.push_back(hz);
    }
    if (table.empty()) return false;
    std::sort(table.begin(), table.end());

    mGpuMaxHz = table.back();
    auto base = std::upper_bound(table.begin(), table.end(), kGpuBaseCeilHz);
    mGpuBaseHz = base != table.begin() ? *(base - 1) : table.front();
    auto boost = std::lower_bound(table.begin(), table.end(), kGpuBoostFloorHz);
    mGpuBoostHz = boost != table.end() ? *boost : mGpuMaxHz;
    LOG(INFO) << "GPU minimum " << mGpuBaseHz << " Hz, boost " << mGpuBoostHz << ", launch "
              << mGpuMaxHz;
    return true;
}

void Power::applyLocked(Clock::time_point now) {
    const bool boosts = mInteractive && !mDeviceIdle && !mLowPower;
    const bool launch = boosts && mLaunch && now < mLaunchEnd;
    const bool boost = boosts && now < mBoostEnd;

    if (loadGpuFrequenciesLocked()) {
        const int gpuMin = launch ? mGpuMaxHz : boost ? mGpuBoostHz : mGpuBaseHz;
        if (gpuMin != mWrittenGpuMin) {
            const std::string path = std::string(kGpu) + "/min_freq";
            if (!::android::base::WriteStringToFile(std::to_string(gpuMin), path)) {
                PLOG(WARNING) << "write " << gpuMin << " to " << path;
            }
            mWrittenGpuMin = gpuMin;
        }
    }

    if (!loadFrequenciesLocked()) return;

    int min = mMinKhz;
    if (launch) {
        min = mMaxKhz;
    } else if (boost) {
        min = mBoostKhz;
    }
    const int max = mLowPower ? mBoostKhz : mMaxKhz;

    // The kernel rejects min > max: raise the maximum first, lower it last.
    if (max != mWrittenMax && max > mWrittenMax) writeLimit("scaling_max_freq", max);
    if (min != mWrittenMin) writeLimit("scaling_min_freq", min);
    if (max != mWrittenMax && max < mWrittenMax) writeLimit("scaling_max_freq", max);
    mWrittenMin = min;
    mWrittenMax = max;
}

// Applies the state on every change and when a boost or launch runs out.
void Power::run() {
    std::unique_lock lock(mLock);
    while (!mStop) {
        const auto now = Clock::now();
        applyLocked(now);

        auto next = Clock::time_point::max();
        if (mLaunch && now < mLaunchEnd) next = std::min(next, mLaunchEnd);
        if (now < mBoostEnd) next = std::min(next, mBoostEnd);
        if (next == Clock::time_point::max()) {
            mCv.wait(lock);
        } else {
            mCv.wait_until(lock, next);
        }
    }
}

ScopedAStatus Power::setMode(Mode type, bool enabled) {
    {
        std::lock_guard lock(mLock);
        switch (type) {
            case Mode::LOW_POWER:
                mLowPower = enabled;
                break;
            case Mode::LAUNCH:
                mLaunch = enabled;
                mLaunchEnd = Clock::now() + kLaunchMax;
                break;
            case Mode::INTERACTIVE:
                mInteractive = enabled;
                break;
            case Mode::DEVICE_IDLE:
                mDeviceIdle = enabled;
                break;
            default:
                return ScopedAStatus::ok();
        }
    }
    mCv.notify_all();
    return ScopedAStatus::ok();
}

ScopedAStatus Power::isModeSupported(Mode type, bool* _aidl_return) {
    *_aidl_return = isSupported(type);
    return ScopedAStatus::ok();
}

ScopedAStatus Power::setBoost(Boost type, int32_t durationMs) {
    if (!isSupported(type)) return ScopedAStatus::ok();
    if (durationMs <= 0) durationMs = kDefaultBoostMs;
    const auto end = Clock::now() + std::chrono::milliseconds(std::min(durationMs, kMaxBoostMs));
    {
        std::lock_guard lock(mLock);
        if (end <= mBoostEnd) return ScopedAStatus::ok();
        mBoostEnd = end;
    }
    mCv.notify_all();
    return ScopedAStatus::ok();
}

ScopedAStatus Power::isBoostSupported(Boost type, bool* _aidl_return) {
    *_aidl_return = isSupported(type);
    return ScopedAStatus::ok();
}

ScopedAStatus Power::createHintSession(int32_t, int32_t, const std::vector<int32_t>&, int64_t,
                                       std::shared_ptr<IPowerHintSession>* _aidl_return) {
    *_aidl_return = nullptr;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::createHintSessionWithConfig(int32_t, int32_t, const std::vector<int32_t>&,
                                                 int64_t, SessionTag, SessionConfig*,
                                                 std::shared_ptr<IPowerHintSession>* _aidl_return) {
    *_aidl_return = nullptr;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::getHintSessionPreferredRate(int64_t* outNanoseconds) {
    *outNanoseconds = -1;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::getSessionChannel(int32_t, int32_t, ChannelConfig*) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::closeSessionChannel(int32_t, int32_t) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::getSupportInfo(SupportInfo* _aidl_return) {
    SupportInfo info;
    info.usesSessions = false;
    info.boosts = supportMask<Boost>();
    info.modes = supportMask<Mode>();
    info.sessionHints = 0;
    info.sessionModes = 0;
    info.sessionTags = 0;
    info.compositionData.isSupported = false;
    info.compositionData.disableGpuFences = false;
    info.compositionData.maxBatchSize = 1;
    info.compositionData.alwaysBatch = false;
    info.headroom.isCpuSupported = false;
    info.headroom.isGpuSupported = false;
    info.headroom.cpuMinIntervalMillis = 0;
    info.headroom.gpuMinIntervalMillis = 0;
    *_aidl_return = info;
    return ScopedAStatus::ok();
}

ScopedAStatus Power::getCpuHeadroom(const CpuHeadroomParams&, CpuHeadroomResult*) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::getGpuHeadroom(const GpuHeadroomParams&, GpuHeadroomResult*) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::sendCompositionData(const std::vector<CompositionData>&) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::sendCompositionUpdate(const CompositionUpdate&) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::power::a3y17lte
