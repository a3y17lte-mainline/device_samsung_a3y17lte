// SPDX-License-Identifier: Apache-2.0

#include "Power.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::android::hardware::power::a3y17lte::Power;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    auto power = ndk::SharedRefBase::make<Power>();
    const std::string instance = std::string(Power::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(power->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
