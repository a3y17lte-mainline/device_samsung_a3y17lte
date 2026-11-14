// SPDX-License-Identifier: Apache-2.0

#include "Thermal.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::android::hardware::thermal::Thermal;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    auto thermal = ndk::SharedRefBase::make<Thermal>();
    const std::string instance = std::string(Thermal::descriptor) + "/default";
    binder_status_t status =
            AServiceManager_addService(thermal->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
