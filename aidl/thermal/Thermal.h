// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/thermal/BnThermal.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace aidl::android::hardware::thermal {

// Thermal HAL over the kernel thermal zones (/sys/class/thermal). The kernel does the
// throttling itself (soc-thermal trips cool the CPU clusters and the GPU); this HAL reports
// temperatures, severities and cooling states to the framework.
class Thermal : public BnThermal {
  public:
    Thermal();
    ~Thermal();

    ndk::ScopedAStatus getCoolingDevices(std::vector<CoolingDevice>* out) override;
    ndk::ScopedAStatus getCoolingDevicesWithType(CoolingType type,
                                                 std::vector<CoolingDevice>* out) override;
    ndk::ScopedAStatus getTemperatures(std::vector<Temperature>* out) override;
    ndk::ScopedAStatus getTemperaturesWithType(TemperatureType type,
                                               std::vector<Temperature>* out) override;
    ndk::ScopedAStatus getTemperatureThresholds(std::vector<TemperatureThreshold>* out) override;
    ndk::ScopedAStatus getTemperatureThresholdsWithType(
            TemperatureType type, std::vector<TemperatureThreshold>* out) override;
    ndk::ScopedAStatus registerThermalChangedCallback(
            const std::shared_ptr<IThermalChangedCallback>& callback) override;
    ndk::ScopedAStatus registerThermalChangedCallbackWithType(
            const std::shared_ptr<IThermalChangedCallback>& callback,
            TemperatureType type) override;
    ndk::ScopedAStatus unregisterThermalChangedCallback(
            const std::shared_ptr<IThermalChangedCallback>& callback) override;
    ndk::ScopedAStatus registerCoolingDeviceChangedCallbackWithType(
            const std::shared_ptr<ICoolingDeviceChangedCallback>& callback,
            CoolingType type) override;
    ndk::ScopedAStatus unregisterCoolingDeviceChangedCallback(
            const std::shared_ptr<ICoolingDeviceChangedCallback>& callback) override;
    ndk::ScopedAStatus forecastSkinTemperature(int32_t forecastSeconds,
                                               float* _aidl_return) override;

  private:
    struct Sensor {
        std::string name;
        TemperatureType type;
        std::string zoneType;          // thermal zone "type" in sysfs
        std::vector<float> hot;        // indexed by ThrottlingSeverity, NaN = unused
        std::string tempPath;          // resolved lazily: zone numbers change between boots
        ThrottlingSeverity severity = ThrottlingSeverity::NONE;
    };
    template <typename Cb, typename Type>
    struct Registration {
        std::shared_ptr<Cb> callback;
        std::optional<Type> type;
    };

    bool readSensorLocked(Sensor& sensor, Temperature* out, bool update);
    std::vector<CoolingDevice> readCoolingDevices();
    void poll();

    std::mutex mMutex;  // guards everything below except mThread
    std::condition_variable mCv;
    std::vector<Sensor> mSensors;
    std::vector<Registration<IThermalChangedCallback, TemperatureType>> mThermalCallbacks;
    std::vector<Registration<ICoolingDeviceChangedCallback, CoolingType>> mCoolingCallbacks;
    std::vector<CoolingDevice> mLastCooling;
    bool mExit = false;
    std::thread mThread;
};

}  // namespace aidl::android::hardware::thermal
