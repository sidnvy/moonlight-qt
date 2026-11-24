#pragma once

#include <cstdint>
#include <QString>

class ClipboardSync
{
public:
    ClipboardSync();
    ~ClipboardSync();

    void start();
    void stop();
    bool isActive() const;

    void onLocalClipboardChanged();
    void onRemoteClipboardText(const QString& text, uint64_t contentHash, uint8_t flags);

private:
    bool m_Active;
    bool m_HasLastLocalHash;
    bool m_HasLastRemoteHash;
    uint64_t m_LastLocalHash;
    uint64_t m_LastRemoteHash;
};
