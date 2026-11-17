// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <aidl/android/media/audio/common/AudioDevice.h>

struct audio_route;

namespace aidl::android::hardware::audio::core::a3y17lte {

// Mixer routing of the A3's sound card ("a3y17lte": I2S0 -> audio mixer -> COD3026X codec +
// TFA9896 speaker amplifier). One PCM (device 0) serves every output and every input; the
// device decides which mixer path (mixer_paths.xml) is on.
class AudioRouter {
  public:
    static AudioRouter& getInstance();

    // ALSA card number of the "a3y17lte" card, or -1 while it is not registered.
    int getCard();

    // Routes the devices of the output or the input stream. An output can go to several
    // devices at once (a ringtone on the speaker and the headphones).
    void setDevices(bool isInput,
                    const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices);

    // A phone call: the modem's downlink joins the mixer and its uplink comes from the codec.
    // The call follows the output devices (earpiece when there are none) and takes the
    // matching microphone unless a capture stream has chosen one. The radio HAL reads the
    // kind of output from a property, for the modem's own echo and noise tuning.
    void setCallActive(bool active);

    // Call volume, 0.0 to 1.0: the level of the modem's downlink in the mixer.
    void setVoiceVolume(float volume);

  private:
    AudioRouter() = default;

    int findCardLocked();
    bool initLocked();
    void applyLocked();
    void applyVoiceVolumeLocked();

    std::mutex mLock;
    int mCard = -1;
    audio_route* mRoute = nullptr;
    std::vector<std::string> mOutputPaths;
    std::vector<std::string> mInputPaths;
    std::string mCallOutput = "handset";  // handset, speaker, headset or headphone
    bool mCallActive = false;
    float mVoiceVolume = 1.0f;
};

}  // namespace aidl::android::hardware::audio::core::a3y17lte
