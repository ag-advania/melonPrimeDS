#pragma once

#include <array>
#include <limits>
#include <vector>

#include "GPU3D.h"
#include "types.h"

namespace melonDS
{

struct AcceleratedCoverageFixConfig
{
    bool Enabled = false;
    float UserPx = 0.0f;
    bool ApplyRepeat = true;
    bool ApplyClamp = false;
    float PassiveRepeatPx = 0.0f;
    bool DisablePassiveRepeat = false;
    bool PaletteUiClampEnabled = false;
    float PaletteUiClampPx = 0.0f;
};

struct AcceleratedCoverageFixState
{
    bool Apply = false;
    bool ApplyUserFix = false;
    bool ApplyPassiveFix = false;
    float EffectivePx = 0.0f;
};

enum AcceleratedPolygonFlags : u32
{
    AcceleratedPolygonFlagTranslucent = 1u << 0u,
    AcceleratedPolygonFlagShadowMask = 1u << 1u,
    AcceleratedPolygonFlagShadow = 1u << 2u,
    AcceleratedPolygonFlagNeedOpaquePass = 1u << 3u,
    AcceleratedPolygonFlagFacingView = 1u << 4u,
    AcceleratedPolygonFlagWBuffer = 1u << 5u,
    AcceleratedPolygonFlagDepthEqual = 1u << 6u,
    AcceleratedPolygonFlagFogWrite = 1u << 7u,
};

struct AcceleratedPolygonMeta
{
    u32 RenderKey = 0;
    u32 Flags = 0;
    u32 PolyAttr = 0;
    u32 PolyId = 0;
    u32 Alpha5 = 0;
};

struct AcceleratedLineEndpoints
{
    std::array<const Vertex*, 2> Vertices{};
    std::array<u32, 2> Indices{};
    u32 Count = 0;
};

enum class AcceleratedPrimitiveType : u8
{
    Lines = 0,
    Triangles = 1,
};

enum AcceleratedTriangleBoundaryFlags : u32
{
    AcceleratedTriangleBoundaryEdge0 = 1u << 0u,
    AcceleratedTriangleBoundaryEdge1 = 1u << 1u,
    AcceleratedTriangleBoundaryEdge2 = 1u << 2u,
};

struct AcceleratedSceneVertex
{
    u32 XFixed = 0;
    u32 YFixed = 0;
    float X = 0.0f;
    float Y = 0.0f;
    u32 Z = 0;
    u32 W = 1;
    u32 GlZWPacked = 0;
    u16 FinalColorR = 0;
    u16 FinalColorG = 0;
    u16 FinalColorB = 0;
    u16 Alpha5 = 0;
    s16 TexCoordS = 0;
    s16 TexCoordT = 0;
    u32 VertexAttr = 0;
    u32 GlColorPacked = 0;
};

struct AcceleratedSceneTriangle
{
    std::array<u16, 3> Indices{};
    u32 BoundaryFlags = 0;
    u32 PackedYBounds = 0;
};

struct AcceleratedSceneDraw
{
    const Polygon* SourcePolygon = nullptr;
    AcceleratedPolygonMeta Meta{};
    AcceleratedPrimitiveType PrimitiveType = AcceleratedPrimitiveType::Triangles;
    AcceleratedCoverageFixState CoverageFixState{};
    u32 FirstVertex = 0;
    u32 VertexCount = 0;
    u32 FirstIndex = 0;
    u32 IndexCount = 0;
    u32 FirstEdgeIndex = 0;
    u32 EdgeIndexCount = 0;
    u32 FirstTriangle = 0;
    u32 TriangleCount = 0;
};

struct AcceleratedScene
{
    std::vector<AcceleratedSceneVertex> Vertices;
    std::vector<u16> Indices;
    std::vector<u16> EdgeIndices;
    std::vector<AcceleratedSceneTriangle> Triangles;
    std::vector<AcceleratedSceneDraw> Draws;
    u32 FirstTranslucentDraw = std::numeric_limits<u32>::max();
};

struct AcceleratedSceneBuildConfig
{
    int Scale = 1;
    bool BetterPolygons = false;
    bool UseHiresCoordinates = false;
    s32 MaxFixedX = 0xFFFF;
    s32 MaxFixedY = 0xFFFF;
    AcceleratedCoverageFixConfig CoverageFix{};
};

[[nodiscard]] bool HasAcceleratedPolygonFlag(const AcceleratedPolygonMeta& polygonMeta, u32 flag) noexcept;
AcceleratedPolygonMeta BuildAcceleratedPolygonMeta(const Polygon& polygon) noexcept;
u32 BuildAcceleratedVertexAttr(const AcceleratedPolygonMeta& polygonMeta) noexcept;
AcceleratedLineEndpoints ResolveAcceleratedLineEndpoints(const Polygon& polygon) noexcept;
u32 BuildAcceleratedRenderKey(const Polygon& polygon) noexcept;
s32 ResolveAcceleratedVertexFixedX(const Vertex& vertex, int scale, bool useHiresCoordinates) noexcept;
s32 ResolveAcceleratedVertexFixedY(const Vertex& vertex, int scale, bool useHiresCoordinates) noexcept;
AcceleratedCoverageFixState ResolveAcceleratedCoverageFix(
    const Polygon& polygon,
    const AcceleratedCoverageFixConfig& config) noexcept;
void ComputeAcceleratedCoverageExpandedVerticesFixed(
    const Polygon& polygon,
    int scale,
    bool useHiresCoordinates,
    s32 maxFixedX,
    s32 maxFixedY,
    float coverageFixPx,
    std::array<u32, 10>& outX,
    std::array<u32, 10>& outY) noexcept;
void BuildAcceleratedScene(
    const GPU3D& gpu3d,
    const AcceleratedSceneBuildConfig& config,
    AcceleratedScene& outScene);

}
