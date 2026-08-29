// Deterministic tests for the native match scoreboard geometry.
//
// The module reproduces MPH's own START scoreboard layout, so the expectations
// below are the worked examples from the reference document rather than
// whatever the module happens to produce. They are written out row by row --
// no loops sharing the module's own walk -- because a walker that drifted by
// one spacing would still be self-consistent.
//
// Reference: mphCodex mphAnalysis/Battle/Scoreboard/
//            Match-Scoreboard-Native-Geometry-AllVersions-JP1_0.md

#include "MelonPrimeHudScoreboardNativeGeometry.h"

#include <cstdio>
#include <vector>

namespace Native = MelonPrime::NativeScoreboard;

namespace {

int g_failures = 0;

void CheckInt(int actual, int expected, const char* what)
{
    if (actual != expected) {
        std::printf("FAIL: %s: expected %d, got %d\n", what, expected, actual);
        ++g_failures;
    }
}

void Check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

// -------------------------------------------------------------------------
//  Section 14.1 -- four-player free-for-all.
// -------------------------------------------------------------------------
void TestFourPlayerFreeForAll()
{
    const int height = Native::ComputeHeight(4, 0);
    CheckInt(height, 125, "4-player FFA height");
    CheckInt(Native::StartY(height), 42, "4-player FFA start Y");

    Native::LayoutWalker walker(Native::StartY(height));
    CheckInt(walker.TakeHeaderRow(), 42, "FFA header Y");
    CheckInt(walker.TakePlayerRow(), 55, "FFA player 1 Y");
    CheckInt(walker.TakePlayerRow(), 83, "FFA player 2 Y");
    CheckInt(walker.TakePlayerRow(), 111, "FFA player 3 Y");
    CheckInt(walker.TakePlayerRow(), 139, "FFA player 4 Y");
    CheckInt(walker.Y(), Native::StartY(height) + height,
             "FFA walk covers exactly the measured height");
}

// -------------------------------------------------------------------------
//  Section 14.2 -- two-player free-for-all.
// -------------------------------------------------------------------------
void TestTwoPlayerFreeForAll()
{
    const int height = Native::ComputeHeight(2, 0);
    CheckInt(height, 69, "2-player FFA height");
    CheckInt(Native::StartY(height), 70, "2-player FFA start Y");

    Native::LayoutWalker walker(Native::StartY(height));
    CheckInt(walker.TakeHeaderRow(), 70, "2-player header Y");
    CheckInt(walker.TakePlayerRow(), 83, "2-player player 1 Y");
    CheckInt(walker.TakePlayerRow(), 111, "2-player player 2 Y");
}

// -------------------------------------------------------------------------
//  Section 14.3 -- 2v2. The second Team header pulls back four pixels.
// -------------------------------------------------------------------------
void TestTwoTeamsEvenSplit()
{
    const int height = Native::ComputeHeight(4, 2);
    CheckInt(height, 157, "2v2 height");
    CheckInt(Native::StartY(height), 26, "2v2 start Y");

    Native::LayoutWalker walker(Native::StartY(height));
    CheckInt(walker.TakeHeaderRow(), 26, "2v2 header Y");
    CheckInt(walker.TakeTeamRow(), 39, "2v2 first team header Y");
    CheckInt(walker.TakePlayerRow(), 57, "2v2 team 1 player 1 Y");
    CheckInt(walker.TakePlayerRow(), 85, "2v2 team 1 player 2 Y");
    CheckInt(walker.TakeTeamRow(), 109, "2v2 second team header Y");
    CheckInt(walker.TakePlayerRow(), 127, "2v2 team 2 player 1 Y");
    CheckInt(walker.TakePlayerRow(), 155, "2v2 team 2 player 2 Y");
    CheckInt(walker.Y(), Native::StartY(height) + height,
             "2v2 walk covers exactly the measured height");
}

// -------------------------------------------------------------------------
//  Section 14.4 -- 1v3. Same height and start, different Team header Y.
//
//  This is the case that forbids a fixed Team header position.
// -------------------------------------------------------------------------
void TestTwoTeamsUnevenSplit()
{
    const int height = Native::ComputeHeight(4, 2);
    CheckInt(Native::StartY(height), 26, "1v3 start Y matches 2v2");

    Native::LayoutWalker walker(Native::StartY(height));
    CheckInt(walker.TakeHeaderRow(), 26, "1v3 header Y");
    CheckInt(walker.TakeTeamRow(), 39, "1v3 first team header Y");
    CheckInt(walker.TakePlayerRow(), 57, "1v3 team 1 player Y");
    CheckInt(walker.TakeTeamRow(), 81, "1v3 second team header Y");
    CheckInt(walker.TakePlayerRow(), 99, "1v3 team 2 player 1 Y");
    CheckInt(walker.TakePlayerRow(), 127, "1v3 team 2 player 2 Y");
    CheckInt(walker.TakePlayerRow(), 155, "1v3 team 2 player 3 Y");
    CheckInt(walker.Y(), Native::StartY(height) + height,
             "1v3 walk covers exactly the measured height");
}

// -------------------------------------------------------------------------
//  Section 15 -- Ending boards carry a GAME OVER row.
// -------------------------------------------------------------------------
void TestEndingBoards()
{
    const int ffaHeight = Native::ComputeHeight(4, 0, true);
    CheckInt(ffaHeight, 138, "4-player FFA ending height");
    CheckInt(Native::StartY(ffaHeight), 35, "4-player FFA ending start Y");

    Native::LayoutWalker ffa(Native::StartY(ffaHeight));
    CheckInt(ffa.TakeGameOverRow(), 35, "ending GAME OVER Y");
    CheckInt(ffa.TakeHeaderRow(), 48, "ending header Y");
    CheckInt(ffa.TakePlayerRow(), 61, "ending player 1 Y");
    CheckInt(ffa.TakePlayerRow(), 89, "ending player 2 Y");
    CheckInt(ffa.TakePlayerRow(), 117, "ending player 3 Y");
    CheckInt(ffa.TakePlayerRow(), 145, "ending player 4 Y");

    const int teamHeight = Native::ComputeHeight(4, 2, true);
    CheckInt(teamHeight, 170, "2v2 ending height");
    CheckInt(Native::StartY(teamHeight), 19, "2v2 ending start Y");

    Native::LayoutWalker teams(Native::StartY(teamHeight));
    CheckInt(teams.TakeGameOverRow(), 19, "2v2 ending GAME OVER Y");
    CheckInt(teams.TakeHeaderRow(), 32, "2v2 ending header Y");
    CheckInt(teams.TakeTeamRow(), 45, "2v2 ending first team header Y");
    CheckInt(teams.TakePlayerRow(), 63, "2v2 ending team 1 player 1 Y");
    CheckInt(teams.TakePlayerRow(), 91, "2v2 ending team 1 player 2 Y");
    CheckInt(teams.TakeTeamRow(), 115, "2v2 ending second team header Y");
    CheckInt(teams.TakePlayerRow(), 133, "2v2 ending team 2 player 1 Y");
    CheckInt(teams.TakePlayerRow(), 161, "2v2 ending team 2 player 2 Y");
}

// -------------------------------------------------------------------------
//  Section 11 -- the ROM halves the height with a shift, not a divide.
//
//  Every odd height must land one pixel lower than a float halving would, so
//  a "height / 2.0f" regression cannot pass unnoticed.
// -------------------------------------------------------------------------
void TestIntegerCentring()
{
    int oddHeights = 0;
    for (int players = 0; players <= 4; ++players) {
        for (int teams = 0; teams <= 2; ++teams) {
            for (int ending = 0; ending <= 1; ++ending) {
                const int height = Native::ComputeHeight(players, teams, ending != 0);
                const int expected = Native::kCenterY - (height >> 1);
                CheckInt(Native::StartY(height), expected, "start Y uses the ROM shift");
                if ((height & 1) != 0) {
                    ++oddHeights;
                    const int floatCentred = static_cast<int>(
                        Native::kCenterY - height / 2.0f);
                    Check(Native::StartY(height) != floatCentred,
                          "odd height must differ from float centring");
                }
            }
        }
    }
    Check(oddHeights > 0, "the sweep must actually reach an odd height");
}

// -------------------------------------------------------------------------
//  Section 10 -- the height depends on the row counts, not their order.
//
//  The Custom HUD counts rows and then walks them; that is only sound if the
//  two agree for every interleaving, including a team block of one player.
// -------------------------------------------------------------------------
void TestHeightIsIndependentOfInterleaving()
{
    // Each case is a drawn row sequence: true = Team header, false = player.
    const std::vector<std::vector<bool>> sequences = {
        {false},
        {false, false, false, false},
        {true, false, false, true, false, false},
        {true, false, true, false, false, false},
        {true, false, false, false, true, false},
        {true, false, false, false, false},
    };

    for (const std::vector<bool>& sequence : sequences) {
        int players = 0;
        int teams = 0;
        for (bool isTeam : sequence)
            ++(isTeam ? teams : players);

        const int height = Native::ComputeHeight(players, teams);
        Native::LayoutWalker walker(Native::StartY(height));
        walker.TakeHeaderRow();
        for (bool isTeam : sequence) {
            if (isTeam)
                walker.TakeTeamRow();
            else
                walker.TakePlayerRow();
        }
        CheckInt(walker.Y(), Native::StartY(height) + height,
                 "measured height matches the walk for this row order");
    }
}

// -------------------------------------------------------------------------
//  The Custom HUD adaptation: cells and the user transform.
// -------------------------------------------------------------------------
void TestPanelBoxAndTransform()
{
    const Native::Transform identity;

    // Free-for-all: the left edge is the portrait, the right edge is column 2.
    const Native::Box ffa = Native::ComputePanelBox(4, 0, identity);
    CheckInt(static_cast<int>(ffa.y), 42, "FFA panel top is the board start");
    CheckInt(static_cast<int>(ffa.h), 125, "FFA panel height is the board height");

    // With a text line to cover, the box grows upwards by half of it so the
    // header, which centres on the board's top edge, is not left hanging out.
    const Native::Box withText = Native::ComputePanelBox(4, 0, identity, false, 5.0f);
    CheckInt(static_cast<int>(withText.y), 37, "the box rises by the overhang");
    CheckInt(static_cast<int>(withText.h), 130, "the box keeps its bottom edge");
    CheckInt(static_cast<int>(withText.y + withText.h),
             static_cast<int>(ffa.y + ffa.h), "the bottom edge does not move");
    const Native::Box header = Native::TextCell(
        Native::kValue1X, Native::kValueCellHalfW, 42, 10.0f, identity);
    const Native::Box covering = Native::ComputePanelBox(4, 0, identity, false, 5.0f);
    Check(header.y >= covering.y, "the box covers the header line");
    CheckInt(static_cast<int>(ffa.x), Native::kPortraitX, "FFA panel left is the portrait");
    CheckInt(static_cast<int>(ffa.x + ffa.w),
             Native::kValue2X + Native::kValueCellHalfW, "FFA panel right is column 2");

    // Team rows push the left edge out to the Team cell.
    const Native::Box teams = Native::ComputePanelBox(4, 2, identity);
    CheckInt(static_cast<int>(teams.x), Native::kTeamNameX - Native::kTeamCellHalfW,
             "team panel left is the Team cell");
    CheckInt(static_cast<int>(teams.y), 26, "team panel top is the board start");

    // The whole board stays on the DS screen at 100%.
    Check(teams.x >= 0.0f
              && teams.x + teams.w <= static_cast<float>(Native::kScreenWidth),
          "the board fits the DS screen width");
    Check(teams.y >= 0.0f
              && teams.y + teams.h <= static_cast<float>(Native::kScreenHeight),
          "the board fits the DS screen height");

    // Offsets translate; they must not scale anything.
    const Native::Transform moved{1.0f, 7.0f, -5.0f};
    const Native::Box movedBox = Native::ComputePanelBox(4, 2, moved);
    CheckInt(static_cast<int>(movedBox.x - teams.x), 7, "offset X translates the board");
    CheckInt(static_cast<int>(movedBox.y - teams.y), -5, "offset Y translates the board");
    CheckInt(static_cast<int>(movedBox.w), static_cast<int>(teams.w),
             "offset X does not resize the board");
    CheckInt(static_cast<int>(movedBox.h), static_cast<int>(teams.h),
             "offset Y does not resize the board");

    // Scale grows the board about the screen centre and the board's own
    // centre line, so those two stay put at any scale.
    const Native::Transform doubled{2.0f, 0.0f, 0.0f};
    const Native::Box doubledBox = Native::ComputePanelBox(4, 2, doubled);
    CheckInt(static_cast<int>(doubledBox.w), static_cast<int>(teams.w) * 2,
             "scale doubles the board width");
    CheckInt(static_cast<int>(doubledBox.h), static_cast<int>(teams.h) * 2,
             "scale doubles the board height");
    for (float scale : {0.5f, 1.0f, 2.0f, 8.0f}) {
        const Native::Transform scaled{scale, 0.0f, 0.0f};
        Check(scaled.X(Native::kScreenWidth / 2)
                  == static_cast<float>(Native::kScreenWidth / 2),
              "scale keeps the screen centre fixed");
        Check(scaled.Y(Native::kCenterY) == static_cast<float>(Native::kCenterY),
              "scale keeps the board centre line fixed");
    }

    // At 100% every anchor is the ROM's own value.
    CheckInt(static_cast<int>(identity.X(Native::kValue1X)), Native::kValue1X,
             "identity transform preserves column 1");
    CheckInt(static_cast<int>(identity.X(Native::kPortraitX)), Native::kPortraitX,
             "identity transform preserves the portrait");
    CheckInt(static_cast<int>(identity.Y(Native::kCenterY)), Native::kCenterY,
             "identity transform preserves the board centre");
}

// -------------------------------------------------------------------------
//  Text cells: every anchor is the cell's centre.
//
//  Centre-aligned text lands on the cell centre, so this is what puts the
//  nickname on the ROM's X=92 rather than somewhere left of it.
// -------------------------------------------------------------------------
void TestTextCellsCentreOnTheirAnchor()
{
    const Native::Transform identity;
    struct Case { const char* what; int anchorX; int halfWidth; };
    const Case cases[] = {
        {"nickname cell", Native::kNicknameX, Native::kNameCellHalfW},
        {"team label cell", Native::kTeamNameX, Native::kTeamCellHalfW},
        {"column 1 cell", Native::kValue1X, Native::kValueCellHalfW},
        {"column 2 cell", Native::kValue2X, Native::kValueCellHalfW},
    };
    for (const Case& c : cases) {
        for (float lineHeight : {4.0f, 10.0f, 20.0f}) {
            const Native::Box cell = Native::TextCell(
                c.anchorX, c.halfWidth, 55, lineHeight, identity);
            CheckInt(static_cast<int>(cell.x + cell.w * 0.5f), c.anchorX, c.what);
            CheckInt(static_cast<int>(cell.y + cell.h * 0.5f), 55,
                     "the cell centres on its anchor Y");
            CheckInt(static_cast<int>(cell.h), static_cast<int>(lineHeight),
                     "cell keeps the requested line height");
        }
    }

    // A taller line grows both ways about the anchor, never off one edge.
    const Native::Box shortLine = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 55, 10.0f, identity);
    const Native::Box tallLine = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 55, 20.0f, identity);
    CheckInt(static_cast<int>(shortLine.y), 50, "a 10-tall line straddles the anchor");
    CheckInt(static_cast<int>(tallLine.y), 45, "a 20-tall line straddles the anchor");
    CheckInt(static_cast<int>(shortLine.y + shortLine.h * 0.5f),
             static_cast<int>(tallLine.y + tallLine.h * 0.5f),
             "line height does not move the centre");

    // Every cell on a player row shares the row's centre, whatever each one is
    // sized at -- that is what makes the row read as one line.
    const Native::Box value = Native::TextCell(
        Native::kValue1X, Native::kValueCellHalfW, 55, 11.0f, identity);
    const Native::Box stars = Native::CenteredImageCell(
        Native::kStarsCenterX, 55, 30.0f, 8.0f, identity, Native::kStarsMaxWidth);
    const Native::Box portrait = Native::CenteredImageCell(
        Native::kPortraitCenterX, 55, 20.0f, 20.0f, identity);
    CheckInt(static_cast<int>(value.y + value.h * 0.5f), 55, "values centre on the row");
    CheckInt(static_cast<int>(stars.y + stars.h * 0.5f), 55, "stars centre on the row");
    CheckInt(static_cast<int>(portrait.y + portrait.h * 0.5f), 55,
             "the portrait centres on the row");

    // The nickname keeps its own anchor nine above the row, so it still clears
    // the stars instead of landing on them.
    const Native::Box name = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 55 + Native::kNicknameDY,
        10.0f, identity);
    Check(name.y + name.h <= stars.y, "the nickname still sits above the stars");

    // Offsets and scale move the cell exactly as they move the board.
    const Native::Transform moved{1.0f, 10.0f, -6.0f};
    const Native::Box movedCell = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 55, 9.0f, moved);
    CheckInt(static_cast<int>(movedCell.x + movedCell.w * 0.5f),
             Native::kNicknameX + 10, "offset X moves the cell centre");
    const Native::Transform doubled{2.0f, 0.0f, 0.0f};
    const Native::Box doubledCell = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 55, 9.0f, doubled);
    CheckInt(static_cast<int>(doubledCell.w), Native::kNameCellHalfW * 4,
             "scale widens the cell");
    CheckInt(static_cast<int>(doubledCell.x + doubledCell.w * 0.5f),
             128 + (Native::kNicknameX - 128) * 2,
             "the scaled cell centre is the scaled anchor");
}

// -------------------------------------------------------------------------
//  Hunter portrait. Centred in the ROM's portrait cell on both axes, so a
//  portrait sized by its own setting still sits on its own row.
// -------------------------------------------------------------------------
void TestPortraitCellIsCentred()
{
    const Native::Transform identity;
    CheckInt(Native::kPortraitCenterX, 36, "portrait cell centre");
    Check(Native::kPortraitX + Native::kPortraitCellWidth <= Native::kStarsLeftX,
          "the portrait cell stops before the stars");

    for (float size : {12.0f, 20.0f, 26.0f, 32.0f, 48.0f}) {
        const Native::Box box = Native::CenteredImageCell(
            Native::kPortraitCenterX, 55, size, size, identity);
        CheckInt(static_cast<int>(box.x + box.w * 0.5f), Native::kPortraitCenterX,
                 "portrait stays centred horizontally at any size");
        CheckInt(static_cast<int>(box.y + box.h * 0.5f), 55,
                 "portrait stays centred on its row at any size");
        CheckInt(static_cast<int>(box.w), static_cast<int>(size),
                 "portrait keeps its own size");
    }

    // A ROM-sized portrait lands on the ROM's own corner, which is what makes
    // centring a safe reading of the anchor rather than a different position.
    const Native::Box romSized = Native::CenteredImageCell(
        Native::kPortraitCenterX, 55,
        static_cast<float>(Native::kPortraitCellWidth),
        static_cast<float>(-Native::kPortraitDY * 2), identity);
    CheckInt(static_cast<int>(romSized.x), Native::kPortraitX,
             "a cell-sized portrait starts at the ROM X");
    CheckInt(static_cast<int>(romSized.y), 55 + Native::kPortraitDY,
             "a cell-sized portrait starts at the ROM Y");

    // Offsets and scale carry it like everything else.
    const Native::Transform doubled{2.0f, 5.0f, 0.0f};
    const Native::Box scaled = Native::CenteredImageCell(
        Native::kPortraitCenterX, 55, 20.0f, 20.0f, doubled);
    CheckInt(static_cast<int>(scaled.x + scaled.w * 0.5f),
             128 + (Native::kPortraitCenterX - 128) * 2 + 5,
             "the scaled portrait centre is the scaled anchor");
}

// -------------------------------------------------------------------------
//  Stars. One combined asset stands in for the ROM's two pieces, so it centres
//  on their midpoint -- which is the nickname's centre, not the left piece's
//  corner.
// -------------------------------------------------------------------------
void TestStarsCellIsCentred()
{
    const Native::Transform identity;

    // A pair-width asset lands exactly where the ROM's two pieces do.
    const Native::Box full = Native::CenteredImageCell(
        Native::kStarsCenterX, 55, static_cast<float>(Native::kStarsMaxWidth),
        8.0f, identity, Native::kStarsMaxWidth);
    CheckInt(static_cast<int>(full.x), Native::kStarsLeftX,
             "a full-width stars asset starts at the ROM left piece");
    CheckInt(static_cast<int>(full.x + full.w),
             Native::kStarsLeftX + Native::kStarsMaxWidth,
             "a full-width stars asset ends where the pair ends");

    // A narrower asset stays centred rather than sticking to the left edge.
    for (float width : {8.0f, 24.0f, 40.0f, 64.0f}) {
        const Native::Box box = Native::CenteredImageCell(
            Native::kStarsCenterX, 55, width, 8.0f, identity, Native::kStarsMaxWidth);
        CheckInt(static_cast<int>(box.x + box.w * 0.5f), Native::kStarsCenterX,
                 "stars stay centred on the pair midpoint");
        CheckInt(static_cast<int>(box.y + box.h * 0.5f), 55,
                 "stars stay centred on their row");
        CheckInt(static_cast<int>(box.w), static_cast<int>(width),
                 "stars keep their own width");
    }

    // The stars share the nickname's centre, which is the whole point.
    const Native::Box stars = Native::CenteredImageCell(
        Native::kStarsCenterX, 55, 30.0f, 8.0f, identity, Native::kStarsMaxWidth);
    const Native::Box name = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 46, 9.0f, identity);
    CheckInt(static_cast<int>(stars.x + stars.w * 0.5f),
             static_cast<int>(name.x + name.w * 0.5f),
             "stars and nickname share a centre");

    // An oversized asset is capped at the pair width, still centred.
    const Native::Box huge = Native::CenteredImageCell(
        Native::kStarsCenterX, 55, 400.0f, 8.0f, identity, Native::kStarsMaxWidth);
    CheckInt(static_cast<int>(huge.w), Native::kStarsMaxWidth,
             "an oversized stars asset is capped at the pair width");
    CheckInt(static_cast<int>(huge.x + huge.w * 0.5f), Native::kStarsCenterX,
             "a capped stars asset is still centred");
    Check(huge.x + huge.w <= static_cast<float>(Native::kValue1X - Native::kValueCellHalfW),
          "a capped stars asset does not reach column 1");

    // Scale moves the centre and the cap together.
    const Native::Transform doubled{2.0f, 0.0f, 0.0f};
    const Native::Box scaled = Native::CenteredImageCell(
        Native::kStarsCenterX, 55, 400.0f, 8.0f, doubled, Native::kStarsMaxWidth);
    CheckInt(static_cast<int>(scaled.w), Native::kStarsMaxWidth * 2,
             "the cap scales with the board");
    CheckInt(static_cast<int>(scaled.x + scaled.w * 0.5f),
             128 + (Native::kStarsCenterX - 128) * 2,
             "the scaled stars centre is the scaled anchor");
}

// -------------------------------------------------------------------------
//  Anchor. Centre means the ROM's own placement; the other eight put the
//  board box against that screen point like any other HUD element.
// -------------------------------------------------------------------------
void TestAnchorPlacement()
{
    const Native::Box rom = Native::ComputePanelBox(4, 2, Native::Transform{1.0f, 0.0f, 0.0f});

    const Native::Transform romTf = Native::ResolveTransform(
        1.0f, Native::kRomAnchor, 0.0f, 0.0f, 128, 96, 4, 2);
    const Native::Box romBox = Native::ComputePanelBox(4, 2, romTf);
    CheckInt(static_cast<int>(romBox.x), static_cast<int>(rom.x),
             "Centre keeps the ROM X");
    CheckInt(static_cast<int>(romBox.y), static_cast<int>(rom.y),
             "Centre keeps the ROM Y");

    // Centre still takes the offsets directly.
    const Native::Transform romOffset = Native::ResolveTransform(
        1.0f, Native::kRomAnchor, 12.0f, -7.0f, 128, 96, 4, 2);
    const Native::Box romOffsetBox = Native::ComputePanelBox(4, 2, romOffset);
    CheckInt(static_cast<int>(romOffsetBox.x - rom.x), 12, "Centre honours offset X");
    CheckInt(static_cast<int>(romOffsetBox.y - rom.y), -7, "Centre honours offset Y");

    // Top-left: the anchor point becomes the box's top-left corner.
    const Native::Transform topLeft = Native::ResolveTransform(
        1.0f, 0, 0.0f, 0.0f, 0, 0, 4, 2);
    const Native::Box topLeftBox = Native::ComputePanelBox(4, 2, topLeft);
    CheckInt(static_cast<int>(topLeftBox.x), 0, "top-left anchor pins the left edge");
    CheckInt(static_cast<int>(topLeftBox.y), 0, "top-left anchor pins the top edge");

    // Bottom-right: the anchor point becomes the box's bottom-right corner.
    const Native::Transform bottomRight = Native::ResolveTransform(
        1.0f, 8, 0.0f, 0.0f, 256, 192, 4, 2);
    const Native::Box bottomRightBox = Native::ComputePanelBox(4, 2, bottomRight);
    CheckInt(static_cast<int>(bottomRightBox.x + bottomRightBox.w), 256,
             "bottom-right anchor pins the right edge");
    CheckInt(static_cast<int>(bottomRightBox.y + bottomRightBox.h), 192,
             "bottom-right anchor pins the bottom edge");

    // An anchored board keeps the ROM's internal geometry: only the board
    // moves, so a cell stays the same distance from the board's own left edge.
    const Native::Box romCell = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 55, 9.0f,
        Native::Transform{1.0f, 0.0f, 0.0f});
    const Native::Box movedCell = Native::TextCell(
        Native::kNicknameX, Native::kNameCellHalfW, 55, 9.0f, topLeft);
    CheckInt(static_cast<int>((movedCell.x - topLeftBox.x)
                              - (romCell.x - rom.x)), 0,
             "anchoring moves the board, not its internal geometry");

    // Out-of-range anchors are clamped, not wrapped into a different corner.
    const Native::Transform clamped = Native::ResolveTransform(
        1.0f, 99, 0.0f, 0.0f, 256, 192, 4, 2);
    const Native::Box clampedBox = Native::ComputePanelBox(4, 2, clamped);
    CheckInt(static_cast<int>(clampedBox.x + clampedBox.w), 256,
             "an out-of-range anchor clamps to bottom-right");
}

} // namespace

int main()
{
    TestFourPlayerFreeForAll();
    TestTwoPlayerFreeForAll();
    TestTwoTeamsEvenSplit();
    TestTwoTeamsUnevenSplit();
    TestEndingBoards();
    TestIntegerCentring();
    TestHeightIsIndependentOfInterleaving();
    TestPanelBoxAndTransform();
    TestTextCellsCentreOnTheirAnchor();
    TestPortraitCellIsCentred();
    TestStarsCellIsCentred();
    TestAnchorPlacement();

    if (g_failures != 0) {
        std::printf("hud-scoreboard-native-geometry-tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf(
        "hud-scoreboard-native-geometry-tests: FFA, team blocks, uneven teams, "
        "ending rows, integer centring, row-order independence, panel box, "
        "user transform, text cells, centred portrait and stars, anchor placement PASS\n");
    return 0;
}
