#include "clipboardsync.h"

#include <QByteArray>
#include <SDL.h>

#include <Limelight.h>

ClipboardSync::ClipboardSync()
    : m_Active(false),
      m_HasLastLocalHash(false),
      m_HasLastRemoteHash(false),
      m_LastLocalHash(0),
      m_LastRemoteHash(0)
{
}

ClipboardSync::~ClipboardSync()
{
    stop();
}

void ClipboardSync::start()
{
    if (m_Active) {
        return;
    }

    if ((LiGetHostFeatureFlags() & LI_FF_CLIPBOARD_EVENT) == 0) {
        return;
    }

    m_Active = true;
}

void ClipboardSync::stop()
{
    m_Active = false;
}

bool ClipboardSync::isActive() const
{
    return m_Active;
}

void ClipboardSync::onLocalClipboardChanged()
{
    if (!m_Active || (LiGetHostFeatureFlags() & LI_FF_CLIPBOARD_EVENT) == 0) {
        return;
    }

    if (!SDL_HasClipboardText()) {
        return;
    }

    char* rawText = SDL_GetClipboardText();
    if (rawText == nullptr) {
        return;
    }

    QByteArray utf8(rawText);
    SDL_free(rawText);

    if (utf8.isEmpty() || utf8.size() > LI_STREAM_DESK_MAX_TEXT_BYTES) {
        return;
    }

    uint64_t hash = LiComputeClipboardHash(utf8.constData(), static_cast<uint32_t>(utf8.size()));
    if ((m_HasLastRemoteHash && hash == m_LastRemoteHash) ||
        (m_HasLastLocalHash && hash == m_LastLocalHash)) {
        return;
    }

    m_HasLastLocalHash = true;
    m_LastLocalHash = hash;
    LiSendClipboardText(utf8.constData(), static_cast<uint32_t>(utf8.size()), 0);
}

void ClipboardSync::onRemoteClipboardText(const QString& text, uint64_t contentHash, uint8_t flags)
{
    Q_UNUSED(flags);

    if (!m_Active || text.isEmpty()) {
        return;
    }

    if ((m_HasLastLocalHash && contentHash == m_LastLocalHash) ||
        (m_HasLastRemoteHash && contentHash == m_LastRemoteHash)) {
        return;
    }

    QByteArray utf8 = text.toUtf8();
    if (utf8.isEmpty() || utf8.size() > LI_STREAM_DESK_MAX_TEXT_BYTES) {
        return;
    }

    m_HasLastRemoteHash = true;
    m_LastRemoteHash = contentHash;
    m_HasLastLocalHash = false;

    SDL_SetClipboardText(utf8.constData());
}
