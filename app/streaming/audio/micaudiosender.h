#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include <QString>
#include <SDL_audio.h>

struct OpusEncoder;

class MicAudioSender
{
public:
    MicAudioSender();
    ~MicAudioSender();

    MicAudioSender(const MicAudioSender&) = delete;
    MicAudioSender& operator=(const MicAudioSender&) = delete;

    bool start(const QString& deviceOverride = QString());
    void stop();
    bool isActive() const;

private:
    void captureLoop();

    OpusEncoder* m_Encoder;
    SDL_AudioDeviceID m_CaptureDevice;
    bool m_InitializedSdlAudio;
    std::atomic<bool> m_Running;
    std::thread m_Worker;
    uint16_t m_Seq;
    uint32_t m_TimestampSamples;
};
