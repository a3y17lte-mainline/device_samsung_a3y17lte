// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/light/BnLights.h>

#include <mutex>
#include <thread>

namespace aidl::android::hardware::light {

// The one light of the a3y17lte that Android drives through this HAL: the touch-key backlight
// (LED class device "tm2-touchkey", on/off only). The panel brightness goes through the
// composer (drm_hwcomposer writes the backlight device), not through a light.
class Lights : public BnLights {
  public:
    Lights();
    ~Lights();

    ndk::ScopedAStatus setLightState(int32_t id, const HwLightState& state) override;
    ndk::ScopedAStatus getLights(std::vector<HwLight>* _aidl_return) override;

  private:
    bool writeButtons(bool on);
    void turnOffAtStart();

    std::mutex mLock;
    bool mStateSet = false;  // the framework has set the light: guarded by mLock
    std::thread mStartThread;
};

}  // namespace aidl::android::hardware::light
