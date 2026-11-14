// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/sensors/BnSensors.h>
#include <android-base/unique_fd.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aidl {
namespace android {
namespace hardware {
namespace sensors {

class ISensorsEventCallback {
  public:
    using Event = ::aidl::android::hardware::sensors::Event;

    virtual ~ISensorsEventCallback(){};
    virtual void postEvents(const std::vector<Event>& events, bool wakeup) = 0;
};

// One sensor of the STM32 sensor hub: an IIO device of the kernel's ssp_sensors drivers, found
// by its name (device numbers change between boots, and the drivers are modules). Each sensor
// has a thread that owns the device while the sensor is enabled.
class Sensor {
  public:
    using OperationMode = ::aidl::android::hardware::sensors::ISensors::OperationMode;
    using Event = ::aidl::android::hardware::sensors::Event;
    using EventPayload = ::aidl::android::hardware::sensors::Event::EventPayload;
    using SensorInfo = ::aidl::android::hardware::sensors::SensorInfo;
    using SensorType = ::aidl::android::hardware::sensors::SensorType;

    virtual ~Sensor();

    const SensorInfo& getSensorInfo() const { return mSensorInfo; }
    void batch(int64_t samplingPeriodNs);
    void activate(bool enable);
    ndk::ScopedAStatus flush();

    // No data injection.
    void setOperationMode(OperationMode) {}
    ndk::ScopedAStatus injectEvent(const Event& event);

  protected:
    Sensor(ISensorsEventCallback* callback, const char* iioName);
    void startThread();  // from the constructor of the final class
    void stopThread();   // from the destructor of the final class

    // Called on the sensor's thread, without the lock.
    virtual bool startDevice() = 0;  // false: not there (yet), tried again later
    virtual void stopDevice() = 0;
    virtual void setPeriod(int64_t periodNs) = 0;
    virtual void readDevice() = 0;  // the device fd is readable

    bool findDevice();  // sets mSysfs and mDevNode
    bool writeAttr(const std::string& attr, const std::string& value);
    bool readAttr(const std::string& attr, std::string* value);
    bool useBoottime();  // device timestamps as CLOCK_BOOTTIME, what Android wants
    bool isWakeUpSensor() const;
    void post(const std::vector<Event>& events);

    SensorInfo mSensorInfo;
    const char* const mIioName;
    std::string mSysfs;    // /sys/bus/iio/devices/iio:deviceN
    std::string mDevNode;  // /dev/iio:deviceN
    ::android::base::unique_fd mFd;  // what the thread polls

  private:
    void run();

    ISensorsEventCallback* const mCallback;
    std::mutex mLock;
    std::condition_variable mCv;
    std::thread mThread;
    ::android::base::unique_fd mWakeFd;  // eventfd: state changed, leave poll()
    bool mStop = false;
    bool mEnabled = false;
    int64_t mPeriodNs = 0;
};

struct SensorSpec {
    const char* iioName;
    ::aidl::android::hardware::sensors::SensorType type;
    const char* name;
    const char* vendor;
    std::vector<const char*> channels;  // scan elements, 1 (scalar) or 3 (x, y, z)
    const char* scaleAttr;              // nullptr: the values are final
    float unitFactor;                   // IIO unit -> Android unit
    const char* frequencyAttr;
    float maxRange;    // Android unit
    float resolution;  // Android unit
    float power;       // mA
    int32_t minDelayUs;
    bool onChange;
    ::aidl::android::hardware::sensors::SensorStatus status;
};

extern const std::vector<SensorSpec> kBufferSensors;

// A sensor read from its IIO buffer (kfifo). The drivers always push whole samples, so every
// scan element is enabled; the record layout is read from scan_elements.
class BufferSensor final : public Sensor {
  public:
    BufferSensor(int32_t sensorHandle, ISensorsEventCallback* callback, const SensorSpec& spec);
    ~BufferSensor() override;

  protected:
    bool startDevice() override;
    void stopDevice() override;
    void setPeriod(int64_t periodNs) override;
    void readDevice() override;

  private:
    struct Element {
        std::string name;
        int index = 0;
        bool isSigned = false;
        unsigned bits = 0;   // valid bits
        unsigned bytes = 0;  // storage size
        unsigned shift = 0;
        size_t offset = 0;
    };

    bool readLayout();
    int64_t value(const Element& element, const uint8_t* record) const;

    const SensorSpec mSpec;
    std::vector<Element> mValues;  // mSpec.channels, in that order
    Element mTimestamp;
    size_t mRecordSize = 0;
    float mScale = 1.0f;
    bool mBoottime = false;
    int64_t mLastTimestamp = 0;
    bool mHavePrevious = false;  // on-change: the last value that was reported
    float mPrevious = 0.0f;
};

// The proximity sensor reports near/far through IIO threshold events: the hub decides (the
// thresholds are in the board's DT), the kernel sends rising = near, falling = far, and the
// current state right after the events are enabled. A wake-up sensor.
class ProximitySensor final : public Sensor {
  public:
    ProximitySensor(int32_t sensorHandle, ISensorsEventCallback* callback);
    ~ProximitySensor() override;

  protected:
    bool startDevice() override;
    void stopDevice() override;
    void setPeriod(int64_t) override {}
    void readDevice() override;

  private:
    bool mBoottime = false;
};

}  // namespace sensors
}  // namespace hardware
}  // namespace android
}  // namespace aidl
