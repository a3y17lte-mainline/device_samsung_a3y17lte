// SPDX-License-Identifier: Apache-2.0
//
// Audio core service for the a3y17lte: AOSP's example service (Config, r_submix, usb from
// libaudioserviceexampleimpl) with our module as "default".

#include <cstdlib>
#include <ctime>
#include <utility>
#include <vector>

#define LOG_TAG "AHAL_A3Main"
#include <Log.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "ModuleA3.h"
#include "core-impl/AudioPolicyConfigXmlConverter.h"
#include "core-impl/ChildInterface.h"
#include "core-impl/Config.h"
#include "core-impl/Module.h"

using aidl::android::hardware::audio::core::ChildInterface;
using aidl::android::hardware::audio::core::Config;
using aidl::android::hardware::audio::core::Module;
using aidl::android::hardware::audio::core::a3y17lte::ModuleA3;
using aidl::android::hardware::audio::core::internal::AudioPolicyConfigXmlConverter;

namespace {

ChildInterface<Module> createModule(const std::string& name,
                                    std::unique_ptr<Module::Configuration>&& config) {
    ChildInterface<Module> result;
    auto moduleType = Module::typeFromString(name);
    if (!moduleType.has_value()) {
        LOG(ERROR) << __func__ << ": module type \"" << name << "\" is not supported";
        return result;
    }
    std::shared_ptr<Module> module;
    if (*moduleType == Module::Type::DEFAULT) {
        module = ndk::SharedRefBase::make<ModuleA3>(std::move(config));
    } else {
        module = Module::createInstance(*moduleType, std::move(config));
    }
    if (module == nullptr) return result;
    result = std::move(module);

    const std::string moduleFqn = std::string().append(Module::descriptor).append("/").append(name);
    binder_status_t status = AServiceManager_addService(result.getBinder(), moduleFqn.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << __func__ << ": failed to register service for \"" << moduleFqn << "\"";
        return ChildInterface<Module>();
    }
    return result;
}

}  // namespace

int main() {
    // The implementation uses random values (e.g. port ids).
    std::srand(std::time(nullptr));

    ABinderProcess_setThreadPoolMaxThreadCount(16);
    ABinderProcess_startThreadPool();

    AudioPolicyConfigXmlConverter audioPolicyConverter{
            ::android::audio_get_audio_policy_config_file()};

    auto config = ndk::SharedRefBase::make<Config>(audioPolicyConverter);
    const std::string configFqn = std::string().append(Config::descriptor).append("/default");
    if (AServiceManager_addService(config->asBinder().get(), configFqn.c_str()) != STATUS_OK) {
        LOG(ERROR) << "failed to register service for \"" << configFqn << "\"";
    }

    std::vector<ChildInterface<Module>> moduleInstances;
    auto configs(audioPolicyConverter.releaseModuleConfigs());
    for (std::pair<std::string, std::unique_ptr<Module::Configuration>>& configPair : *configs) {
        if (auto instance = createModule(configPair.first, std::move(configPair.second));
            instance) {
            moduleInstances.push_back(std::move(instance));
        }
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
