// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "SensorsHAL_A3"

#include "Sensor.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <utils/SystemClock.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/iio/events.h>
#include <linux/iio/types.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using ::aidl::android::hardware::sensors::SensorStatus;
using ::android::base::unique_fd;
using ::ndk::ScopedAStatus;

namespace aidl {
namespace android {
namespace hardware {
namespace sensors {

namespace {

constexpr const char* kIioDevices = "/sys/bus/iio/devices";

// The hub takes its delay in ms, 200 ms at the most.
constexpr int32_t kMaxDelayUs = 200 * 1000;
constexpr int64_t kDefaultPeriodNs = 200 * 1000 * 1000LL;

constexpr float kGravity = 9.80665f;
constexpr float kPi = 3.14159265f;

constexpr float kProximityNearCm = 0.0f;
constexpr float kProximityFarCm = 5.0f;

}  // namespace

// Units: IIO has m/s^2, rad/s, Gauss, kPa and lux; Android wants uT and hPa.
// accel/gyro: ST LSM6DSL, +-8 g and +-1000 dps. mag: AK09916C; the hub calibrates it itself
// once it has seen some motion and does not pass its accuracy on. pressure: LPS25H, which the
// hub reads every 180 ms whatever is asked for. light: TMD3725, lux computed on the hub.
const std::vector<SensorSpec> kBufferSensors = {
        {"ssp-accelerometer", SensorType::ACCELEROMETER, "LSM6DSL Accelerometer",
         "STMicroelectronics", {"in_accel_x", "in_accel_y", "in_accel_z"}, "in_accel_scale", 1.0f,
         "in_accel_sampling_frequency", 8 * kGravity, kGravity / 4096, 0.15f, 10000, false,
         SensorStatus::ACCURACY_HIGH},
        {"ssp-gyroscope", SensorType::GYROSCOPE, "LSM6DSL Gyroscope", "STMicroelectronics",
         {"in_anglvel_x", "in_anglvel_y", "in_anglvel_z"}, "in_anglvel_scale", 1.0f,
         "in_anglvel_sampling_frequency", 1000 * kPi / 180, 0.035f * kPi / 180, 0.55f, 10000,
         false, SensorStatus::ACCURACY_HIGH},
        {"ssp-magnetometer", SensorType::MAGNETIC_FIELD, "AK09916C Magnetometer",
         "Asahi Kasei Microdevices", {"in_magn_x", "in_magn_y", "in_magn_z"}, "in_magn_scale",
         100.0f, "sampling_frequency", 4912.0f, 0.06f, 1.1f, 10000, false,
         SensorStatus::ACCURACY_MEDIUM},
        {"ssp-pressure", SensorType::PRESSURE, "LPS25H Barometer", "STMicroelectronics",
         {"in_pressure"}, "in_pressure_scale", 10.0f, "sampling_frequency", 1260.0f,
         1.0f / 4096, 0.025f, 180000, false, SensorStatus::ACCURACY_HIGH},
        {"ssp-light", SensorType::LIGHT, "TMD3725 Light", "ams", {"in_illuminance"}, nullptr,
         1.0f, "sampling_frequency", 60000.0f, 1.0f, 0.1f, 0, true, SensorStatus::ACCURACY_HIGH},
};

Sensor::Sensor(ISensorsEventCallback* callback, const char* iioName)
    : mIioName(iioName), mCallback(callback), mWakeFd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
    mSensorInfo.version = 1;
    mSensorInfo.typeAsString = "";
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 0;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.maxDelayUs = kMaxDelayUs;
    mSensorInfo.flags = 0;
}

Sensor::~Sensor() {}

void Sensor::startThread() {
    mThread = std::thread(&Sensor::run, this);
}

void Sensor::stopThread() {
    {
        std::lock_guard lock(mLock);
        mStop = true;
    }
    mCv.notify_all();
    eventfd_write(mWakeFd.get(), 1);
    if (mThread.joinable()) mThread.join();
}

void Sensor::batch(int64_t samplingPeriodNs) {
    samplingPeriodNs = std::clamp<int64_t>(samplingPeriodNs,
                                           std::max(mSensorInfo.minDelayUs, 10000) * 1000LL,
                                           mSensorInfo.maxDelayUs * 1000LL);
    {
        std::lock_guard lock(mLock);
        if (mPeriodNs == samplingPeriodNs) return;
        mPeriodNs = samplingPeriodNs;
    }
    mCv.notify_all();
    eventfd_write(mWakeFd.get(), 1);
}

void Sensor::activate(bool enable) {
    {
        std::lock_guard lock(mLock);
        if (mEnabled == enable) return;
        mEnabled = enable;
    }
    mCv.notify_all();
    eventfd_write(mWakeFd.get(), 1);
}

ScopedAStatus Sensor::flush() {
    {
        std::lock_guard lock(mLock);
        if (!mEnabled) {
            return ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(BnSensors::ERROR_BAD_VALUE));
        }
    }
    // Nothing is batched: every sample is posted as it arrives.
    Event event;
    event.sensorHandle = mSensorInfo.sensorHandle;
    event.sensorType = SensorType::META_DATA;
    event.timestamp = 0;
    EventPayload::MetaData meta = {
            .what = EventPayload::MetaData::MetaDataEventType::META_DATA_FLUSH_COMPLETE,
    };
    event.payload.set<EventPayload::Tag::meta>(meta);
    post({event});
    return ScopedAStatus::ok();
}

ScopedAStatus Sensor::injectEvent(const Event& event) {
    if (event.sensorType == SensorType::ADDITIONAL_INFO) return ScopedAStatus::ok();
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

bool Sensor::isWakeUpSensor() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_WAKE_UP);
}

void Sensor::post(const std::vector<Event>& events) {
    if (!events.empty()) mCallback->postEvents(events, isWakeUpSensor());
}

bool Sensor::findDevice() {
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(kIioDevices), closedir);
    if (!dir) return false;
    while (dirent* entry = readdir(dir.get())) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0) continue;
        const std::string sysfs = std::string(kIioDevices) + "/" + entry->d_name;
        std::string name;
        if (!::android::base::ReadFileToString(sysfs + "/name", &name)) continue;
        if (::android::base::Trim(name) != mIioName) continue;
        mSysfs = sysfs;
        mDevNode = std::string("/dev/") + entry->d_name;
        return true;
    }
    return false;
}

bool Sensor::writeAttr(const std::string& attr, const std::string& value) {
    const std::string path = mSysfs + "/" + attr;
    if (::android::base::WriteStringToFile(value, path)) return true;
    PLOG(ERROR) << mIioName << ": write '" << value << "' to " << path;
    return false;
}

bool Sensor::readAttr(const std::string& attr, std::string* value) {
    if (!::android::base::ReadFileToString(mSysfs + "/" + attr, value)) return false;
    *value = ::android::base::Trim(*value);
    return true;
}

bool Sensor::useBoottime() {
    std::string clock;
    writeAttr("current_timestamp_clock", "boottime");
    return readAttr("current_timestamp_clock", &clock) && clock == "boottime";
}

void Sensor::run() {
    bool running = false;
    int64_t appliedPeriodNs = 0;

    std::unique_lock lock(mLock);
    while (!mStop) {
        if (!mEnabled) {
            if (running) {
                lock.unlock();
                stopDevice();
                lock.lock();
                running = false;
                continue;
            }
            mCv.wait(lock);
            continue;
        }

        const int64_t periodNs = mPeriodNs != 0 ? mPeriodNs : kDefaultPeriodNs;
        lock.unlock();
        if (!running) {
            running = startDevice();
            appliedPeriodNs = 0;
            if (!running) {
                // The hub's modules are not loaded yet, or the hub is being reset.
                stopDevice();
                lock.lock();
                mCv.wait_for(lock, std::chrono::seconds(1));
                continue;
            }
        }
        if (appliedPeriodNs != periodNs) {
            setPeriod(periodNs);
            appliedPeriodNs = periodNs;
        }

        pollfd fds[2] = {{mFd.get(), POLLIN, 0}, {mWakeFd.get(), POLLIN, 0}};
        if (TEMP_FAILURE_RETRY(poll(fds, 2, -1)) > 0) {
            if (fds[1].revents & POLLIN) {
                eventfd_t count;
                eventfd_read(mWakeFd.get(), &count);
            }
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                // The device went away (module unloaded): look for it again.
                LOG(WARNING) << mIioName << ": device lost";
                stopDevice();
                running = false;
            } else if (fds[0].revents & POLLIN) {
                readDevice();
            }
        }
        lock.lock();
    }
    if (running) {
        lock.unlock();
        stopDevice();
    }
}

BufferSensor::BufferSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                           const SensorSpec& spec)
    : Sensor(callback, spec.iioName), mSpec(spec) {
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.name = spec.name;
    mSensorInfo.vendor = spec.vendor;
    mSensorInfo.type = spec.type;
    mSensorInfo.maxRange = spec.maxRange;
    mSensorInfo.resolution = spec.resolution;
    mSensorInfo.power = spec.power;
    mSensorInfo.minDelayUs = spec.minDelayUs;
    if (spec.onChange) {
        mSensorInfo.flags |= static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE);
    }
    startThread();
}

BufferSensor::~BufferSensor() {
    stopThread();
}

// scan_elements/<name>_index and <name>_type ("le:s16/16>>0") of every element give the
// record: elements in index order, each aligned to its own size.
bool BufferSensor::readLayout() {
    const std::string scanDir = mSysfs + "/scan_elements";
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(scanDir.c_str()), closedir);
    if (!dir) return false;

    std::vector<Element> elements;
    while (dirent* entry = readdir(dir.get())) {
        const std::string file = entry->d_name;
        if (!::android::base::EndsWith(file, "_type")) continue;

        Element element;
        element.name = file.substr(0, file.size() - 5);
        std::string type, index;
        if (!readAttr("scan_elements/" + file, &type) ||
            !readAttr("scan_elements/" + element.name + "_index", &index) ||
            !::android::base::ParseInt(index, &element.index)) {
            return false;
        }
        char endian[3], sign;
        unsigned storageBits;
        if (sscanf(type.c_str(), "%2[a-z]:%c%u/%u>>%u", endian, &sign, &element.bits,
                   &storageBits, &element.shift) != 5 ||
            strcmp(endian, "le") != 0 || storageBits % 8 != 0 || storageBits > 64 ||
            element.bits == 0 || element.bits > storageBits) {
            LOG(ERROR) << mIioName << ": unsupported scan type " << type;
            return false;
        }
        element.isSigned = sign == 's';
        element.bytes = storageBits / 8;
        elements.push_back(element);
    }
    std::sort(elements.begin(), elements.end(),
              [](const Element& a, const Element& b) { return a.index < b.index; });

    size_t offset = 0, align = 1;
    for (Element& element : elements) {
        offset = (offset + element.bytes - 1) / element.bytes * element.bytes;
        element.offset = offset;
        offset += element.bytes;
        align = std::max<size_t>(align, element.bytes);
    }
    mRecordSize = (offset + align - 1) / align * align;

    auto find = [&](const std::string& name, Element* out) {
        for (const Element& element : elements) {
            if (element.name == name) {
                *out = element;
                return true;
            }
        }
        LOG(ERROR) << mIioName << ": no scan element " << name;
        return false;
    };
    mValues.clear();
    for (const char* channel : mSpec.channels) {
        Element element;
        if (!find(channel, &element)) return false;
        mValues.push_back(element);
    }
    return find("in_timestamp", &mTimestamp);
}

int64_t BufferSensor::value(const Element& element, const uint8_t* record) const {
    uint64_t raw = 0;
    memcpy(&raw, record + element.offset, element.bytes);  // little endian, like the CPU
    raw >>= element.shift;
    if (element.bits < 64) {
        raw &= (UINT64_C(1) << element.bits) - 1;
        if (element.isSigned && (raw & (UINT64_C(1) << (element.bits - 1)))) {
            raw |= ~((UINT64_C(1) << element.bits) - 1);
        }
    }
    return static_cast<int64_t>(raw);
}

bool BufferSensor::startDevice() {
    if (!findDevice() || !readLayout()) return false;

    mScale = mSpec.unitFactor;
    if (mSpec.scaleAttr != nullptr) {
        std::string scale;
        if (!readAttr(mSpec.scaleAttr, &scale)) return false;
        mScale *= strtof(scale.c_str(), nullptr);
    }

    // A buffer left running by a HAL that died: its scan elements cannot be changed.
    ::android::base::WriteStringToFile("0", mSysfs + "/buffer/enable");
    mBoottime = useBoottime();

    // The drivers push whole samples (one fixed scan mask), so every element is switched on
    // and the record is the full layout.
    {
        std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir((mSysfs + "/scan_elements").c_str()),
                                                closedir);
        if (!dir) return false;
        while (dirent* entry = readdir(dir.get())) {
            const std::string file = entry->d_name;
            if (!::android::base::EndsWith(file, "_en")) continue;
            if (!writeAttr("scan_elements/" + file, "1")) return false;
        }
    }

    writeAttr("buffer/length", "64");

    mFd.reset(open(mDevNode.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    if (mFd.get() < 0) {
        PLOG(ERROR) << mIioName << ": open " << mDevNode;
        return false;
    }
    if (!writeAttr("buffer/enable", "1")) return false;

    mLastTimestamp = 0;
    mHavePrevious = false;
    return true;
}

void BufferSensor::stopDevice() {
    if (!mSysfs.empty()) ::android::base::WriteStringToFile("0", mSysfs + "/buffer/enable");
    mFd.reset();
}

void BufferSensor::setPeriod(int64_t periodNs) {
    char hz[32];
    snprintf(hz, sizeof(hz), "%.6f", 1e9 / periodNs);
    writeAttr(mSpec.frequencyAttr, hz);
}

void BufferSensor::readDevice() {
    uint8_t buffer[64 * 16];
    const size_t wanted = sizeof(buffer) / mRecordSize * mRecordSize;
    const ssize_t length = TEMP_FAILURE_RETRY(read(mFd.get(), buffer, wanted));
    if (length <= 0) return;

    std::vector<Event> events;
    for (size_t at = 0; at + mRecordSize <= static_cast<size_t>(length); at += mRecordSize) {
        const uint8_t* record = buffer + at;

        // The hub's sample times are in the device clock; without boot time there (the
        // attribute could not be written) the arrival time has to do.
        int64_t timestamp = mBoottime ? value(mTimestamp, record)
                                      : ::android::elapsedRealtimeNano();
        if (timestamp <= mLastTimestamp) timestamp = mLastTimestamp + 1;
        mLastTimestamp = timestamp;

        Event event;
        event.sensorHandle = mSensorInfo.sensorHandle;
        event.sensorType = mSensorInfo.type;
        event.timestamp = timestamp;
        if (mValues.size() == 3) {
            EventPayload::Vec3 vec3 = {
                    .x = value(mValues[0], record) * mScale,
                    .y = value(mValues[1], record) * mScale,
                    .z = value(mValues[2], record) * mScale,
                    .status = mSpec.status,
            };
            event.payload.set<EventPayload::Tag::vec3>(vec3);
        } else {
            const float scalar = value(mValues[0], record) * mScale;
            if (mSpec.onChange) {
                if (mHavePrevious && scalar == mPrevious) continue;
                mHavePrevious = true;
                mPrevious = scalar;
            }
            event.payload.set<EventPayload::Tag::scalar>(scalar);
        }
        events.push_back(event);
    }
    post(events);
}

ProximitySensor::ProximitySensor(int32_t sensorHandle, ISensorsEventCallback* callback)
    : Sensor(callback, "ssp-proximity") {
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.name = "TMD3725 Proximity";
    mSensorInfo.vendor = "ams";
    mSensorInfo.type = SensorType::PROXIMITY;
    mSensorInfo.maxRange = kProximityFarCm;
    mSensorInfo.resolution = kProximityFarCm;
    mSensorInfo.power = 0.75f;
    mSensorInfo.minDelayUs = 0;
    mSensorInfo.flags = static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE |
                                              SensorInfo::SENSOR_FLAG_BITS_WAKE_UP);
    startThread();
}

ProximitySensor::~ProximitySensor() {
    stopThread();
}

bool ProximitySensor::startDevice() {
    if (!findDevice()) return false;
    mBoottime = useBoottime();

    // The event fd outlives the device fd, which only one opener can hold.
    unique_fd device(open(mDevNode.c_str(), O_RDONLY | O_CLOEXEC));
    if (device.get() < 0) {
        PLOG(ERROR) << mIioName << ": open " << mDevNode;
        return false;
    }
    int eventFd = -1;
    if (ioctl(device.get(), IIO_GET_EVENT_FD_IOCTL, &eventFd) < 0 || eventFd < 0) {
        PLOG(ERROR) << mIioName << ": IIO_GET_EVENT_FD_IOCTL";
        return false;
    }
    mFd.reset(eventFd);
    fcntl(eventFd, F_SETFD, FD_CLOEXEC);

    // Off first: the kernel sends the current state when the events are switched on.
    ::android::base::WriteStringToFile("0", mSysfs + "/events/in_proximity_thresh_either_en");
    return writeAttr("events/in_proximity_thresh_either_en", "1");
}

void ProximitySensor::stopDevice() {
    if (!mSysfs.empty()) {
        ::android::base::WriteStringToFile("0",
                                           mSysfs + "/events/in_proximity_thresh_either_en");
    }
    mFd.reset();
}

void ProximitySensor::readDevice() {
    iio_event_data data;
    if (TEMP_FAILURE_RETRY(read(mFd.get(), &data, sizeof(data))) != sizeof(data)) return;
    if (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(data.id) != IIO_PROXIMITY) return;

    const bool near = IIO_EVENT_CODE_EXTRACT_DIR(data.id) == IIO_EV_DIR_RISING;
    Event event;
    event.sensorHandle = mSensorInfo.sensorHandle;
    event.sensorType = SensorType::PROXIMITY;
    event.timestamp = mBoottime ? data.timestamp : ::android::elapsedRealtimeNano();
    event.payload.set<EventPayload::Tag::scalar>(near ? kProximityNearCm : kProximityFarCm);
    post({event});
}

}  // namespace sensors
}  // namespace hardware
}  // namespace android
}  // namespace aidl
