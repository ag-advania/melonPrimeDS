#ifndef MELON_PRIME_HUD_SCOREBOARD_NATIVE_GEOMETRY_H
#define MELON_PRIME_HUD_SCOREBOARD_NATIVE_GEOMETRY_H

#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudGeometry.h"

// =========================================================================
//  Native match scoreboard geometry.
//
//  MPH lays the START scoreboard out on the 256x192 DS screen from a handful
//  of immediates. All seven ROM versions use the same constants and the same
//  relative placement; only the data addresses differ, so this module is
//  version-independent and there is no per-ROM table.
//
//  Vertical placement is not a fixed Y per row. The renderer first measures
//  the whole board, then centres that height on Y=104 -- eight pixels below
//  the physical screen centre -- and walks the rows downwards. The measuring
//  pass and the walking pass must agree, so both are expressed here as the
//  same LayoutWalker: a height is simply the distance one walk covers.
//
//  The ROM halves the height with an arithmetic shift rather than a divide,
//  which is visible for odd heights: 104 - (H >> 1) sits half a pixel above
//  104 - H/2.0f. Reproducing the shift is the difference between native pixel
//  parity and a board that is consistently half a pixel low.
//
//  Reference: mphCodex mphAnalysis/Battle/Scoreboard/
//             Match-Scoreboard-Native-Geometry-AllVersions-JP1_0.md
//             (JP1_0 renderer 0202A99C, player row 0202BC38, height 0202BD30).
//
//  Deliberately free of Qt, melonDS and Custom HUD state so the layout can be
//  exercised directly by tools/testing/hud-scoreboard-native-geometry-tests.cpp.
// =========================================================================

namespace MelonPrime::NativeScoreboard {

// The board is authored against the DS screen, not the output surface.
inline constexpr int kScreenWidth = 256;
inline constexpr int kScreenHeight = 192;

// The board is centred on this Y, not on the screen centre (96).
inline constexpr int kCenterY = 104;

// Horizontal anchors. The two statistics columns are fixed for every game
// mode: Points/Time/Octoliths/Kills/Deaths all land on the same two centres.
inline constexpr int kTeamNameX = 42;
inline constexpr int kPlayerBaseX = 60;
inline constexpr int kPortraitX = kPlayerBaseX - 40;    // 20, HUD object top-left
inline constexpr int kStarsLeftX = kPlayerBaseX;        // 60, HUD object top-left
inline constexpr int kStarsRightX = kPlayerBaseX + 32;  // 92, second stars piece
inline constexpr int kNicknameX = kPlayerBaseX + 32;    // 92, text centre
inline constexpr int kValue1X = 160;                    // text centre
inline constexpr int kValue2X = 215;                    // text centre
inline constexpr int kGameOverX = 128;                  // text centre

// Player row offsets from the row's own Y. The row Y is not the top of the
// row: the portrait rises above it and the nickname sits just above the stars.
inline constexpr int kPortraitDY = -13;
inline constexpr int kNicknameDY = -9;

// The four vertical spacings the ROM keeps as 12-bit fixed point
// (0xD000, 0x4000, 0x12000, 0x1C000).
inline constexpr int kStartSpace = 13;
inline constexpr int kTeamHeaderSpace = 4;
inline constexpr int kTeamLineSpace = 18;
inline constexpr int kPlayerSpace = 28;

// One downward walk over the board.
//
// Each Take* call returns the Y the row draws at and advances past it, so a
// caller cannot place a row without also paying its spacing. The second and
// later Team headers pull back by kTeamHeaderSpace before they are placed --
// that is why the walker, and not the caller, owns "have I seen a team yet".
class LayoutWalker
{
public:
    constexpr LayoutWalker() noexcept = default;
    constexpr explicit LayoutWalker(int startY) noexcept : y_(startY) {}

    // Y the next row would take; after a full walk, the board's bottom edge.
    constexpr int Y() const noexcept { return y_; }

    // Ending boards only: GAME OVER sits one start space above the header.
    constexpr int TakeGameOverRow() noexcept { return Advance(kStartSpace); }

    constexpr int TakeHeaderRow() noexcept { return Advance(kStartSpace); }

    constexpr int TakeTeamRow() noexcept
    {
        if (seenTeamRow_)
            y_ -= kTeamHeaderSpace;
        seenTeamRow_ = true;
        return Advance(kTeamLineSpace);
    }

    constexpr int TakePlayerRow() noexcept { return Advance(kPlayerSpace); }

private:
    constexpr int Advance(int space) noexcept
    {
        const int y = y_;
        y_ += space;
        return y;
    }

    int y_ = 0;
    bool seenTeamRow_ = false;
};

// Height of a board with the given row counts.
//
// Measured by walking from zero rather than by a closed-form sum: the walk is
// the placement rule, so the two cannot drift apart. The result does not
// depend on how the rows interleave, only on how many there are, because the
// team pull-back applies once per team after the first either way.
//
// Rows that are not drawn -- inactive players -- must not be counted.
constexpr int ComputeHeight(int playerRowCount, int teamRowCount,
                            bool ending = false) noexcept
{
    LayoutWalker walker;
    if (ending)
        walker.TakeGameOverRow();
    walker.TakeHeaderRow();
    for (int i = 0; i < teamRowCount; ++i)
        walker.TakeTeamRow();
    for (int i = 0; i < playerRowCount; ++i)
        walker.TakePlayerRow();
    return walker.Y();
}

// Top edge of a board of this height. The shift is the ROM's, not a divide.
constexpr int StartY(int height) noexcept
{
    return kCenterY - (height >> 1);
}

// C++17 leaves >> on a negative value implementation-defined; heights are
// positive here, but pin the assumption rather than leave it implicit.
static_assert((-2 >> 1) == -1,
    "native scoreboard centring assumes an arithmetic right shift");

// The worked examples in the reference document, checked at compile time so a
// change to the spacing constants cannot pass unnoticed.
static_assert(ComputeHeight(4, 0) == 125, "4-player FFA height");
static_assert(StartY(ComputeHeight(4, 0)) == 42, "4-player FFA start Y");
static_assert(ComputeHeight(2, 0) == 69, "2-player FFA height");
static_assert(StartY(ComputeHeight(2, 0)) == 70, "2-player FFA start Y");
static_assert(ComputeHeight(4, 2) == 157, "4-player 2-team height");
static_assert(StartY(ComputeHeight(4, 2)) == 26, "4-player 2-team start Y");
static_assert(ComputeHeight(4, 0, true) == 138, "4-player FFA ending height");
static_assert(ComputeHeight(4, 2, true) == 170, "4-player 2-team ending height");

// -------------------------------------------------------------------------
//  Custom HUD adaptation.
//
//  Everything above is the ROM's. Everything below is this port's, and is
//  shared rather than duplicated because the runtime board, the in-game edit
//  mode and the settings-dialog preview are three translation units that must
//  agree on where the board lands.
// -------------------------------------------------------------------------

// Text cells.
//
// The ROM draws text from a centre with no box at all, using a DS font whose
// glyph widths a Qt font does not reproduce, so every native text anchor needs
// a cell here. The widths are fixed rather than measured from the content on
// purpose: a value that gains a digit keeps its cell, which keeps the
// per-frame refresh on its cheap path instead of forcing a full plan rebuild.
inline constexpr int kValueCellHalfW = 27; // 160+-27 and 215+-27 do not meet
inline constexpr int kNameCellHalfW = 40;  // 92+-40 clears the portrait and column 1
inline constexpr int kTeamCellHalfW = 40;  // 42+-40 stays on screen

static_assert(kValue1X + kValueCellHalfW < kValue2X - kValueCellHalfW,
    "the two statistics columns must not overlap");
static_assert(kNicknameX + kNameCellHalfW <= kValue1X - kValueCellHalfW,
    "the nickname cell must not run into column 1");
static_assert(kTeamNameX - kTeamCellHalfW >= 0
    && kValue2X + kValueCellHalfW <= kScreenWidth,
    "the board must stay on the DS screen");

// User placement on top of the native geometry.
//
// Scale % grows the board about its own centre so it stays centred, and the
// offsets then move the result. The anchor and the gap settings describe the
// auto layout's table and are deliberately not part of this: the ROM board
// owns its own centring and its own spacing.
//
// Asset and font sizes follow Scale % through their own paths, so this carries
// positions and cell widths only.
struct Transform {
    static constexpr float kScaleAnchorX = static_cast<float>(kScreenWidth) * 0.5f;
    static constexpr float kScaleAnchorY = static_cast<float>(kCenterY);

    float scale = 1.0f;
    float ofsX = 0.0f;
    float ofsY = 0.0f;

    constexpr float X(int nativeX) const noexcept
    {
        return kScaleAnchorX + (static_cast<float>(nativeX) - kScaleAnchorX) * scale + ofsX;
    }
    constexpr float Y(int nativeY) const noexcept
    {
        return kScaleAnchorY + (static_cast<float>(nativeY) - kScaleAnchorY) * scale + ofsY;
    }
    constexpr float Len(int nativeLength) const noexcept
    {
        return static_cast<float>(nativeLength) * scale;
    }
};

struct Box {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// The hunter portrait cell.
//
// The ROM anchors the portrait's top-left at (kPortraitX, rowY + kPortraitDY)
// and takes its size from HUD asset metadata this port does not read, so the
// cell below is this port's own and is not a claim about the ROM's artwork.
// Two things about the ROM do pin it down, though: it hangs the portrait
// thirteen pixels above the row, so the band it occupies is thirteen either
// side of the row; and the board's HUD objects come in 32-pixel pieces, which
// is how the stars are tiled at +32. A Custom HUD portrait sized by its own
// setting therefore centres in a 32-wide, 26-tall cell at that anchor, rather
// than sharing the ROM's top-left corner -- pinning the corner leaves any
// smaller asset visibly high and to the left of its own row.
inline constexpr int kPortraitCellWidth = kStarsRightX - kStarsLeftX;       // 32
inline constexpr int kPortraitCenterX = kPortraitX + kPortraitCellWidth / 2; // 36

static_assert(kPortraitCellWidth == 32, "the portrait cell is one HUD object wide");
static_assert(kPortraitX + kPortraitCellWidth <= kStarsLeftX,
    "the portrait cell must not reach the stars");

// The stars.
//
// The ROM draws two pieces, one at kStarsLeftX and one at kStarsRightX, each a
// piece wide; the pair therefore spans 64 pixels and its centre is kStarsRightX
// -- the very X the nickname centres on. Custom HUD has a single combined asset
// per star count, so it centres on that midpoint and is capped at the pair's
// width. Hanging it off the left piece's corner instead puts it visibly left of
// the name above it, which is not what the ROM looks like.
inline constexpr int kStarsCenterX = kStarsRightX;                      // 92
inline constexpr int kStarsMaxWidth = (kStarsRightX - kStarsLeftX) * 2; // 64

static_assert(kStarsCenterX == kNicknameX,
    "the stars pair and the nickname share a centre");
static_assert(kStarsCenterX - kStarsMaxWidth / 2 == kStarsLeftX,
    "a full-width stars pair starts at the ROM's left piece");
static_assert(kStarsCenterX + kStarsMaxWidth / 2 <= kValue1X - kValueCellHalfW,
    "the stars pair must not run into column 1");

// An image cell centred on both axes.
//
// The asset keeps its own pixel size -- it is already scaled by its height
// setting -- and centres on the anchor, so changing that setting grows the
// image about the ROM's position instead of away from its corner. maxNativeWidth
// caps the width in native units; zero means no cap.
inline Box CenteredImageCell(int centerX, int centerY,
                             float width, float height,
                             const Transform& tf, int maxNativeWidth = 0)
{
    float w = width;
    if (maxNativeWidth > 0) {
        const float cap = tf.Len(maxNativeWidth);
        if (w > cap)
            w = cap;
    }
    return Box{tf.X(centerX) - w * 0.5f, tf.Y(centerY) - height * 0.5f,
               w, height};
}

// The cell a native text anchor draws into.
//
// Every cell on the board centres on its anchor, on both axes: horizontally so
// centre-aligned text lands on the ROM's own X, vertically so a Qt line that is
// taller or shorter than the DS one grows about the anchor instead of hanging
// off one edge of it.
//
// Vertical centring is a port-side choice, and it is the one place the board
// leaves the ROM: the ROM treats a text Y as the top of its own 9-pixel glyph
// line, so its text sits half a line lower than this. Anchoring the top instead
// would mean every element on a row -- portrait, stars, values, all sized by
// their own settings -- lines up only when those sizes happen to match the
// ROM's. Sharing a centre makes a row read as a row at any size.
inline Box TextCell(int centerX, int halfWidth, int anchorY,
                    float lineHeight, const Transform& tf)
{
    return Box{tf.X(centerX - halfWidth), tf.Y(anchorY) - lineHeight * 0.5f,
               tf.Len(halfWidth * 2), lineHeight};
}

// The board's box.
//
// Its native extent follows from the row counts alone: every element sits
// inside the ROM's own height, and the widest cell edge is column 2's, which
// no portrait or stars asset can reach. Left edge is the Team cell when there
// are Team rows and the portrait otherwise.
// topTextOverhang is how far the header line rises above the board's own top
// edge, which is half a text line now that cells centre on their anchors. The
// bottom needs no such allowance: the last player row keeps most of its 28
// pixels below whatever it draws.
inline Box ComputePanelBox(int playerRowCount, int teamRowCount,
                           const Transform& tf, bool ending = false,
                           float topTextOverhang = 0.0f) noexcept
{
    const int height = ComputeHeight(playerRowCount, teamRowCount, ending);
    const int left = teamRowCount > 0 ? kTeamNameX - kTeamCellHalfW : kPortraitX;
    const int right = kValue2X + kValueCellHalfW;
    const float overhang = topTextOverhang > 0.0f ? topTextOverhang : 0.0f;
    return Box{tf.X(left), tf.Y(StartY(height)) - overhang,
               tf.Len(right - left), tf.Len(height) + overhang};
}

// The anchor value that means "leave the board where the ROM puts it".
//
// The ROM board is already centred -- on Y=104, eight pixels below the screen
// centre, and on its own columns rather than the screen's middle -- so Centre
// is the anchor that has to mean native placement if the default is to be
// pixel-exact. Every other anchor pulls the board box onto that screen point
// like any other HUD element, so the native board can still be put in a
// corner; only its internal geometry is fixed, not where the board sits.
inline constexpr int kRomAnchor = 4;

// Where the board goes, for an anchor and the user's offsets.
//
// anchorPointX/Y is the screen point the anchor and the offsets already
// resolve to (HudGeometry::ApplyAnchor), including the top-screen stretch, so
// this must not add the offsets to it a second time. At kRomAnchor there is no
// anchor point to pull onto and the offsets move the ROM position directly.
inline Transform ResolveTransform(float scale, int anchor,
                                  float ofsX, float ofsY,
                                  int anchorPointX, int anchorPointY,
                                  int playerRowCount, int teamRowCount,
                                  bool ending = false)
{
    const int safeAnchor = anchor < 0 ? 0 : (anchor > 8 ? 8 : anchor);
    if (safeAnchor == kRomAnchor)
        return Transform{scale, ofsX, ofsY};

    const Box romBox = ComputePanelBox(playerRowCount, teamRowCount,
                                       Transform{scale, 0.0f, 0.0f}, ending);
    float x = static_cast<float>(anchorPointX);
    float y = static_cast<float>(anchorPointY);
    HudGeometry::ApplyRectAnchorF(x, y, romBox.w, romBox.h,
                                  safeAnchor % 3, safeAnchor / 3);
    return Transform{scale, x - romBox.x, y - romBox.y};
}

} // namespace MelonPrime::NativeScoreboard

#endif // MELONPRIME_CUSTOM_HUD

#endif // MELON_PRIME_HUD_SCOREBOARD_NATIVE_GEOMETRY_H
