import re
from pathlib import Path

src_path = Path(r"C:\Users\Admin\Documents\git\melonDS-android-lib\src\GPU2D_Soft.cpp")
text = src_path.read_text(encoding="utf-8")
lines = text.splitlines()

ranges = [
    (78, 118),
    (334, 651),
    (671, 997),
    (1275, 1997),
]

extracted = []
for start, end in ranges:
    extracted.extend(lines[start - 1 : end])

body = "\n".join(extracted)

subs = [
    ("LastDebugCaptureStats", "SapphireDebugCaptureStats"),
    ("CurUnit->", "CaptureUnit()."),
    ("CurUnit == nullptr", "false"),
    ("CurUnit->Num == 0", "true"),
    ("CurUnit->Num == 1", "false"),
    ("CurUnit->Num", "0u"),
    ("&CurUnit->DispFIFOBuffer[0]", "GPU.DispFIFOBuffer"),
    ("CurUnit->DispCnt", "GPU.GPU2D_A.DispCnt"),
    ("CurUnit->CaptureCnt", "GPU.CaptureFrameCnt"),
    ("CurUnit->CaptureLatch", "GPU.CaptureEnable"),
    ("CurUnit->MasterBrightness", "GPU.MasterBrightnessA"),
    ("_3DLine", "Capture3DLine"),
    ("BGOBJLine", "CaptureBGOBJLine"),
    (
        "void SoftRenderer::DoCapture(u32 line, u32 width, u32 sourceLine)",
        "void SoftRenderer::DoCaptureStructured(u32 line, u32 width, u32 sourceLine)",
    ),
]

for old, new in subs:
    body = body.replace(old, new)

body = re.sub(
    r"bool SoftRenderer::CurrentUnitTargetsTopScreen\(\) const noexcept\s*\{.*?\n\}",
    """bool SoftRenderer::CurrentUnitTargetsTopScreen() const noexcept
{
    return ScreenIndexForEngine(StructuredVulkan2DCurrentEngine) == 0u;
}""",
    body,
    count=1,
    flags=re.DOTALL,
)

header = """/*
    Sapphire GPU2D structured Vulkan capture path ported for MelonPrimeDS.
    Source: SapphireRhodonite/melonDS-android-lib @ d77944275fa61f9b79cfcead2c3e98993429a023
*/

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GPU_Soft.h"
#include "GPU_ColorOp.h"
#include "VulkanDesktopCompat.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace MelonDSAndroid {
using ::areRendererDebugToolsEnabled;
using ::areRendererDebugBgObjLogsEnabled;
}

namespace melonDS
{
namespace
{
"""

idx = body.find("const u32* SoftRenderer::GetStructuredVulkan2DPlane")
pre = body[:idx]
post = body[idx:]

out = header + pre + "\n} // anonymous namespace\n\n" + post

out += """

GPU2D& SoftRenderer::CaptureUnit() noexcept
{
    return GPU.GPU2D_A;
}

const GPU2D& SoftRenderer::CaptureUnit() const noexcept
{
    return GPU.GPU2D_A;
}

bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return GetRenderer3D().UsesStructured2DMetadata();
}

void SoftRenderer::BeginStructuredVulkan2DLine(u32 engine, u32 line) noexcept
{
    StructuredVulkan2DCurrentEngine = engine;
    StructuredVulkan2DCurrentLineTargetsTop = ScreenIndexForEngine(engine) == 0u;
    ClearStructuredVulkan2DLine(line);
}

u32* SoftRenderer::GetCaptureBGOBJLine() noexcept
{
    return Rend2D_A != nullptr ? Rend2D_A->BGOBJLineForCapture() : nullptr;
}

} // namespace melonDS

#endif
"""

out_path = Path(r"C:\Users\Admin\Documents\git\melonPrimeDS\src\SapphireGPU2DStructuredVulkan.cpp")
out_path.write_text(out, encoding="utf-8")
print(f"Wrote {out_path} ({len(out.splitlines())} lines)")
