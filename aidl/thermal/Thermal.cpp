// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "thermal-a3y17lte"

#include "Thermal.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include <dirent.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace aidl::android::hardware::thermal {

namespace {

constexpr const char* kThermalDir = "/sys/class/thermal";
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kHysteresis = 2.0f;  // C, before a severity is lowered again
constexpr auto kPollIdle = std::chrono::seconds(5);
constexpr auto kPollHot = std::chrono::seconds(1);
constexpr size_t kSeverityCount = static_cast<size_t>(ThrottlingSeverity::SHUTDOWN) + 1;

ndk::ScopedAStatus illegalArgument(const char* msg) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT, msg);
}

// Hot thresholds, indexed by ThrottlingSeverity:
//              NONE  LIGHT  MODERATE SEVERE CRITICAL EMERGENCY SHUTDOWN
// soc-thermal is the only SoC sensor (TMU): it stands in for CPU, GPU and, as a virtual skin
// sensor, SKIN, which ThermalManagerService uses for the thermal status and headroom.
// CPU/GPU SEVERE and SHUTDOWN are the kernel's passive (76 C) and critical (110 C) trips.
// The SKIN steps are starting values, to be adjusted after real use.
std::vector<float> socThresholds() {
    return {kNaN, kNaN, kNaN, 76.0f, kNaN, kNaN, 110.0f};
}

std::vector<float> skinThresholds() {
    return {kNaN, 60.0f, 66.0f, 72.0f, 76.0f, 95.0f, 105.0f};
}

std::vector<float> batteryThresholds() {
    return {kNaN, kNaN, kNaN, 50.0f, kNaN, kNaN, 60.0f};
}

// Severity for a temperature, given the previous one: rising uses the threshold, falling
// needs kHysteresis below it.
ThrottlingSeverity severityFor(const std::vector<float>& hot, float temp,
                               ThrottlingSeverity previous) {
    size_t result = 0;
    for (size_t i = 1; i < hot.size(); i++) {
        if (std::isnan(hot[i])) continue;
        float limit = hot[i];
        if (i <= static_cast<size_t>(previous)) limit -= kHysteresis;
        if (temp >= limit) result = i;
    }
    return static_cast<ThrottlingSeverity>(result);
}

std::optional<std::string> findZone(const std::string& type) {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kThermalDir), closedir);
    if (!dir) return std::nullopt;
    while (struct dirent* ent = readdir(dir.get())) {
        if (!::android::base::StartsWith(ent->d_name, "thermal_zone")) continue;
        std::string base = std::string(kThermalDir) + "/" + ent->d_name;
        std::string zoneType;
        if (!::android::base::ReadFileToString(base + "/type", &zoneType)) continue;
        if (::android::base::Trim(zoneType) == type) return base + "/temp";
    }
    return std::nullopt;
}

bool readLong(const std::string& path, int64_t* value) {
    std::string text;
    return ::android::base::ReadFileToString(path, &text) &&
           ::android::base::ParseInt(::android::base::Trim(text), value);
}

}  // namespace

Thermal::Thermal() {
    auto sensor = [](const char* name, TemperatureType type, const char* zone,
                     std::vector<float> hot) {
        Sensor s;
        s.name = name;
        s.type = type;
        s.zoneType = zone;
        s.hot = std::move(hot);
        return s;
    };
    mSensors = {
            sensor("soc", TemperatureType::CPU, "soc-thermal", socThresholds()),
            sensor("gpu", TemperatureType::GPU, "soc-thermal", socThresholds()),
            sensor("skin", TemperatureType::SKIN, "soc-thermal", skinThresholds()),
            sensor("battery", TemperatureType::BATTERY, "battery-thermal", batteryThresholds()),
    };
    for (auto& sensor : mSensors) CHECK_EQ(sensor.hot.size(), kSeverityCount);
    mThread = std::thread(&Thermal::poll, this);
}

Thermal::~Thermal() {
    {
        std::lock_guard lock(mMutex);
        mExit = true;
    }
    mCv.notify_all();
    mThread.join();
}

bool Thermal::readSensorLocked(Sensor& sensor, Temperature* out, bool update) {
    if (sensor.tempPath.empty()) {
        auto path = findZone(sensor.zoneType);
        if (!path) return false;  // driver not bound yet: try again next time
        sensor.tempPath = *path;
    }
    int64_t milliC;
    if (!readLong(sensor.tempPath, &milliC)) {
        sensor.tempPath.clear();
        return false;
    }
    float temp = milliC / 1000.0f;
    // Only the poll thread moves the stored severity, so that it sees every change and
    // notifies the callbacks.
    ThrottlingSeverity severity = severityFor(sensor.hot, temp, sensor.severity);
    if (update) sensor.severity = severity;

    out->type = sensor.type;
    out->name = sensor.name;
    out->value = temp;
    out->throttlingStatus = severity;
    return true;
}

// cpufreq-cpu0 / cpufreq-cpu4 (the two clusters) and devfreq-11400000.gpu (Mali), all cooling
// devices of soc-thermal.
std::vector<CoolingDevice> Thermal::readCoolingDevices() {
    std::vector<CoolingDevice> devices;
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kThermalDir), closedir);
    if (!dir) return devices;
    while (struct dirent* ent = readdir(dir.get())) {
        if (!::android::base::StartsWith(ent->d_name, "cooling_device")) continue;
        std::string base = std::string(kThermalDir) + "/" + ent->d_name;
        std::string type;
        int64_t state;
        if (!::android::base::ReadFileToString(base + "/type", &type) ||
            !readLong(base + "/cur_state", &state)) {
            continue;
        }
        type = ::android::base::Trim(type);

        CoolingDevice device;
        if (::android::base::StartsWith(type, "cpufreq-")) {
            device.type = CoolingType::CPU;
        } else if (::android::base::StartsWith(type, "devfreq-")) {
            device.type = CoolingType::GPU;
        } else {
            device.type = CoolingType::COMPONENT;
        }
        device.name = type;
        device.value = state;
        devices.push_back(device);
    }
    std::sort(devices.begin(), devices.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    return devices;
}

void Thermal::poll() {
    std::unique_lock lock(mMutex);
    while (!mExit) {
        std::vector<Temperature> changed;
        bool hot = false;
        for (auto& sensor : mSensors) {
            ThrottlingSeverity before = sensor.severity;
            Temperature temp;
            if (!readSensorLocked(sensor, &temp, true)) continue;
            if (temp.throttlingStatus != before) changed.push_back(temp);
            if (temp.throttlingStatus != ThrottlingSeverity::NONE) hot = true;
        }

        std::vector<CoolingDevice> cooling = readCoolingDevices();
        std::vector<CoolingDevice> coolingChanged;
        for (const auto& device : cooling) {
            bool same = false;
            for (const auto& last : mLastCooling) {
                if (last.name == device.name && last.value == device.value) same = true;
            }
            if (!same) coolingChanged.push_back(device);
        }
        mLastCooling = cooling;

        for (const auto& temp : changed) {
            LOG(INFO) << temp.name << ": " << temp.value << " C, "
                      << toString(temp.throttlingStatus);
            for (auto it = mThermalCallbacks.begin(); it != mThermalCallbacks.end();) {
                if (it->type && *it->type != temp.type) {
                    ++it;
                    continue;
                }
                // Drop callbacks whose process died.
                if (!it->callback->notifyThrottling(temp).isOk()) {
                    it = mThermalCallbacks.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (const auto& device : coolingChanged) {
            for (auto it = mCoolingCallbacks.begin(); it != mCoolingCallbacks.end();) {
                if (it->type && *it->type != device.type) {
                    ++it;
                    continue;
                }
                if (!it->callback->notifyCoolingDeviceChanged(device).isOk()) {
                    it = mCoolingCallbacks.erase(it);
                } else {
                    ++it;
                }
            }
        }

        mCv.wait_for(lock, hot ? kPollHot : kPollIdle, [this] { return mExit; });
    }
}

ndk::ScopedAStatus Thermal::getCoolingDevices(std::vector<CoolingDevice>* out) {
    *out = readCoolingDevices();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::getCoolingDevicesWithType(CoolingType type,
                                                      std::vector<CoolingDevice>* out) {
    out->clear();
    for (const auto& device : readCoolingDevices()) {
        if (device.type == type) out->push_back(device);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::getTemperatures(std::vector<Temperature>* out) {
    std::lock_guard lock(mMutex);
    out->clear();
    for (auto& sensor : mSensors) {
        Temperature temp;
        if (readSensorLocked(sensor, &temp, false)) out->push_back(temp);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::getTemperaturesWithType(TemperatureType type,
                                                    std::vector<Temperature>* out) {
    std::lock_guard lock(mMutex);
    out->clear();
    for (auto& sensor : mSensors) {
        Temperature temp;
        if (sensor.type == type && readSensorLocked(sensor, &temp, false)) {
            out->push_back(temp);
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::getTemperatureThresholds(std::vector<TemperatureThreshold>* out) {
    std::lock_guard lock(mMutex);
    out->clear();
    for (const auto& sensor : mSensors) {
        TemperatureThreshold threshold;
        threshold.type = sensor.type;
        threshold.name = sensor.name;
        threshold.hotThrottlingThresholds = sensor.hot;
        threshold.coldThrottlingThresholds = std::vector<float>(kSeverityCount, kNaN);
        out->push_back(threshold);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::getTemperatureThresholdsWithType(
        TemperatureType type, std::vector<TemperatureThreshold>* out) {
    std::vector<TemperatureThreshold> all;
    getTemperatureThresholds(&all);
    out->clear();
    for (auto& threshold : all) {
        if (threshold.type == type) out->push_back(std::move(threshold));
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::registerThermalChangedCallback(
        const std::shared_ptr<IThermalChangedCallback>& callback) {
    if (!callback) return illegalArgument("null callback");
    std::lock_guard lock(mMutex);
    for (const auto& reg : mThermalCallbacks) {
        if (reg.callback->asBinder() == callback->asBinder()) {
            return illegalArgument("callback already registered");
        }
    }
    mThermalCallbacks.push_back({callback, std::nullopt});
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::registerThermalChangedCallbackWithType(
        const std::shared_ptr<IThermalChangedCallback>& callback, TemperatureType type) {
    if (!callback) return illegalArgument("null callback");
    std::lock_guard lock(mMutex);
    for (const auto& reg : mThermalCallbacks) {
        if (reg.callback->asBinder() == callback->asBinder()) {
            return illegalArgument("callback already registered");
        }
    }
    mThermalCallbacks.push_back({callback, type});
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::unregisterThermalChangedCallback(
        const std::shared_ptr<IThermalChangedCallback>& callback) {
    if (!callback) return illegalArgument("null callback");
    std::lock_guard lock(mMutex);
    for (auto it = mThermalCallbacks.begin(); it != mThermalCallbacks.end(); ++it) {
        if (it->callback->asBinder() == callback->asBinder()) {
            mThermalCallbacks.erase(it);
            return ndk::ScopedAStatus::ok();
        }
    }
    return illegalArgument("callback not registered");
}

ndk::ScopedAStatus Thermal::registerCoolingDeviceChangedCallbackWithType(
        const std::shared_ptr<ICoolingDeviceChangedCallback>& callback, CoolingType type) {
    if (!callback) return illegalArgument("null callback");
    std::lock_guard lock(mMutex);
    for (const auto& reg : mCoolingCallbacks) {
        if (reg.callback->asBinder() == callback->asBinder()) {
            return illegalArgument("callback already registered");
        }
    }
    mCoolingCallbacks.push_back({callback, type});
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::unregisterCoolingDeviceChangedCallback(
        const std::shared_ptr<ICoolingDeviceChangedCallback>& callback) {
    if (!callback) return illegalArgument("null callback");
    std::lock_guard lock(mMutex);
    for (auto it = mCoolingCallbacks.begin(); it != mCoolingCallbacks.end(); ++it) {
        if (it->callback->asBinder() == callback->asBinder()) {
            mCoolingCallbacks.erase(it);
            return ndk::ScopedAStatus::ok();
        }
    }
    return illegalArgument("callback not registered");
}

// No skin model to forecast with.
ndk::ScopedAStatus Thermal::forecastSkinTemperature(int32_t, float*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::thermal
