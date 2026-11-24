#include "micaudiosender.h"

#include <chrono>
#include <vector>

#include <QByteArray>
#include <SDL.h>
#include <opus.h>

#include <Limelight.h>

MicAudioSender::MicAudioSender()
    : m_Encoder(nullptr),
      m_CaptureDevice(0),
      m_InitializedSdlAudio(false),
      m_Running(false),
      m_Seq(0),
      m_TimestampSamples(0)
{
}

MicAudioSender::~MicAudioSender()
{
    stop();
}

bool MicAudioSender::isActive() const
{
    return m_Running.load();
}

bool MicAudioSender::start(const QString& deviceOverride)
{
    if (m_Running.load()) {
        return true;
    }

    if ((LiGetHostFeatureFlags() & LI_FF_MIC_INPUT) == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Reverse mic requested, but host does not advertise LI_FF_MIC_INPUT");
        return false;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MicAudioSender SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s",
                     SDL_GetError());
        return false;
    }
    m_InitializedSdlAudio = true;

    int opusErr = OPUS_OK;
    m_Encoder = opus_encoder_create(LI_MIC_SAMPLE_RATE, LI_MIC_CHANNELS, OPUS_APPLICATION_VOIP, &opusErr);
    if (opusErr != OPUS_OK || m_Encoder == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create reverse mic Opus encoder: %s",
                     opus_strerror(opusErr));
        stop();
        return false;
    }

    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(48000));
    opus_encoder_ctl(m_Encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(m_Encoder, OPUS_SET_DTX(1));

    QString targetDevice = deviceOverride;
    if (targetDevice.isEmpty()) {
        QByteArray envDev = qgetenv("MOONLIGHT_MIC_INPUT_DEVICE");
        if (!envDev.isEmpty()) {
            targetDevice = QString::fromUtf8(envDev);
        }
    }

    SDL_AudioSpec want = {};
    SDL_AudioSpec have = {};
    want.freq = LI_MIC_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = LI_MIC_CHANNELS;
    want.samples = LI_MIC_FRAME_SAMPLES;

    QByteArray targetDeviceUtf8 = targetDevice.toUtf8();
    const char* devName = targetDeviceUtf8.isEmpty() ? nullptr : targetDeviceUtf8.constData();
    m_CaptureDevice = SDL_OpenAudioDevice(devName, 1, &want, &have, 0);
    if (m_CaptureDevice == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_OpenAudioDevice(capture) failed: %s",
                     SDL_GetError());
        stop();
        return false;
    }

    m_Seq = 0;
    m_TimestampSamples = 0;
    m_Running.store(true);
    SDL_PauseAudioDevice(m_CaptureDevice, 0);

    m_Worker = std::thread(&MicAudioSender::captureLoop, this);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Reverse mic uplink started (%d Hz, %d ch)",
                have.freq, have.channels);
    return true;
}

void MicAudioSender::stop()
{
    m_Running.store(false);
    if (m_Worker.joinable()) {
        m_Worker.join();
    }
    if (m_CaptureDevice != 0) {
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
    }
    if (m_Encoder != nullptr) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }
    if (m_InitializedSdlAudio) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_InitializedSdlAudio = false;
    }
}

void MicAudioSender::captureLoop()
{
    const Uint32 frameBytes = static_cast<Uint32>(LI_MIC_FRAME_SAMPLES * sizeof(int16_t));
    std::vector<int16_t> pcm(LI_MIC_FRAME_SAMPLES);
    std::vector<unsigned char> opus(LI_MIC_MAX_OPUS_BYTES);

    while (m_Running.load()) {
        Uint32 queued = SDL_GetQueuedAudioSize(m_CaptureDevice);
        if (queued < frameBytes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        Uint32 got = SDL_DequeueAudio(m_CaptureDevice, pcm.data(), frameBytes);
        if (got < frameBytes) {
            continue;
        }

        int encoded = opus_encode(m_Encoder,
                                  pcm.data(),
                                  LI_MIC_FRAME_SAMPLES,
                                  opus.data(),
                                  static_cast<opus_int32>(opus.size()));
        if (encoded < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Reverse mic Opus encode error: %s",
                        opus_strerror(encoded));
            continue;
        }

        uint16_t seq = m_Seq++;
        uint32_t ts = m_TimestampSamples;
        m_TimestampSamples += LI_MIC_FRAME_SAMPLES;

        // Skip Opus DTX comfort-noise/silence packets while advancing monotonic seq & timestamp
        if (encoded <= 2) {
            continue;
        }

        LiSendMicAudioFrame(opus.data(), encoded, seq, ts);
    }
}
