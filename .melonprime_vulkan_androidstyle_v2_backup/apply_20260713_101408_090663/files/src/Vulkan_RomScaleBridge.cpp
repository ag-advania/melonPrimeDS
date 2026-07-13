#include "Vulkan_RomScaleBridge.h"

#include "GPU3D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace melonDS::Vulkan
{
namespace
{

struct Point
{
    float X = 0.0f;
    float Y = 0.0f;
};

float Edge(const Point& a, const Point& b, float x, float y) noexcept
{
    return (x - a.X) * (b.Y - a.Y) - (y - a.Y) * (b.X - a.X);
}

float ApplyRenderXScroll(float x, u16 xpos) noexcept
{
    if (xpos == 0)
        return x;
    if ((xpos & 0x100u) != 0)
        return x + static_cast<float>(512u - xpos);
    return x - static_cast<float>(xpos);
}

Point VertexPoint(const Vertex& vertex, int scale, bool hires, u16 xpos) noexcept
{
    float x = hires
        ? static_cast<float>(vertex.HiresPosition[0]) / 16.0f
        : static_cast<float>(vertex.FinalPosition[0]);
    float y = hires
        ? static_cast<float>(vertex.HiresPosition[1]) / 16.0f
        : static_cast<float>(vertex.FinalPosition[1]);
    x = ApplyRenderXScroll(x, xpos);
    return {x * static_cast<float>(scale), y * static_cast<float>(scale)};
}

void RasterizeTriangle(
    const Point& a,
    const Point& b,
    const Point& c,
    int width,
    int height,
    std::vector<std::uint8_t>& coverage,
    std::uint64_t& coveredPixels) noexcept
{
    const float area = Edge(a, b, c.X, c.Y);
    if (std::fabs(area) < 0.0001f)
        return;

    const int minX = std::max(0, static_cast<int>(std::floor(
        std::min({a.X, b.X, c.X}) - 0.5f)));
    const int maxX = std::min(width - 1, static_cast<int>(std::ceil(
        std::max({a.X, b.X, c.X}) + 0.5f)));
    const int minY = std::max(0, static_cast<int>(std::floor(
        std::min({a.Y, b.Y, c.Y}) - 0.5f)));
    const int maxY = std::min(height - 1, static_cast<int>(std::ceil(
        std::max({a.Y, b.Y, c.Y}) + 0.5f)));
    if (minX > maxX || minY > maxY)
        return;

    const bool positive = area > 0.0f;
    constexpr float epsilon = 0.0001f;
    for (int y = minY; y <= maxY; ++y)
    {
        const float py = static_cast<float>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x)
        {
            const float px = static_cast<float>(x) + 0.5f;
            const float e0 = Edge(a, b, px, py);
            const float e1 = Edge(b, c, px, py);
            const float e2 = Edge(c, a, px, py);
            const bool inside = positive
                ? (e0 >= -epsilon && e1 >= -epsilon && e2 >= -epsilon)
                : (e0 <= epsilon && e1 <= epsilon && e2 <= epsilon);
            if (!inside)
                continue;
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            if (coverage[index] == 0)
            {
                coverage[index] = 1;
                ++coveredPixels;
            }
        }
    }
}

void RasterizeLine(
    Point a,
    Point b,
    int scale,
    int width,
    int height,
    std::vector<std::uint8_t>& coverage,
    std::uint64_t& coveredPixels) noexcept
{
    const float dx = b.X - a.X;
    const float dy = b.Y - a.Y;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(
        std::fabs(dx), std::fabs(dy)))));
    const int radius = std::max(0, scale / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int cx = static_cast<int>(std::floor(a.X + dx * t));
        const int cy = static_cast<int>(std::floor(a.Y + dy * t));
        for (int oy = -radius; oy <= radius; ++oy)
        {
            const int y = cy + oy;
            if (y < 0 || y >= height)
                continue;
            for (int ox = -radius; ox <= radius; ++ox)
            {
                const int x = cx + ox;
                if (x < 0 || x >= width)
                    continue;
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                if (coverage[index] == 0)
                {
                    coverage[index] = 1;
                    ++coveredPixels;
                }
            }
        }
    }
}

// MELONPRIME_VULKAN_COMPAT_HIRES_SAMPLE_V1
// The current ROM-visible Vulkan path is a Software-correctness compatibility
// bridge, not the final native Vulkan rasterizer. Preserve its safety mask, but
// sample the captured RGB6 3D plane at subpixel centers so ScaleFactor > 1
// produces a genuinely scale-dependent 3D image instead of writing the same
// nearest native color into every high-resolution subpixel.
u32 SampleNative3DBilinear(
    const u32* source,
    float sourceX,
    float sourceY,
    int fallbackX,
    int fallbackY) noexcept
{
    sourceX = std::clamp(sourceX, 0.0f, 255.0f);
    sourceY = std::clamp(sourceY, 0.0f, 191.0f);

    const int x0 = static_cast<int>(std::floor(sourceX));
    const int y0 = static_cast<int>(std::floor(sourceY));
    const int x1 = std::min(255, x0 + 1);
    const int y1 = std::min(191, y0 + 1);
    const float fx = sourceX - static_cast<float>(x0);
    const float fy = sourceY - static_cast<float>(y0);

    float totalWeight = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;

    const auto accumulate = [&](int x, int y, float weight)
    {
        if (weight <= 0.0f)
            return;
        const u32 pixel = source[static_cast<std::size_t>(y) * 256u + x];
        const u32 alpha = (pixel >> 24) & 0x1Fu;
        if (alpha == 0)
            return;
        totalWeight += weight;
        red += static_cast<float>(pixel & 0x3Fu) * weight;
        green += static_cast<float>((pixel >> 8) & 0x3Fu) * weight;
        blue += static_cast<float>((pixel >> 16) & 0x3Fu) * weight;
    };

    accumulate(x0, y0, (1.0f - fx) * (1.0f - fy));
    accumulate(x1, y0, fx * (1.0f - fy));
    accumulate(x0, y1, (1.0f - fx) * fy);
    accumulate(x1, y1, fx * fy);

    fallbackX = std::clamp(fallbackX, 0, 255);
    fallbackY = std::clamp(fallbackY, 0, 191);
    const u32 fallback =
        source[static_cast<std::size_t>(fallbackY) * 256u + fallbackX];
    if (totalWeight <= 0.0001f)
        return fallback;

    const auto quantizeRgb6 = [totalWeight](float weighted) -> u32
    {
        const long rounded = std::lround(weighted / totalWeight);
        return static_cast<u32>(std::clamp<long>(rounded, 0L, 63L));
    };

    const u32 alpha = (fallback >> 24) & 0x1Fu;
    return quantizeRgb6(red)
        | (quantizeRgb6(green) << 8)
        | (quantizeRgb6(blue) << 16)
        | (alpha << 24);
}

} // namespace

void VulkanRomScaleResult::Clear() noexcept
{
    Width = 0;
    Height = 0;
    HighResolution3D.clear();
    Coverage.clear();
    InputPolygonCount = 0;
    RasterizedTriangleCount = 0;
    CoveredPixelCount = 0;
    BetterPolygonPathUsed = false;
    HiresCoordinatePathUsed = false;
    Valid = false;
    FailureReason.clear();
}

bool BuildVulkanRomScaleBridge(
    const GPU3D& gpu3D,
    const u32* native3D,
    const VulkanRomScaleSettings& requested,
    VulkanRomScaleResult& result) noexcept
{
    result.Clear();
    if (!native3D)
    {
        result.FailureReason = "native 3D source is null";
        return false;
    }

    const int scale = std::clamp(requested.ScaleFactor, 1, 16);
    const int width = 256 * scale;
    const int height = 192 * scale;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / sizeof(u32))
    {
        result.FailureReason = "scaled output size overflow";
        return false;
    }

    try
    {
        result.Width = width;
        result.Height = height;
        result.Coverage.assign(pixelCount, 0);
        result.HighResolution3D.assign(pixelCount, 0);
    }
    catch (...)
    {
        result.Clear();
        result.FailureReason = "scaled output allocation failed";
        return false;
    }

    result.InputPolygonCount = gpu3D.RenderNumPolygons;
    result.BetterPolygonPathUsed = requested.BetterPolygons;
    result.HiresCoordinatePathUsed = requested.HiresCoordinates;
    const u16 xpos = gpu3D.GetRenderXPos();

    for (u32 polygonIndex = 0; polygonIndex < gpu3D.RenderNumPolygons; ++polygonIndex)
    {
        const Polygon* polygon = gpu3D.RenderPolygonRAM[polygonIndex];
        if (!polygon || polygon->Degenerate || polygon->NumVertices < 2)
            continue;

        const u32 count = std::min<u32>(polygon->NumVertices, 10u);
        std::array<Point, 10> points{};
        bool verticesValid = true;
        for (u32 i = 0; i < count; ++i)
        {
            if (!polygon->Vertices[i])
            {
                verticesValid = false;
                break;
            }
            points[i] = VertexPoint(*polygon->Vertices[i], scale,
                requested.HiresCoordinates, xpos);
        }
        if (!verticesValid)
            continue;

        if (polygon->Type == 1 || count == 2)
        {
            RasterizeLine(points[0], points[1], scale, width, height,
                result.Coverage, result.CoveredPixelCount);
            continue;
        }

        if (count == 3 || !requested.BetterPolygons)
        {
            for (u32 i = 1; i + 1 < count; ++i)
            {
                RasterizeTriangle(points[0], points[i], points[i + 1],
                    width, height, result.Coverage, result.CoveredPixelCount);
                ++result.RasterizedTriangleCount;
            }
        }
        else
        {
            Point center{};
            for (u32 i = 0; i < count; ++i)
            {
                center.X += points[i].X;
                center.Y += points[i].Y;
            }
            center.X /= static_cast<float>(count);
            center.Y /= static_cast<float>(count);
            for (u32 i = 0; i < count; ++i)
            {
                const u32 next = (i + 1u) % count;
                RasterizeTriangle(center, points[i], points[next],
                    width, height, result.Coverage, result.CoveredPixelCount);
                ++result.RasterizedTriangleCount;
            }
        }
    }

    std::array<std::uint8_t, 256u * 192u> nativeCenterCoverage{};
    const int centerOffset = scale / 2;
    for (int nativeY = 0; nativeY < 192; ++nativeY)
    {
        const int sampleY = std::min(height - 1, nativeY * scale + centerOffset);
        for (int nativeX = 0; nativeX < 256; ++nativeX)
        {
            const int sampleX = std::min(width - 1, nativeX * scale + centerOffset);
            nativeCenterCoverage[static_cast<std::size_t>(nativeY) * 256u + nativeX] =
                result.Coverage[static_cast<std::size_t>(sampleY) * width + sampleX];
        }
    }

    for (int y = 0; y < height; ++y)
    {
        const int nativeY = std::min(191, y / scale);
        for (int x = 0; x < width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            if (result.Coverage[index] == 0)
                continue;
            const int nativeX = std::min(255, x / scale);
            int sampleX = nativeX;
            int sampleY = nativeY;
            if (nativeCenterCoverage[static_cast<std::size_t>(nativeY) * 256u + nativeX] == 0)
            {
                int bestDistance = std::numeric_limits<int>::max();
                for (int oy = -2; oy <= 2; ++oy)
                {
                    const int candidateY = nativeY + oy;
                    if (candidateY < 0 || candidateY >= 192)
                        continue;
                    for (int ox = -2; ox <= 2; ++ox)
                    {
                        const int candidateX = nativeX + ox;
                        if (candidateX < 0 || candidateX >= 256)
                            continue;
                        if (nativeCenterCoverage[
                                static_cast<std::size_t>(candidateY) * 256u + candidateX] == 0)
                            continue;
                        const int distance = ox * ox + oy * oy;
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            sampleX = candidateX;
                            sampleY = candidateY;
                        }
                    }
                }
            }
            const float sourceX =
                (static_cast<float>(x) + 0.5f) / static_cast<float>(scale) - 0.5f;
            const float sourceY =
                (static_cast<float>(y) + 0.5f) / static_cast<float>(scale) - 0.5f;
            result.HighResolution3D[index] = SampleNative3DBilinear(
                native3D, sourceX, sourceY, sampleX, sampleY);
        }
    }

    result.Valid = true;
    return true;
}

} // namespace melonDS::Vulkan
