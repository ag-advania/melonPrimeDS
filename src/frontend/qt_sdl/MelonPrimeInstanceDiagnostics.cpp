#include "MelonPrimeInstanceDiagnostics.h"

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)

#include "EmuInstance.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

namespace MelonPrime::InstanceDiagnostics {

namespace {

[[nodiscard]] int InstanceId(EmuInstance* emu) noexcept
{
    return emu ? emu->getInstanceID() : -1;
}

void ReportThreadViolation(
    EmuInstance* emu,
    const char* expected,
    const char* operation) noexcept
{
    qWarning().nospace()
        << "[MelonPrime][instance=" << InstanceId(emu)
        << "][thread-check] expected=" << expected
        << " operation=" << operation
        << " current=" << QThread::currentThread();

    if (qEnvironmentVariableIntValue("MELONPRIME_STRICT_THREAD_ASSERTS") != 0)
        Q_ASSERT_X(false, "MelonPrime thread ownership", operation);
}

} // namespace

void LogLifecycle(EmuInstance* emu, const void* core, const char* event) noexcept
{
    qInfo().nospace()
        << "[MelonPrime][instance=" << InstanceId(emu)
        << "][core=" << core << "] " << event;
}

bool CheckGuiThread(EmuInstance* emu, const char* operation) noexcept
{
    const QCoreApplication* app = QCoreApplication::instance();
    const bool matches = app && QThread::currentThread() == app->thread();
    if (!matches)
        ReportThreadViolation(emu, "GUI", operation);
    return matches;
}

bool CheckEmuThread(EmuInstance* emu, const char* operation) noexcept
{
    const bool matches = emu
        && emu->getEmuThread()
        && QThread::currentThread() == emu->getEmuThread();
    if (!matches)
        ReportThreadViolation(emu, "EmuThread", operation);
    return matches;
}

} // namespace MelonPrime::InstanceDiagnostics

#endif
