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

#ifndef MELONPRIME_RENDERER_SWITCH_STRESS_H
#define MELONPRIME_RENDERER_SWITCH_STRESS_H

// Developer-only renderer-switch stress driver.
//
// The whole file is compiled out of release builds: MELONPRIME_ENABLE_DEVELOPER_FEATURES
// is set only by development configures, and the source is not added to the
// target without it. Even in a developer build it stays completely dormant
// unless MELONPRIME_RENDERER_SWITCH_STRESS is set in the environment, so a
// developer build behaves exactly like a release one by default.
//
// Why this exists: runtime renderer switching has no scripted entry point --
// it is a settings-dialog interaction -- which makes "does switching leak or
// crash after fifty round trips" untestable by hand. This drives the real
// production path (MainWindow::onUpdateVideoSettings, which destroys and
// recreates the screen panel and replaces the 3D renderer) on a timer, and
// logs every transition so a run can be audited afterwards.
//
// Environment:
//   MELONPRIME_RENDERER_SWITCH_STRESS
//       Comma-separated 3D.Renderer ids to cycle through, e.g. "3,0" for
//       Vulkan <-> Software. Setting it at all arms the driver.
//       Ids match the renderer3D_* enum: 0 software, 1 OpenGL,
//       2 OpenGL Compute, 3 Vulkan, 4 DX12.
//   MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS
//       Full passes over the list. Default 50.
//   MELONPRIME_RENDERER_SWITCH_STRESS_INTERVAL_MS
//       Milliseconds between switches. Default 400, minimum 50 -- a switch
//       needs enough frames in between to actually present, or the test proves
//       only that teardown does not crash.

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES

class MainWindow;

namespace MelonPrime
{
namespace RendererSwitchStress
{

// No-op unless MELONPRIME_RENDERER_SWITCH_STRESS is set. Call once, from the
// main window, after the first screen panel exists. GUI thread only:
// onUpdateVideoSettings() destroys and creates QWidgets.
void ArmFromEnvironment(MainWindow* window);

} // namespace RendererSwitchStress
} // namespace MelonPrime

#endif // MELONPRIME_ENABLE_DEVELOPER_FEATURES
#endif // MELONPRIME_RENDERER_SWITCH_STRESS_H
