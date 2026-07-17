#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GPU3D_AcceleratedFrontend.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace melonDS
{

bool HasAcceleratedPolygonFlag(const AcceleratedPolygonMeta& polygonMeta, u32 flag) noexcept
{
    return (polygonMeta.Flags & flag) != 0u;
}

AcceleratedPolygonMeta BuildAcceleratedPolygonMeta(const Polygon& polygon) noexcept
{
    AcceleratedPolygonMeta polygonMeta{};
    polygonMeta.RenderKey = BuildAcceleratedRenderKey(polygon);
    polygonMeta.PolyAttr = polygon.Attr;
    polygonMeta.PolyId = (polygon.Attr >> 24u) & 0x3Fu;
    polygonMeta.Alpha5 = (polygon.Attr >> 16u) & 0x1Fu;

    if (polygon.Translucent)
        polygonMeta.Flags |= AcceleratedPolygonFlagTranslucent;
    if (polygon.IsShadowMask)
        polygonMeta.Flags |= AcceleratedPolygonFlagShadowMask;
    if (polygon.IsShadow)
        polygonMeta.Flags |= AcceleratedPolygonFlagShadow;
    if (polygon.Translucent && polygonMeta.Alpha5 == 0x1Fu)
        polygonMeta.Flags |= AcceleratedPolygonFlagNeedOpaquePass;
    if (polygon.FacingView)
        polygonMeta.Flags |= AcceleratedPolygonFlagFacingView;
    if (polygon.WBuffer)
        polygonMeta.Flags |= AcceleratedPolygonFlagWBuffer;
    if ((polygon.Attr & (1u << 14u)) != 0u)
        polygonMeta.Flags |= AcceleratedPolygonFlagDepthEqual;
    if ((polygon.Attr & (1u << 15u)) == 0u)
        polygonMeta.Flags |= AcceleratedPolygonFlagFogWrite;

    return polygonMeta;
}

u32 BuildAcceleratedVertexAttr(const AcceleratedPolygonMeta& polygonMeta) noexcept
{
    u32 vertexAttr = polygonMeta.PolyAttr & 0x1F00C8F0u;
    if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagFacingView))
        vertexAttr |= (1u << 8u);
    if (HasAcceleratedPolygonFlag(polygonMeta, AcceleratedPolygonFlagWBuffer))
        vertexAttr |= (1u << 9u);
    return vertexAttr;
}

AcceleratedLineEndpoints ResolveAcceleratedLineEndpoints(const Polygon& polygon) noexcept
{
    AcceleratedLineEndpoints lineEndpoints{};
    s32 lastLineX = 0;
    s32 lastLineY = 0;
    bool haveLastLineVertex = false;

    for (u32 vertexIndex = 0; vertexIndex < polygon.NumVertices && lineEndpoints.Count < 2u; vertexIndex++)
    {
        const Vertex* vertex = polygon.Vertices[vertexIndex];
        if (vertex == nullptr)
            continue;

        if (haveLastLineVertex
            && lastLineX == vertex->FinalPosition[0]
            && lastLineY == vertex->FinalPosition[1])
        {
            continue;
        }

        lineEndpoints.Vertices[lineEndpoints.Count] = vertex;
        lineEndpoints.Indices[lineEndpoints.Count] = vertexIndex;
        lineEndpoints.Count++;
        lastLineX = vertex->FinalPosition[0];
        lastLineY = vertex->FinalPosition[1];
        haveLastLineVertex = true;
    }

    return lineEndpoints;
}

u32 BuildAcceleratedRenderKey(const Polygon& polygon) noexcept
{
    u32 renderKey = (polygon.Attr >> 14) & 0x1u;
    if (!polygon.IsShadowMask)
    {
        if (polygon.Translucent)
        {
            if (polygon.IsShadow)
                renderKey |= 0x20000u;
            else
                renderKey |= 0x10000u;

            renderKey |= (polygon.Attr >> 10) & 0x2u;
            renderKey |= (polygon.Attr >> 13) & 0x4u;
            renderKey |= (polygon.Attr & 0x3F000000u) >> 16u;
            if ((polygon.Attr & 0x001F0000u) == 0x001F0000u)
                renderKey |= 0x4000u;
        }
        else
        {
            if ((polygon.Attr & 0x001F0000u) == 0u)
                renderKey |= 0x2u;
            renderKey |= (polygon.Attr & 0x3F000000u) >> 16u;
        }
    }
    else
    {
        renderKey |= 0x30000u;
    }

    return renderKey;
}

s32 ResolveAcceleratedVertexFixedX(const Vertex& vertex, int scale, bool useHiresCoordinates) noexcept
{
    const int safeScale = std::max(scale, 1);
    if (useHiresCoordinates)
        return std::clamp(vertex.HiresPosition[0] * safeScale, 0, 0xFFFF);
    return std::clamp(vertex.FinalPosition[0] << 4, 0, 0xFFFF);
}

s32 ResolveAcceleratedVertexFixedY(const Vertex& vertex, int scale, bool useHiresCoordinates) noexcept
{
    const int safeScale = std::max(scale, 1);
    if (useHiresCoordinates)
        return std::clamp(vertex.HiresPosition[1] * safeScale, 0, 0xFFFF);
    return std::clamp(vertex.FinalPosition[1] << 4, 0, 0xFFFF);
}

AcceleratedCoverageFixState ResolveAcceleratedCoverageFix(
    const Polygon& polygon,
    const AcceleratedCoverageFixConfig& config) noexcept
{
    AcceleratedCoverageFixState state{};
    if (polygon.Type == 1)
        return state;

    const bool wrapS = (polygon.TexParam & (1u << 16)) != 0u;
    const bool wrapT = (polygon.TexParam & (1u << 17)) != 0u;
    const bool mirrorS = (polygon.TexParam & (1u << 18)) != 0u;
    const bool mirrorT = (polygon.TexParam & (1u << 19)) != 0u;
    const bool isRepeat = wrapS || wrapT;
    const bool textureColor0Transparent = (polygon.TexParam & (1u << 29)) != 0u;
    const u32 textureFormat = (polygon.TexParam >> 26u) & 0x7u;
    const u32 alpha5 = (polygon.Attr >> 16u) & 0x1Fu;
    const u32 blendMode = (polygon.Attr >> 4u) & 0x3u;
    const bool depthWriteDisabled = (polygon.Attr & (1u << 11u)) == 0u;
    bool linearW = polygon.NumVertices > 0u;
    const s32 firstW = linearW ? std::max<s32>(1, polygon.FinalW[0]) : 1;
    for (u32 vertexIndex = 1; vertexIndex < polygon.NumVertices && linearW; vertexIndex++)
        linearW = std::max<s32>(1, polygon.FinalW[vertexIndex]) == firstW;
    linearW = linearW && ((static_cast<u32>(firstW) & 0x7Fu) == 0u);
    const bool paletteUiClamp =
        config.PaletteUiClampEnabled
        && polygon.Translucent
        && linearW
        && textureFormat == 3u
        && textureColor0Transparent
        && depthWriteDisabled
        && blendMode == 0u
        && alpha5 > 0u
        && alpha5 < 31u
        && !wrapS
        && !wrapT
        && !mirrorS
        && !mirrorT;

    if (config.Enabled && config.UserPx > 0.0f)
    {
        state.ApplyUserFix = isRepeat ? config.ApplyRepeat : config.ApplyClamp;
        if (state.ApplyUserFix)
            state.EffectivePx += config.UserPx;
    }

    if (!config.DisablePassiveRepeat && config.PassiveRepeatPx > 0.0f && isRepeat)
    {
        state.ApplyPassiveFix = true;
        state.EffectivePx += config.PassiveRepeatPx;
    }

    if (paletteUiClamp && config.PaletteUiClampPx > 0.0f)
    {
        state.ApplyPassiveFix = true;
        state.EffectivePx += config.PaletteUiClampPx;
    }

    state.Apply = state.EffectivePx > 0.0f;
    return state;
}

void ComputeAcceleratedCoverageExpandedVerticesFixed(
    const Polygon& polygon,
    int scale,
    bool useHiresCoordinates,
    s32 maxFixedX,
    s32 maxFixedY,
    float coverageFixPx,
    std::array<u32, 10>& outX,
    std::array<u32, 10>& outY) noexcept
{
    outX.fill(0u);
    outY.fill(0u);

    if (coverageFixPx <= 0.0f)
        return;

    float centerX = 0.0f;
    float centerY = 0.0f;
    u32 vertexCount = 0;
    std::array<float, 10> baseX{};
    std::array<float, 10> baseY{};

    for (u32 vertexIndex = 0; vertexIndex < polygon.NumVertices; vertexIndex++)
    {
        const Vertex* vertex = polygon.Vertices[vertexIndex];
        if (vertex == nullptr)
            continue;

        const s32 xFixed = ResolveAcceleratedVertexFixedX(*vertex, scale, useHiresCoordinates);
        const s32 yFixed = ResolveAcceleratedVertexFixedY(*vertex, scale, useHiresCoordinates);
        baseX[vertexIndex] = static_cast<float>(xFixed) * (1.0f / 16.0f);
        baseY[vertexIndex] = static_cast<float>(yFixed) * (1.0f / 16.0f);
        centerX += baseX[vertexIndex];
        centerY += baseY[vertexIndex];
        vertexCount++;
    }

    if (vertexCount == 0u)
        return;

    centerX /= static_cast<float>(vertexCount);
    centerY /= static_cast<float>(vertexCount);

    for (u32 vertexIndex = 0; vertexIndex < polygon.NumVertices; vertexIndex++)
    {
        if (polygon.Vertices[vertexIndex] == nullptr)
            continue;

        const float dx = baseX[vertexIndex] - centerX;
        const float dy = baseY[vertexIndex] - centerY;
        float outPosX = baseX[vertexIndex];
        float outPosY = baseY[vertexIndex];
        const float lengthSquared = (dx * dx) + (dy * dy);
        if (lengthSquared > 0.000001f)
        {
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            outPosX += dx * inverseLength * coverageFixPx;
            outPosY += dy * inverseLength * coverageFixPx;
        }

        const s32 xFixed = std::clamp(
            static_cast<s32>(std::lround(outPosX * 16.0f)),
            0,
            std::max(maxFixedX, 0));
        const s32 yFixed = std::clamp(
            static_cast<s32>(std::lround(outPosY * 16.0f)),
            0,
            std::max(maxFixedY, 0));
        outX[vertexIndex] = static_cast<u32>(xFixed);
        outY[vertexIndex] = static_cast<u32>(yFixed);
    }
}

void BuildAcceleratedScene(
    const GPU3D& gpu3d,
    const AcceleratedSceneBuildConfig& config,
    AcceleratedScene& outScene)
{
    outScene.Vertices.clear();
    outScene.Indices.clear();
    outScene.EdgeIndices.clear();
    outScene.Triangles.clear();
    outScene.Draws.clear();
    outScene.FirstTranslucentDraw = std::numeric_limits<u32>::max();

    const u32 renderPolygonCount = gpu3d.RenderNumPolygons;
    outScene.Vertices.reserve(static_cast<size_t>(renderPolygonCount) * 9u);
    outScene.Indices.reserve(static_cast<size_t>(renderPolygonCount) * 30u);
    outScene.EdgeIndices.reserve(static_cast<size_t>(renderPolygonCount) * 20u);
    outScene.Triangles.reserve(static_cast<size_t>(renderPolygonCount) * 8u);
    outScene.Draws.reserve(static_cast<size_t>(renderPolygonCount));

    const int safeScale = std::max(config.Scale, 1);
    const float maxTargetX = static_cast<float>(std::max(config.MaxFixedX, 0)) * (1.0f / 16.0f);
    const float maxTargetY = static_cast<float>(std::max(config.MaxFixedY, 0)) * (1.0f / 16.0f);

    const auto packGlColor = [](u16 r, u16 g, u16 b, u16 a) -> u32 {
        return (static_cast<u32>(std::min<u16>(255u, r >> 1u)))
            | (static_cast<u32>(std::min<u16>(255u, g >> 1u)) << 8u)
            | (static_cast<u32>(std::min<u16>(255u, b >> 1u)) << 16u)
            | (static_cast<u32>(std::min<u16>(31u, a)) << 24u);
    };

    const auto appendSceneVertex = [&](s32 xFixed,
                                       s32 yFixed,
                                       u32 zRaw,
                                       u32 wRaw,
                                       u16 finalColorR,
                                       u16 finalColorG,
                                       u16 finalColorB,
                                       u16 alpha5,
                                       s16 texCoordS,
                                       s16 texCoordT,
                                       u32 vertexAttrBase,
                                       bool preserveSignedScreenCoords = false,
                                       std::optional<u32> glColorOverride = std::nullopt) -> u16 {
        AcceleratedSceneVertex sceneVertex{};
        sceneVertex.XFixed = static_cast<u32>(std::clamp<s32>(xFixed, 0, 0xFFFF));
        sceneVertex.YFixed = static_cast<u32>(std::clamp<s32>(yFixed, 0, 0xFFFF));
        if (preserveSignedScreenCoords)
        {
            sceneVertex.X = static_cast<float>(xFixed) * (1.0f / 16.0f);
            sceneVertex.Y = static_cast<float>(yFixed) * (1.0f / 16.0f);
        }
        else
        {
            sceneVertex.X = std::clamp(static_cast<float>(sceneVertex.XFixed) * (1.0f / 16.0f), 0.0f, maxTargetX);
            sceneVertex.Y = std::clamp(static_cast<float>(sceneVertex.YFixed) * (1.0f / 16.0f), 0.0f, maxTargetY);
        }
        sceneVertex.Z = zRaw;
        sceneVertex.W = std::max<u32>(1u, wRaw);
        sceneVertex.FinalColorR = std::min<u16>(511u, finalColorR);
        sceneVertex.FinalColorG = std::min<u16>(511u, finalColorG);
        sceneVertex.FinalColorB = std::min<u16>(511u, finalColorB);
        sceneVertex.Alpha5 = std::min<u16>(31u, alpha5);
        sceneVertex.TexCoordS = texCoordS;
        sceneVertex.TexCoordT = texCoordT;

        u32 glZ = sceneVertex.Z;
        u32 zShift = 0u;
        while (glZ > 0xFFFFu)
        {
            glZ >>= 1u;
            zShift++;
        }
        sceneVertex.GlZWPacked = glZ | ((sceneVertex.W & 0xFFFFu) << 16u);
        sceneVertex.VertexAttr = vertexAttrBase | (zShift << 16u);
        sceneVertex.GlColorPacked = glColorOverride.value_or(
            packGlColor(sceneVertex.FinalColorR, sceneVertex.FinalColorG, sceneVertex.FinalColorB, sceneVertex.Alpha5));

        const u16 vertexIndex = static_cast<u16>(outScene.Vertices.size());
        outScene.Vertices.push_back(sceneVertex);
        return vertexIndex;
    };

    const auto appendTriangle = [&](AcceleratedSceneDraw& draw,
                                    u16 index0,
                                    u16 index1,
                                    u16 index2,
                                    u32 boundaryFlags,
                                    u32 packedYBounds) {
        draw.IndexCount += 3u;
        outScene.Indices.push_back(index0);
        outScene.Indices.push_back(index1);
        outScene.Indices.push_back(index2);

        AcceleratedSceneTriangle triangle{};
        triangle.Indices = {index0, index1, index2};
        triangle.BoundaryFlags = boundaryFlags;
        triangle.PackedYBounds = packedYBounds;
        outScene.Triangles.push_back(triangle);
        draw.TriangleCount++;
    };

    const auto appendEdges = [&](AcceleratedSceneDraw& draw, u32 edgeVertexCount) {
        if (edgeVertexCount < 2u)
            return;

        const u16 firstVertex = static_cast<u16>(draw.FirstVertex);
        for (u32 vertexOffset = 1; vertexOffset < edgeVertexCount; vertexOffset++)
        {
            outScene.EdgeIndices.push_back(static_cast<u16>(firstVertex + vertexOffset - 1u));
            outScene.EdgeIndices.push_back(static_cast<u16>(firstVertex + vertexOffset));
            draw.EdgeIndexCount += 2u;
        }
        outScene.EdgeIndices.push_back(static_cast<u16>(firstVertex + edgeVertexCount - 1u));
        outScene.EdgeIndices.push_back(firstVertex);
        draw.EdgeIndexCount += 2u;
    };

    const auto packYBounds = [&](const std::vector<u16>& polygonVertexIndices,
                                 bool usePolygonBounds,
                                 const Polygon& polygon) -> std::optional<u32> {
        if (usePolygonBounds)
        {
            const s32 rawTop = std::clamp(polygon.YTop, 0, config.MaxFixedY / 16);
            const s32 rawBottom = std::clamp(polygon.YBottom, 0, config.MaxFixedY / 16);
            u32 polygonYTop = static_cast<u32>(rawTop);
            u32 polygonYBot = static_cast<u32>(rawBottom);
            if (polygonYBot <= polygonYTop)
                polygonYBot = polygonYTop + 1u;
            return (polygonYTop & 0xFFFFu) | ((polygonYBot & 0xFFFFu) << 16u);
        }

        if (polygonVertexIndices.empty())
            return std::nullopt;

        u32 polygonYTop = static_cast<u32>(std::max(config.MaxFixedY / 16, 0));
        u32 polygonYBot = 0u;
        bool hasBounds = false;
        for (u16 vertexIndex : polygonVertexIndices)
        {
            if (vertexIndex >= outScene.Vertices.size())
                continue;

            const AcceleratedSceneVertex& vertex = outScene.Vertices[vertexIndex];
            const float clampedY = std::clamp(vertex.Y, 0.0f, maxTargetY);
            const u32 yTopLine = static_cast<u32>(std::floor(clampedY));
            const u32 yBottomLine = static_cast<u32>(std::ceil(clampedY));
            polygonYTop = std::min(polygonYTop, yTopLine);
            polygonYBot = std::max(polygonYBot, yBottomLine);
            hasBounds = true;
        }

        if (!hasBounds)
            return std::nullopt;
        if (polygonYBot <= polygonYTop)
            polygonYBot = polygonYTop + 1u;
        return (polygonYTop & 0xFFFFu) | ((polygonYBot & 0xFFFFu) << 16u);
    };

    for (u32 polygonIndex = 0; polygonIndex < renderPolygonCount; polygonIndex++)
    {
        const Polygon* polygon = gpu3d.RenderPolygonRAM[polygonIndex];
        if (polygon == nullptr
            || polygon->Degenerate
            || (polygon->Type == 1 ? polygon->NumVertices < 2u : polygon->NumVertices < 3u))
        {
            continue;
        }

        AcceleratedSceneDraw draw{};
        draw.SourcePolygon = polygon;
        draw.Meta = BuildAcceleratedPolygonMeta(*polygon);
        draw.CoverageFixState = ResolveAcceleratedCoverageFix(*polygon, config.CoverageFix);
        if (draw.CoverageFixState.Apply)
        {
            bool polygonTouchesClipEdge = false;
            for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
            {
                const Vertex* vertex = polygon->Vertices[vertexIndex];
                if (vertex == nullptr)
                    continue;

                const s32 xFixed = config.UseHiresCoordinates
                    ? vertex->HiresPosition[0] * safeScale
                    : vertex->FinalPosition[0] << 4;
                const s32 yFixed = config.UseHiresCoordinates
                    ? vertex->HiresPosition[1] * safeScale
                    : vertex->FinalPosition[1] << 4;
                if (xFixed < 0 || yFixed < 0 || xFixed > config.MaxFixedX || yFixed > config.MaxFixedY)
                {
                    polygonTouchesClipEdge = true;
                    break;
                }
            }
            if (polygonTouchesClipEdge)
                draw.CoverageFixState = {};
        }
        draw.FirstVertex = static_cast<u32>(outScene.Vertices.size());
        draw.FirstIndex = static_cast<u32>(outScene.Indices.size());
        draw.FirstEdgeIndex = static_cast<u32>(outScene.EdgeIndices.size());
        draw.FirstTriangle = static_cast<u32>(outScene.Triangles.size());
        draw.PrimitiveType = polygon->Type == 1 ? AcceleratedPrimitiveType::Lines : AcceleratedPrimitiveType::Triangles;

        if (outScene.FirstTranslucentDraw == std::numeric_limits<u32>::max() && polygon->Translucent)
            outScene.FirstTranslucentDraw = static_cast<u32>(outScene.Draws.size());

        u32 vertexAttrBase = BuildAcceleratedVertexAttr(draw.Meta);
        if (draw.CoverageFixState.Apply)
            vertexAttrBase |= (1u << 10u);

        std::array<u32, 10> expandedX{};
        std::array<u32, 10> expandedY{};
        if (draw.CoverageFixState.Apply)
        {
            ComputeAcceleratedCoverageExpandedVerticesFixed(
                *polygon,
                safeScale,
                config.UseHiresCoordinates,
                config.MaxFixedX,
                config.MaxFixedY,
                draw.CoverageFixState.EffectivePx,
                expandedX,
                expandedY);
        }

        std::vector<u16> polygonVertexIndices{};
        polygonVertexIndices.reserve(std::max<u32>(polygon->NumVertices + 1u, 3u));

        const auto resolveFixedCoords = [&](u32 vertexIndex) -> std::pair<s32, s32> {
            const Vertex* vertex = polygon->Vertices[vertexIndex];
            s32 xFixed = 0;
            s32 yFixed = 0;
            if (vertex != nullptr)
            {
                if (draw.CoverageFixState.Apply)
                {
                    xFixed = static_cast<s32>(expandedX[vertexIndex]);
                    yFixed = static_cast<s32>(expandedY[vertexIndex]);
                }
                else if (config.UseHiresCoordinates)
                {
                    xFixed = vertex->HiresPosition[0] * safeScale;
                    yFixed = vertex->HiresPosition[1] * safeScale;
                }
                else
                {
                    xFixed = vertex->FinalPosition[0] << 4;
                    yFixed = vertex->FinalPosition[1] << 4;
                }
            }
            return {xFixed, yFixed};
        };

        if (polygon->Type == 1)
        {
            const AcceleratedLineEndpoints lineEndpoints = ResolveAcceleratedLineEndpoints(*polygon);
            for (u32 endpointIndex = 0; endpointIndex < lineEndpoints.Count; endpointIndex++)
            {
                const Vertex* vertex = lineEndpoints.Vertices[endpointIndex];
                if (vertex == nullptr)
                    continue;

                const u32 sourceVertexIndex = lineEndpoints.Indices[endpointIndex];
                const auto [xFixed, yFixed] = resolveFixedCoords(sourceVertexIndex);
                const u16 sceneVertexIndex = appendSceneVertex(
                    xFixed,
                    yFixed,
                    polygon->FinalZ[sourceVertexIndex],
                    std::max<s32>(1, polygon->FinalW[sourceVertexIndex]),
                    static_cast<u16>(std::clamp(vertex->FinalColor[0], 0, 511)),
                    static_cast<u16>(std::clamp(vertex->FinalColor[1], 0, 511)),
                    static_cast<u16>(std::clamp(vertex->FinalColor[2], 0, 511)),
                    static_cast<u16>(draw.Meta.Alpha5),
                    static_cast<s16>(vertex->TexCoords[0]),
                    static_cast<s16>(vertex->TexCoords[1]),
                    vertexAttrBase);
                polygonVertexIndices.push_back(sceneVertexIndex);
                outScene.Indices.push_back(sceneVertexIndex);
                draw.IndexCount++;
            }

            draw.VertexCount = static_cast<u32>(outScene.Vertices.size()) - draw.FirstVertex;
            appendEdges(draw, draw.VertexCount);
            outScene.Draws.push_back(draw);
            continue;
        }

        if (config.BetterPolygons && polygon->NumVertices > 3u)
        {
            s64 centerXFixedSum = 0;
            s64 centerYFixedSum = 0;
            float centerZ = 0.0f;
            float centerReciprocalW = 0.0f;
            float centerRawR = 0.0f;
            float centerRawG = 0.0f;
            float centerRawB = 0.0f;
            float centerGlR = 0.0f;
            float centerGlG = 0.0f;
            float centerGlB = 0.0f;
            float centerS = 0.0f;
            float centerT = 0.0f;
            bool validCenter = true;

            for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
            {
                const Vertex* vertex = polygon->Vertices[vertexIndex];
                if (vertex == nullptr)
                {
                    validCenter = false;
                    break;
                }

                const auto [xFixed, yFixed] = resolveFixedCoords(vertexIndex);
                centerXFixedSum += static_cast<s64>(xFixed);
                centerYFixedSum += static_cast<s64>(yFixed);

                const float fw = static_cast<float>(std::max<s32>(1, polygon->FinalW[vertexIndex]))
                    * static_cast<float>(polygon->NumVertices);
                centerReciprocalW += 1.0f / fw;

                if (polygon->WBuffer)
                    centerZ += static_cast<float>(polygon->FinalZ[vertexIndex]) / fw;
                else
                    centerZ += static_cast<float>(polygon->FinalZ[vertexIndex]);

                const float rawR = static_cast<float>(std::clamp(vertex->FinalColor[0], 0, 511));
                const float rawG = static_cast<float>(std::clamp(vertex->FinalColor[1], 0, 511));
                const float rawB = static_cast<float>(std::clamp(vertex->FinalColor[2], 0, 511));
                centerRawR += rawR / fw;
                centerRawG += rawG / fw;
                centerRawB += rawB / fw;
                centerGlR += static_cast<float>(static_cast<u32>(rawR) >> 1u) / fw;
                centerGlG += static_cast<float>(static_cast<u32>(rawG) >> 1u) / fw;
                centerGlB += static_cast<float>(static_cast<u32>(rawB) >> 1u) / fw;
                centerS += static_cast<float>(vertex->TexCoords[0]) / fw;
                centerT += static_cast<float>(vertex->TexCoords[1]) / fw;
            }

            if (validCenter && centerReciprocalW > 0.0f)
            {
                const s32 centerXFixed = static_cast<s32>(centerXFixedSum / static_cast<s64>(polygon->NumVertices));
                const s32 centerYFixed = static_cast<s32>(centerYFixedSum / static_cast<s64>(polygon->NumVertices));
                const float centerW = 1.0f / centerReciprocalW;
                if (polygon->WBuffer)
                    centerZ *= centerW;
                else
                    centerZ /= static_cast<float>(polygon->NumVertices);
                centerRawR *= centerW;
                centerRawG *= centerW;
                centerRawB *= centerW;
                centerGlR *= centerW;
                centerGlG *= centerW;
                centerGlB *= centerW;
                centerS *= centerW;
                centerT *= centerW;

                const u32 glColorPacked =
                    (static_cast<u32>(std::clamp(static_cast<int>(centerGlR), 0, 255)) & 0xFFu)
                    | ((static_cast<u32>(std::clamp(static_cast<int>(centerGlG), 0, 255)) & 0xFFu) << 8u)
                    | ((static_cast<u32>(std::clamp(static_cast<int>(centerGlB), 0, 255)) & 0xFFu) << 16u)
                    | (draw.Meta.Alpha5 << 24u);

                const u16 centerVertexIndex = appendSceneVertex(
                    centerXFixed,
                    centerYFixed,
                    static_cast<u32>(std::max(0.0f, centerZ)),
                    std::max<u32>(1u, static_cast<u32>(centerW)),
                    static_cast<u16>(std::clamp(static_cast<int>(centerRawR), 0, 511)),
                    static_cast<u16>(std::clamp(static_cast<int>(centerRawG), 0, 511)),
                    static_cast<u16>(std::clamp(static_cast<int>(centerRawB), 0, 511)),
                    static_cast<u16>(draw.Meta.Alpha5),
                    static_cast<s16>(static_cast<s32>(centerS)),
                    static_cast<s16>(static_cast<s32>(centerT)),
                    vertexAttrBase,
                    true,
                    glColorPacked);
                polygonVertexIndices.push_back(centerVertexIndex);
            }
        }

        for (u32 vertexIndex = 0; vertexIndex < polygon->NumVertices; vertexIndex++)
        {
            const Vertex* vertex = polygon->Vertices[vertexIndex];
            if (vertex == nullptr)
                continue;

            const auto [xFixed, yFixed] = resolveFixedCoords(vertexIndex);
            const u16 sceneVertexIndex = appendSceneVertex(
                xFixed,
                yFixed,
                polygon->FinalZ[vertexIndex],
                std::max<s32>(1, polygon->FinalW[vertexIndex]),
                static_cast<u16>(std::clamp(vertex->FinalColor[0], 0, 511)),
                static_cast<u16>(std::clamp(vertex->FinalColor[1], 0, 511)),
                static_cast<u16>(std::clamp(vertex->FinalColor[2], 0, 511)),
                static_cast<u16>(draw.Meta.Alpha5),
                static_cast<s16>(vertex->TexCoords[0]),
                static_cast<s16>(vertex->TexCoords[1]),
                vertexAttrBase);
            polygonVertexIndices.push_back(sceneVertexIndex);
        }

        draw.VertexCount = static_cast<u32>(outScene.Vertices.size()) - draw.FirstVertex;
        const std::optional<u32> packedYBounds = packYBounds(
            polygonVertexIndices,
            !config.UseHiresCoordinates,
            *polygon);
        if (!packedYBounds.has_value())
            continue;

        if (config.BetterPolygons && polygon->NumVertices > 3u && !polygonVertexIndices.empty())
        {
            const u16 centerVertexIndex = polygonVertexIndices.front();
            const u32 firstOuterIndex = 1u;
            for (u32 outerVertexIndex = firstOuterIndex + 1u; outerVertexIndex < polygonVertexIndices.size(); outerVertexIndex++)
            {
                appendTriangle(
                    draw,
                    centerVertexIndex,
                    polygonVertexIndices[outerVertexIndex - 1u],
                    polygonVertexIndices[outerVertexIndex],
                    AcceleratedTriangleBoundaryEdge0,
                    *packedYBounds);
            }
            if (polygonVertexIndices.size() > 3u)
            {
                appendTriangle(
                    draw,
                    centerVertexIndex,
                    polygonVertexIndices.back(),
                    polygonVertexIndices[firstOuterIndex],
                    AcceleratedTriangleBoundaryEdge0,
                    *packedYBounds);
            }
        }
        else
        {
            for (u32 vertexIndex = 2u; vertexIndex < polygonVertexIndices.size(); vertexIndex++)
            {
                u32 boundaryFlags = AcceleratedTriangleBoundaryEdge0;
                if (vertexIndex + 1u == polygonVertexIndices.size())
                    boundaryFlags |= AcceleratedTriangleBoundaryEdge1;
                if (vertexIndex == 2u)
                    boundaryFlags |= AcceleratedTriangleBoundaryEdge2;
                appendTriangle(
                    draw,
                    polygonVertexIndices[0],
                    polygonVertexIndices[vertexIndex - 1u],
                    polygonVertexIndices[vertexIndex],
                    boundaryFlags,
                    *packedYBounds);
            }
        }

        appendEdges(draw, draw.VertexCount);
        outScene.Draws.push_back(draw);
    }
}

}

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
