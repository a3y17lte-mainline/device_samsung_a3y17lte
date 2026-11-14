// SPDX-License-Identifier: Apache-2.0

#include "Nfc.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::android::hardware::nfc::a3y17lte::Nfc;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    auto nfc = ndk::SharedRefBase::make<Nfc>();
    const std::string instance = std::string(Nfc::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(nfc->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
