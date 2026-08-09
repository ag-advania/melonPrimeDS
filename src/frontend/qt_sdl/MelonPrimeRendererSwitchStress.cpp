/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "MelonPrimeRendererSwitchStress.h"

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES

#include <vector>

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "Config.h"
#include "MelonPrimeVideoBackend.h"
#include "Platform.h"
#include "Window.h"

namespace MelonPrime
{
namespace RendererSwitchStress
{

namespace
{

using melonDS::Platform::Log;
using melonDS::Platform::LogLevel;

int EnvInt(const char* name, int fallback, int minimum)
{
    const QByteArray raw = qgetenv(name);
    if (raw.isEmpty())
        return fallback;

    bool ok = false;
    const int value = QString::fromUtf8(raw).trimmed().toInt(&ok);
    if (!ok || value < minimum)
    {
        Log(LogLevel::Warn,
            "[switch-stress] %s=\"%s\" is not an integer >= %d; using %d\n",
            name, raw.constData(), minimum, fallback);
        return fallback;
    }
    return value;
}

// Drives the cycle. Parented to the window, so closing the window destroys it
// and stops the timer; a stress run can never outlive the thing it is driving.
class Driver : public QObject
{
public:
    Driver(MainWindow* window, std::vector<int> sequence, int iterations, int intervalMs)
        : QObject(window)
        , Window(window)
        , Sequence(std::move(sequence))
        , Iterations(iterations)
    {
        Timer = new QTimer(this);
        Timer->setInterval(intervalMs);
        connect(Timer, &QTimer::timeout, this, &Driver::Step);
    }

    void Start(int originalRenderer)
    {
        OriginalRenderer = originalRenderer;
        Total = static_cast<int>(Sequence.size()) * Iterations;
        Log(LogLevel::Info,
            "[switch-stress] armed: %d switches (%zu-step cycle x %d iterations), %d ms apart\n",
            Total, Sequence.size(), Iterations, Timer->interval());
        Timer->start();
    }

private:
    void Step()
    {
        if (!Window)
        {
            Timer->stop();
            return;
        }

        if (Done >= Total)
        {
            Timer->stop();
            // Put the user's renderer back, through the same path, so the
            // session ends on the configuration it started with rather than
            // wherever the cycle happened to stop.
            Log(LogLevel::Info,
                "[switch-stress] complete: %d/%d switches performed, restoring renderer %d\n",
                Done, Total, OriginalRenderer);
            Apply(OriginalRenderer);
            Log(LogLevel::Info, "[switch-stress] finished\n");
            return;
        }

        const int next = Sequence[static_cast<std::size_t>(Done) % Sequence.size()];
        Done++;
        Log(LogLevel::Info,
            "[switch-stress] switch %d/%d -> renderer %d\n", Done, Total, next);
        Apply(next);
    }

    void Apply(int renderer)
    {
        Config::Table cfg = Config::GetGlobalTable();
        const int previous = cfg.GetInt("3D.Renderer");
        if (previous == renderer)
            return;

        // glchange is what tells onUpdateVideoSettings to tear the screen panel
        // down and build a new one. Computing it the same way the settings
        // dialog does is the point: this must exercise the production path, not
        // a shortcut around it.
        const bool useGL = cfg.GetBool("Screen.UseGL");
        const auto before = VideoBackend::ResolvePresentationBackend(useGL, previous);

        cfg.SetInt("3D.Renderer", renderer);

        const auto after = VideoBackend::ResolvePresentationBackend(useGL, renderer);
        const bool backendChanged = (before != after);

        // onUpdateVideoSettings is a private slot. Invoking it through the
        // meta-object system reaches exactly the same code the settings dialog
        // reaches, without widening MainWindow's API for a developer-only tool
        // that must not exist in release builds at all. DirectConnection
        // because this already runs on the GUI thread and the switch must be
        // complete before the next timer tick.
        if (!QMetaObject::invokeMethod(
                Window, "onUpdateVideoSettings", Qt::DirectConnection,
                Q_ARG(bool, backendChanged)))
        {
            Log(LogLevel::Error,
                "[switch-stress] onUpdateVideoSettings could not be invoked; stopping\n");
            Timer->stop();
        }
    }

    QPointer<MainWindow> Window;
    QTimer* Timer = nullptr;
    std::vector<int> Sequence;
    int Iterations = 0;
    int Total = 0;
    int Done = 0;
    int OriginalRenderer = 0;
};

} // namespace


void ArmFromEnvironment(MainWindow* window)
{
    if (!window)
        return;

    // Only the top-level window drives the cycle: onUpdateVideoSettings()
    // already forwards to the parent and updates every child, so arming a
    // child too would run two overlapping cycles over the same panels.
    if (window->parentWidget())
        return;

    const QByteArray raw = qgetenv("MELONPRIME_RENDERER_SWITCH_STRESS");
    if (raw.isEmpty())
        return;

    std::vector<int> sequence;
    const QStringList parts = QString::fromUtf8(raw).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (!ok)
        {
            Log(LogLevel::Warn,
                "[switch-stress] ignoring non-numeric renderer id \"%s\"\n",
                part.trimmed().toUtf8().constData());
            continue;
        }
        sequence.push_back(value);
    }

    if (sequence.size() < 2)
    {
        Log(LogLevel::Warn,
            "[switch-stress] MELONPRIME_RENDERER_SWITCH_STRESS needs at least two "
            "renderer ids (got %zu); not arming\n",
            sequence.size());
        return;
    }

    const int iterations = EnvInt("MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS", 50, 1);
    const int intervalMs = EnvInt("MELONPRIME_RENDERER_SWITCH_STRESS_INTERVAL_MS", 400, 50);

    Config::Table cfg = Config::GetGlobalTable();
    const int original = cfg.GetInt("3D.Renderer");

    auto* driver = new Driver(window, std::move(sequence), iterations, intervalMs);
    driver->Start(original);
}

} // namespace RendererSwitchStress
} // namespace MelonPrime

#endif // MELONPRIME_ENABLE_DEVELOPER_FEATURES
