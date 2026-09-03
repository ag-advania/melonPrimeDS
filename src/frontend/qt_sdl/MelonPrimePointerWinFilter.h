#ifndef MELONPRIME_POINTER_WIN_FILTER_H
#define MELONPRIME_POINTER_WIN_FILTER_H

#ifdef _WIN32

#include <QAbstractNativeEventFilter>

namespace MelonPrime {

class DirectAimIngress;

// Windows Pointer/Pen ingress for direct aim.
//
// Responsibility is narrow on purpose: receive native messages, confirm the
// target window, classify the pointer, and hand a fixed POD sample to the
// normalizer. It performs no config lookup, no aim transform, no device
// enumeration, no vendor detection, no allocation, and no per-event logging.
//
// The filter is owned by the capturing GUI surface and installed only while a
// tablet-enabled direct-aim capture is held, so a mouse-only session never
// pays for it.
class PointerWinFilter final : public QAbstractNativeEventFilter
{
public:
    explicit PointerWinFilter(DirectAimIngress& ingress) noexcept;
    ~PointerWinFilter() override;

    PointerWinFilter(const PointerWinFilter&) = delete;
    PointerWinFilter& operator=(const PointerWinFilter&) = delete;

    // Both handles are accepted: pointer and mouse messages are delivered to
    // the top-level window, or to the render surface when it is native.
    void SetTargetWindows(void* topLevelHwnd, void* surfaceHwnd) noexcept;

    // Installs/removes on the GUI thread. Install is idempotent.
    void Install();
    void Remove();
    [[nodiscard]] bool Installed() const noexcept { return m_installed; }

    bool nativeEventFilter(const QByteArray& eventType,
                           void* message,
                           qintptr* result) override;

private:
    DirectAimIngress& m_ingress;
    void* m_topLevelHwnd = nullptr;
    void* m_surfaceHwnd = nullptr;
    bool m_installed = false;
};

} // namespace MelonPrime

#endif // _WIN32
#endif // MELONPRIME_POINTER_WIN_FILTER_H
