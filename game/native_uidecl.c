#include <common.h>

#ifdef CTR_NATIVE

// Defined in game/DebugMenu.c, which the unity build includes after this file.
int DebugMenu_IsOpen(void);

// Which player menu screen is the active menu? The game says it itself:
// sdata->ptrActiveMenu points at the box whose procedure draws the screen
// (MM_TrackSelect.c:453, MM_Characters.c:680/686). In addition a
// menu must be in the picture - otherwise the pointer is a leftover.
internal int NativeUiDecl_ActiveMenuIs(const struct RectMenu *menu)
{
	return CTR_UI_MenuMode() && (sdata->ptrActiveMenu == menu);
}

// The driver select for three players: icon grid on the left (26..288), windows on the right.
internal int NativeUiDecl_CharSelect3P(void)
{
	return NativeUiDecl_ActiveMenuIs(&D230.menuCharacterSelect) && (sdata->gGT->numPlyrNextGame == 3);
}

internal int NativeUiDecl_CupSelect(void)
{
	return NativeUiDecl_ActiveMenuIs(&D230.menuCupSelect);
}

internal int NativeUiDecl_BattleSetup(void)
{
	return NativeUiDecl_ActiveMenuIs(&D230.menuBattleWeapons);
}

// game/native_mods_page.c, later in the unity build
int NativeModsPage_IsOpen(void);

// Any track screen: retail (time trial, arcade, VS, battle) or the
// copy for CUSTOM - both draw through D230.menuTrackSelect. Not while the
// MODS page stands over it alone: the rows of the track screen would take
// the page's parts left of their rectangles' edge (x 307) to the left edge -
// the page is one panel ("menu-mods" below).
internal int NativeUiDecl_TrackSelect(void)
{
	return NativeUiDecl_ActiveMenuIs(&D230.menuTrackSelect) && !NativeModsPage_IsOpen();
}

// Is there a reason under a box of the chain in this frame? (native_menuscreen.c)
int NativeMenuReason_OnScreen(void);

// The track screen of ARCADE -> CUSTOM: the same box as retail, but with
// the copy of the proc (MM_NativeTrackSelect.c) on the pointer.
internal int NativeUiDecl_CustomTrackSelect(void)
{
	return NativeUiDecl_TrackSelect() && (D230.menuTrackSelect.funcPtr == MM_NativeTrackSelect_MenuProc);
}

// Is the arcade/adventure results screen in the picture? Two facts that the
// code sets itself: MainGameEnd_Initialize raises END_OF_RACE, and
// LOAD_TenStages picks for ARCADE_MODE as for ADVENTURE_MODE the
// end overlay 1, whose drawer is AA_EndEvent_DrawMenu (222.c). No shape,
// no threshold.
internal int NativeUiDecl_ArcadeResultsOnScreen(void)
{
	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		return 0;
	}

	return ((sdata->gGT->gameMode1 & END_OF_RACE) != 0) && (sdata->gGT->overlayIndex_EndOfRace == 1);
}

// WHERE A UI ELEMENT SITS, DECLARED RATHER THAN DERIVED.
//
// Without a table the anchor is worked out per frame from the element box:
// CTR_UI_AnchorForBox divides the canvas into thirds and reads the centre. That
// rule is right most of the time and it answers a question nobody wrote down.
// An element whose content grows - a fruit count, a lap number, a list that
// grows with the driver count - can push its centre across a boundary and
// change anchor with nothing having been decided, and the only way to find out
// is to look at the picture.
//
// A declaration says it once, in one place, for all three ratios: this element
// holds THAT edge, at that offset, at that size. The derived rule stays as the
// fallback for everything not yet declared, and the number of elements still
// falling back is logged - that number is how much of a screen is still
// guesswork.
//
// WHAT IS KEYED BY WHAT
//
// A drawn box in the 512x216 reference space, plus the UI ordering-table slot.
// There is nothing else to key on: primitives arrive as geometry. A row names a
// REGION the box must lie inside rather than an exact rectangle, so something
// whose width changes with its content keeps one declaration.
//
// A row is matched against a PRIMITIVE first and only then against an element,
// and that order is the whole point rather than an optimisation. An element is
// built from what touches what, so an element's box is a fact about this frame:
// a 3D model that turns changes its own width, so a pair that touches at one
// rotation and not at the next is one element and then two, and one element gets
// one anchor while two get two. A row keyed against the merged box then matches
// on some frames and not on others, and the picture moves although nothing about
// the design changed.
//
// So a primitive a row claims is taken out of the grouping and out of the floor:
// its position becomes a function of its own coordinates and its declared fixed
// point, and of nothing else in the frame. Rows that key on a merged box - an
// opaque overlay, which is only ever itself plus whatever it was laid over -
// cannot match a single primitive and fall through to the element lookup
// unchanged.
//
// The cost of that order is that a region is now much more likely to claim
// something it did not mean, because a single primitive is small and fits
// anywhere. That is why a row may take its region from the game's own layout
// table instead of writing coordinates down again - see hudSlot below - and why
// --ui-watch prints what every row actually claimed.
//
// THE EXCEPTION, IN THE SAME TABLE
//
// A row may name a ratio. A row that names the current one beats a row that
// names none; two rows of the same standing claiming one box is a defect in the
// table and is counted rather than broken quietly.
//
// This is the whole mechanism for "16:9 and 43:18 need different things". It is
// deliberately not a second table and not a second set of rows kept per ratio:
// one fact written in two places is how one of them falls behind. An exception
// stands next to the rule it excepts, and the diff that adds it shows both.
//
// No row names a ratio today. That is a statement, not an omission: nothing
// measured so far needs one, and an empty exception list that can be read at a
// glance is worth more than a full one that was guessed at.
//
// THE FLOOR
//
// Part of the declaration, not a pass afterwards. The mapping is a proportional
// squeeze - x' = x*s for a left-anchored element - so a margin of five pixels
// becomes four at 16:9 and three at 43:18. That is not an element fault; it is
// the mapping scaling away a distance the design chose. The floor says that
// distance survives:
//
//   mapped >= min(FLOOR, authored)
//
// The min() is not decoration. Elements authored partly outside the canvas have
// negative margins, and a bare floor would clamp them at 4:3 - where the factor
// is 1 and nothing may move. With the min() the condition reads
// "authored >= authored" at 4:3 for every element, so the floor PROVABLY cannot
// fire there, and 4:3 byte-equality cannot break on it.
//
// The two numbers below are carried over from an earlier version rather than
// re-derived here: they came out of a 4:3 census as
// the smallest margin and the smallest gap the stock data authors. They cannot
// be re-derived without driving, so they are made visible instead - every frame
// reports how many elements the floor moved and by how much. A wrong value shows
// up as clamps in the log, not as a quietly different picture.
#define NATIVE_UI_FLOOR_MARGIN 5
#define NATIVE_UI_FLOOR_GAP    5

// Margin and gap are solved together and the solver is bounded. Sixteen rounds
// is not a tuning knob - see NativeUiDecl_Floor for why it must be bounded and
// why the caller is told when it did not settle.
#define NATIVE_UI_DECL_MAX_ROUNDS 16

// How many rows the per-frame precondition state has room for. Asserted against
// the table below rather than trusted: a row added past this would silently
// never arm, which is a declaration that quietly does nothing.
#define NATIVE_UI_DECL_CAPACITY 32

// How an authored box is matched against a row rectangle.
//
//   INSIDE  the box lies within the rectangle. What a variable-width element
//           needs. Too generous for a fixed panel on its own: a rectangle over
//           the left half of the canvas would claim every small element sitting
//           in the left half of every other screen.
//   EXACT   the box equals the rectangle.
//   COVERS  the box contains the rectangle. What an opaque overlay needs: an
//           overlay swallows whatever it is laid over, because the grouping
//           merges every primitive it touches - so its element is the panel PLUS
//           the HUD behind it, and the box is bigger than the panel and varies
//           with what is on screen. Keyed on what the overlay guarantees, that
//           it spans at least its own rectangle, it matches regardless.
enum
{
	NATIVE_UI_DECL_INSIDE = 0,
	NATIVE_UI_DECL_EXACT = 1,
	NATIVE_UI_DECL_COVERS = 2,
};

struct NativeUiDecl
{
	const char *name;

	// The UI ordering-table slot, or -1 for any. The slot is the statement the
	// game itself makes about layering.
	int slot;

	// The ratio this row is for, or 0:0 for every ratio. See THE EXCEPTION.
	int aspectW;
	int aspectH;

	// WHERE THE REGION COMES FROM.
	//
	// -1: x0,y0,x1,y1 below are the region, written out here.
	//
	// >= 0: an index into enum UIHudSlot, and the region is the point the GAME
	// authored for that slot plus the reach in x0,y0,x1,y1. The coordinate is
	// then written down once, in data.hud_* in game/zGlobal_DATA.c, and this row
	// carries only how far around it to look. A row that copied the coordinate
	// would be the same number in two places, and one of them would fall behind
	// the other the first time a layout moved.
	//
	// It also makes the row follow the layout: the same slot sits at 336,16 in
	// the one-player HUD and at 454,10 in the two-player one, and a row keyed on
	// the slot is right in both without a second row.
	int hudSlot;

	// How many drivers the layout is for, or 0 for any. Only meaningful next to
	// a hudSlot, because the layouts are per driver count.
	int players;

	// Offset and size: either the region itself, or the reach around the
	// authored point, in the 512x216 reference space.
	int x0, y0, x1, y1;

	// Reference object: which fixed point this element holds on to.
	int anchor;

	int match;

	// WHAT MUST BE TRUE OF THE FRAME BEFORE THIS ROW CLAIMS ANYTHING IN IT.
	//
	// A region is a rectangle, and a rectangle on a 512x216 canvas will sooner or
	// later have something else inside it. Both rows below were measured claiming
	// the wrong thing on a boot screen, so both got a precondition, and each
	// precondition is a fact the code already guarantees rather than a shape.
	//
	// `when`: a function that answers "is the thing this row describes on screen
	// at all". NULL means always. The debug panel needs this because COVERS
	// matches ANY element spanning its rectangle, and a full-screen background
	// does - measured at vblank 140 with the panel shut.
	int (*when)(void);

	// `needsOrigin`: the frame must contain a primitive whose LEFT EDGE is exactly
	// the region's x0. A HUD readout drawn from a fixed left edge guarantees that
	// - UI_DrawNumWumpa puts the sign at posX and the number at posX+13, so the
	// sign is always at the authored x whatever the value. Stray primitives that
	// merely fall inside the rectangle do not start there, which is what was
	// measured: the counter is 336,16..364,31 and the intruders began at 345..350.
	int needsOrigin;
};

int g_cfg_uiDeclOff = 0;
int g_cfg_uiFloorOff = 0;

int g_uiDeclHits = 0;

// Counted apart from the element hits above, because they answer different
// questions. An element hit says a row claimed a whole merged box; a primitive
// hit says a row claimed something on its own and took it out of the grouping
// altogether. A row that stops matching per primitive and starts matching per
// element has changed what it does, and one number for both would hide that.
int g_uiDeclPrimHits = 0;

int g_uiDeclMisses = 0;
int g_uiDeclCollisions = 0;
int g_uiFloorClamps = 0;
int g_uiFloorMaxShift = 0;
int g_uiFloorUnsettled = 0;

global_variable const struct NativeUiDecl g_nativeUiDecls[] = {
    // THE DEBUG MENU PANEL. The left half of the canvas, and it must STAY the
    // left half at every ratio.
    //
    // The numbers are this tree's own: NATIVE_DEBUG_MENU_W is 256 and
    // NATIVE_DEBUG_MENU_H is 216 in game/DebugMenu.c, and the panel is drawn at
    // the origin. The derived rule reads its box centre at 128, calls it
    // left-anchored, and squeezes it by the factor - 256 px would become 143 at
    // 43:18, so "half the screen" would be 28 % of it. Worse, the rows inside it
    // are drawn at a fixed pixel pitch with a fixed font, so squeezing the panel
    // would leave the text hanging off its own edge.
    //
    // FULL_CANVAS is the anchor that means "not mapped". A layer laid over the
    // canvas is not a thing standing in the picture and has no edge to hold.
    //
    // COVERS, not EXACT: the panel is opaque and the grouping merges everything
    // it touches, so its element is the panel PLUS whatever HUD sits behind it.
    // What the panel guarantees is that it spans AT LEAST its own rectangle.
    {"debug-menu", -1, 0, 0, -1, 0, 0, 0, 256, 216, (int)CTR_UI_ANCHOR_FULL_CANVAS, NATIVE_UI_DECL_COVERS, DebugMenu_IsOpen, 0},

    // THE FRUIT COUNTER OF THE ONE-PLAYER RACE HUD, and it is the clearest case
    // the derived rule can produce: the element crosses a boundary nobody chose
    // when the tenth wumpa is collected.
    //
    // Every number below is read off this tree, not measured on a screen.
    // data.hud_1P_P1 in game/zGlobal_DATA.c authors the three parts of the group
    // at their slot indices (enum UIHudSlot):
    //
    //   UI_HUD_SLOT_WEAPON       0   232,  5
    //   UI_HUD_SLOT_FRUIT_MODEL  3   316, 24
    //   UI_HUD_SLOT_WUMPA_COUNT  4   336, 16
    //
    // UI_DrawNumWumpa draws the sign at posX in FONT_SMALL and the number at
    // posX + UI_DRAWNUM_BIG_TEXT_OFFSET_X (13) in FONT_BIG, and
    // data.font_charPixWidth is 13 small and 17 big. So the drawn box starts at
    // 336 and ends near 336+13+17 = 366 at one digit and 336+13+34 = 383 at two.
    // The reach below is 0..48 in x, which holds both, and -2..20 in y, which
    // holds the sign at posY+4 in an 8-pixel font and the number at posY in a
    // 17-pixel one.
    //
    // The row is keyed on the SLOT, not on 336. The coordinate belongs to
    // data.hud_* and is written there; a copy of it here would be the same fact
    // in two places. Keying on the slot also makes the row right in the
    // two-player layout, where the same slot is authored at 454,10 - one row
    // instead of two that can drift apart.
    //
    // WHY IT NEEDS DECLARING AT ALL. The thirds boundary is 512*2/3 = 341.33.
    // The drawn box centre is about 351 at one digit and 359 at two - both past
    // it, so the derived rule answers RIGHT. The fruit model beside it sits
    // short of the boundary and answers CENTRE. Two parts of one reading, one
    // holding the canvas centre and the other the right edge: at 4:3 the factor
    // is 1 and nothing moves, at every wider ratio they pull apart by the whole
    // distance between two different fixed points.
    //
    // AND WHY IT HAD TO BE CLAIMED PER PRIMITIVE. The fruit model beside it is,
    // in the one-player HUD, a 3D mesh projected straight into this ordering
    // table - UI_RenderFrame.c draws a fixed-size copy quad only from two
    // players up. A mesh turns, so its drawn width changes every frame, so the
    // distance to this counter changes, so the two are one element at one
    // rotation and two at the next. Keyed against a merged box this row simply
    // stopped matching on some frames. That is the flicker seen at 43:18.
    //
    // THE ANCHOR IS DECLARED AND IT IS THE CENTRE. The fruit and its count are
    // one reading; the reading belongs beside the weapon box; the weapon box is
    // centred. Nothing about this element wants the right edge - it only ever
    // got it because its authored x happens to be 336.
    //
    // WHICH WAY THE COUNT GROWS is decided by the same function: the sign sits
    // at posX and does not move with the value, so the number grows RIGHTWARD
    // from a fixed left edge - and the left edge is the one facing the fruit,
    // which is what holds the gap at every count.
    //
    // INSIDE, not EXACT: an EXACT key would hold for one digit count and
    // silently drop the other back to the derived rule, which is the fault that
    // looks like the counter jumping when the tenth fruit is collected. INSIDE
    // is also what lets each glyph be claimed on its own.
    //
    // At 4:3 the factor is 1 and CENTRE is the identity, so this row provably
    // cannot change the 4:3 frame.
    {"race-hud-wumpa-count", -1, 0, 0, UI_HUD_SLOT_WUMPA_COUNT, 1, 0, -2, 48, 20, (int)CTR_UI_ANCHOR_CENTRE,
     NATIVE_UI_DECL_INSIDE, NULL, 1},

    // THE DRIVER ROW OF THE ARCADE RESULTS SCREEN, and it is the case for
    // which "declare per row" was decided: a row of
    // up to eight icons that the producer means as ONE row and that the
    // derived anchor tears into three parts.
    //
    // 222.c centers the row in the reference width: width N*44 + (N-1)*12,
    // start 256 - width/2, step AA_DRIVER_ICON_SPACING = 56, icon 44 wide,
    // y = 0x60. Measured at 43:18 (--ui-watch-from 8990, race end forced,
    // VBlank 9223), eight icons:
    //
    //    38.. 81  left       94..137  left
    //   150..193  centre    206..249  centre    262..305  centre   318..361  centre
    //   374..417  right     430..473  right
    //
    // The gap of 12 is larger than the touch distance 4, so
    // every icon is an element of its own with its own third: shifts 0,
    // +203 and +406 in ONE row. At 4:3 all three are 0.
    //
    // The anchor is the centre, because the producer centers the row. The
    // rectangle is the path of the row, not its resting place: the icons fly
    // in from 0x218 = 536 (UI_Lerp2D_Linear in 222.c, right edge 580) and
    // out to -100. A rectangle only around the resting place would have handed every icon
    // back to the derived anchor on its way - a jump of 203
    // columns in mid-flight. In y the icons lie at 95..121 and the
    // rank digit next to them at 0x5f = 95 in the small font.
    //
    // INSIDE, per primitive: every icon and every digit is claimed on its
    // own and shifted with the same centre, so the distance
    // of 12 is kept. What else lies in this band is "PRESS TO CONTINUE"
    // at 256,100 in JUSTIFY_CENTER (222.c:498) - centred is the right anchor
    // for the text anyway. Precondition: the results screen is in the
    // picture, otherwise the rectangle would lie across the standings of the race HUD.
    {"arcade-results-driver-row", 0, 0, 0, -1, 0, -100, 93, 580, 123, (int)CTR_UI_ANCHOR_CENTRE, NATIVE_UI_DECL_INSIDE,
     NativeUiDecl_ArcadeResultsOnScreen, 0},

    // THE TITLE OF THE DRIVER SELECT FOR THREE PLAYERS BELONGS TO ITS GRID.
    //
    // With three players the icon grid stands on the left (26..288, edge distance 26)
    // and SELECT CHARACTER in two lines above it, centred on the grid
    // (MM_Characters.c:708-716, middle 0x9c = 156; measured 80..231 x 20..53).
    // Taken on its own the title would be centred by the edge rule (80 points from
    // the edge) and would wander 203 columns away from the grid at 43:18. As part of the
    // block title + grid (26..288) it holds the left edge together with it.
    //
    // The rectangle is the path of the title, not only its resting place: it flies
    // out to the left by 512 with the grid (D230.c:490-492). To the right it reaches
    // only to 300, so never the windows (from 293, but 160 wide), and
    // down only to 56, so never the grid (from 68).
    {"menu-charselect-3p-title", 0, 0, 0, -1, 0, -540, 18, 300, 56, (int)CTR_UI_ANCHOR_LEFT, NATIVE_UI_DECL_INSIDE,
     NativeUiDecl_CharSelect3P, 0},

    // THE MAP OF THE CUSTOM TRACK SCREEN BELONGS TO THE PREVIEW WINDOW.
    //
    // Retail sets the map centre fixed at y 162 (MM_TrackSelect.c:917-920).
    // A retail map is about 96 high and so begins directly below the
    // window (bottom edge 114). The menu grouping joins both into one
    // column (at most 8 points of air, native_gpu.c:3087-3091), and
    // the block holds the right edge. The map of Baby T Park is 62 high,
    // begins at 131 - 17 points of air, so an element of its own. By the
    // edge rule it is centred, and at 43:18 the map stood 203 columns left
    // of the window (measured).
    //
    // Right-anchored like the block in retail. The rectangle is the path of the
    // map, not only its resting place: it flies in with the lap choice from +512
    // (trackSelect_lapMenuTransition, D230.c:588). At the top it begins
    // below the window including shadow (to 120), so that window and title never
    // fall into it. Precondition: the copy is the active track screen -
    // the retail screen stays as it is. At 4:3 every anchor is the
    // identity, there this row changes nothing.
    {"menu-custom-trackselect-map", -1, 0, 0, -1, 0, 300, 121, 1000, 216, (int)CTR_UI_ANCHOR_RIGHT, NATIVE_UI_DECL_INSIDE,
     NativeUiDecl_CustomTrackSelect, 0},

    // THE TRACK LIST HOLDS THE LEFT EDGE OF THE ZONE, WITHOUT TRANSIT.
    //
    // The carousel (MM_TrackSelect.c:704-839, likewise the copy for CUSTOM)
    // draws nine rows on an arc: x = -180 + 200*cos, width 256, so
    // -28..288 at rest including shadow (measured). The top and the bottom
    // row stick out 28 points to the left of the picture - authored that way, 4:3 and 16:9
    // cut them off at the screen edge.
    //
    // By the edge rule the list is left, and since the composition zone
    // (native_view.c, CTR_UI_MapX) left holds the edge of the zone. The edge rule
    // would in addition get the transit over the 4:3 edge (CTR_UI_MenuEdgeTransit),
    // because the list lies partly outside - and at rest it would then stand 10
    // columns left of its 16:9 position (108 instead of 118 at 918). This row takes
    // it out of the edge rule: LEFT, without transit, exactly as in 16:9. In exchange,
    // at 918 columns the 28 points of row ends lie visibly left of the zone
    // (90..118). Whether they disturb is left to a check of the screenshot.
    //
    // The rectangle is the path of the list, not its resting place: it flies in by 512
    // from the left (transitionMeta_trackSel, D230.c:588), so from -540. When
    // scrolling the arc turns one step further, y then reaches about -58..280.
    // On the right it ends before the right column (MM_TRACK_SELECT_PREVIEW_X = 308);
    // stars and ghost hint in time trial hang on the row and lie
    // inside it. Title and preview fly vertically, lap box and map from the
    // right - none of them comes into the rectangle.
    //
    // At 4:3 and 16:9 LEFT is the identity or +0, and the list was already
    // left there (edge rule): this row changes no bit in either.
    {"menu-trackselect-list", -1, 0, 0, -1, 0, -560, -70, 307, 290, (int)CTR_UI_ANCHOR_LEFT, NATIVE_UI_DECL_INSIDE,
     NativeUiDecl_TrackSelect, 0},

    // THE REASON UNDER A BOX OF THE CHAIN HOLDS THE RIGHT EDGE LIKE THE
    // BOX. One line of FONT_SMALL, centred under the open
    // box, when the cursor is on a grey row
    // (NativeMenuReason_NoteDrawn, native_menuscreen.c) - under the
    // NITRO-PIT box at 207..214, under the OPTIONS box higher. The
    // chain stands on the right (frame 293..475 at 4:3), but the edge rule would give the
    // narrower text line CENTRE, and it would run 85
    // columns away from the box in widescreen. What of the box itself falls into the rectangle
    // (shadow) holds RIGHT anyway. Precondition: in this frame a
    // reason is drawn.
    {"menu-reason-line", -1, 0, 0, -1, 0, 150, 185, 1000, 216, (int)CTR_UI_ANCHOR_RIGHT, NATIVE_UI_DECL_INSIDE,
     NativeMenuReason_OnScreen, 0},

    {NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, 0},
};

// SCREENS THAT ARE ONE ELEMENT AS A WHOLE.
//
// Two menu screens are ONE composed panel, and the edge rule,
// applied to their parts, tears them apart (measured at
// 43:18):
//
//   Battle settings: right-aligned labels 10..144 and panels
//   150..482, six points apart. Parts: left and right, 406 columns
//   apart. As a whole (10..482) the screen lies at both edges and
//   so does not fit the rule - it stays centred.
//
//   Cup select: two columns with two cup boxes each, 72..264 and 272..464 with
//   shadow, the title centred above. The right column stands with its
//   shadow 48 points from the edge and would get right, the left one centre - 203
//   columns apart. The grid is composed around 256 (boxes 72..252 and
//   272..452); only the shadows reach into the 51-point strip on the right. Centred,
//   and reported as an open decision: the edge rule taken literally
//   would give RIGHT for the whole grid.
//
// Every element of this screen gets the one anchor, and because the parts
// fly in and out on a change - the cup boxes by 256 points diagonally
// (D230.c:617), the battle rows by 400 to the right and 200 to the left
// (D230.c:636-646) - in addition the shift for the flight over the edge
// (CTR_UI_TransitShift): what is outside in 4:3 stays outside on the
// canvas.
struct NativeUiMenuPolicy
{
	const char *name;
	int (*when)(void);
	int anchor;
};

// game/native_graphics.c, later in the unity build
int NativeGraphics_Active(void);

global_variable const struct NativeUiMenuPolicy g_nativeUiMenuPolicies[] = {
    {"menu-battle-setup", NativeUiDecl_BattleSetup, (int)CTR_UI_ANCHOR_CENTRE},
    // Beta 0: the GRAPHICS page, 56..456 like the options screen of the pause
    {"menu-graphics", NativeGraphics_Active, (int)CTR_UI_ANCHOR_CENTRE},
    // The MODS page, the same panel 56..456, over the track select or the cup
    // select. Without this row the edge rule takes its parts one by one; over
    // the track select the rows of that screen are off while it is open
    // (NativeUiDecl_TrackSelect), else they took everything left of x 307 to
    // the left edge: labels outside the frame, a torn title, one edge of the
    // frame alone on the left.
    {"menu-mods", NativeModsPage_IsOpen, (int)CTR_UI_ANCHOR_CENTRE},
    {"menu-cup-select", NativeUiDecl_CupSelect, (int)CTR_UI_ANCHOR_CENTRE},
    {NULL, NULL, 0},
};

// The anchor that the active screen prescribes to all its elements, or -1.
int NativeUiDecl_MenuPolicy(const char **outName)
{
	int i;

	for (i = 0; g_nativeUiMenuPolicies[i].name != NULL; i++)
	{
		if (g_nativeUiMenuPolicies[i].when())
		{
			if (outName != NULL)
			{
				*outName = g_nativeUiMenuPolicies[i].name;
			}

			return g_nativeUiMenuPolicies[i].anchor;
		}
	}

	return -1;
}

// No count is stored beside the table. The terminator is the end, and a function
// that walks to it cannot disagree with it - a length written down separately is
// the same fact in two places.
int NativeUiDecl_Count(void)
{
	int count = 0;

	while (g_nativeUiDecls[count].name != NULL)
	{
		count++;
	}

	return count;
}

const struct NativeUiDecl *NativeUiDecl_At(int index)
{
	if ((index < 0) || (index >= NativeUiDecl_Count()))
	{
		return NULL;
	}

	return &g_nativeUiDecls[index];
}

// The region this row is about right now, or 0 when the row does not apply.
//
// A row keyed on a HUD slot resolves against the layout the game is currently
// drawing, so the coordinate lives in data.hud_* and nowhere else. A row keyed on
// nothing carries its own rectangle, which is right for things the 1999 layout
// table knows nothing about - our debug panel, for one.
int NativeUiDecl_Region(const struct NativeUiDecl *d, int *out)
{
	const struct UiElement2D *hud;
	int players;

	if ((d == NULL) || (out == NULL))
	{
		return 0;
	}

	if (d->hudSlot < 0)
	{
		out[0] = d->x0;
		out[1] = d->y0;
		out[2] = d->x1;
		out[3] = d->y1;
		return 1;
	}

	if ((sdata == NULL) || (sdata->gGT == NULL) || (d->hudSlot >= UI_HUD_SLOT_COUNT))
	{
		return 0;
	}

	players = (int)sdata->gGT->numPlyrCurrGame;

	if ((players < 1) || (players > 4))
	{
		return 0;
	}

	if ((d->players > 0) && (players != d->players))
	{
		return 0;
	}

	hud = data.hudStructPtr[players - 1];

	if (hud == NULL)
	{
		return 0;
	}

	out[0] = hud[d->hudSlot].x + d->x0;
	out[1] = hud[d->hudSlot].y + d->y0;
	out[2] = hud[d->hudSlot].x + d->x1;
	out[3] = hud[d->hudSlot].y + d->y1;
	return 1;
}

internal int NativeUiDecl_Matches(const struct NativeUiDecl *d, const int *box)
{
	int region[4];

	if (!NativeUiDecl_Region(d, region))
	{
		return 0;
	}

	if (d->match == NATIVE_UI_DECL_EXACT)
	{
		return (box[0] == region[0]) && (box[1] == region[1]) && (box[2] == region[2]) && (box[3] == region[3]);
	}

	if (d->match == NATIVE_UI_DECL_COVERS)
	{
		return (box[0] <= region[0]) && (box[1] <= region[1]) && (box[2] >= region[2]) && (box[3] >= region[3]);
	}

	return (box[0] >= region[0]) && (box[1] >= region[1]) && (box[2] <= region[2]) && (box[3] <= region[3]);
}

// Does the frame satisfy this row's preconditions?
//
// Worked out once per frame and per row rather than per primitive, because it is
// a statement about the frame. NativeUiDecl_BeginFrame is what recomputes it;
// nothing else may, or the answer would depend on which primitive asked first.
global_variable u8 s_declArmed[NATIVE_UI_DECL_CAPACITY];

// Called once, after every primitive of the frame is known and before any of
// them is matched. originsFound[i] says whether some primitive's left edge sat
// exactly on row i's region origin.
void NativeUiDecl_BeginFrame(const u8 *originsFound)
{
	const int count = NativeUiDecl_Count();
	int i;

	local_persist int s_reportedOverflow = 0;

	for (i = 0; i < NATIVE_UI_DECL_CAPACITY; i++)
	{
		s_declArmed[i] = 0;
	}

	// A row past the capacity would never arm, which is a declaration that
	// quietly does nothing - said once rather than left to be noticed on screen.
	if ((count > NATIVE_UI_DECL_CAPACITY) && !s_reportedOverflow)
	{
		s_reportedOverflow = 1;
		Platform_Log("[CTR UIElem] the table has %d rows and only %d can be armed - rows past that never fire\n", count,
		             NATIVE_UI_DECL_CAPACITY);
	}

	for (i = 0; (i < count) && (i < NATIVE_UI_DECL_CAPACITY); i++)
	{
		const struct NativeUiDecl *d = &g_nativeUiDecls[i];

		if ((d->when != NULL) && !d->when())
		{
			continue;
		}

		if (d->needsOrigin && ((originsFound == NULL) || !originsFound[i]))
		{
			continue;
		}

		s_declArmed[i] = 1;
	}
}

// -1 when nothing declares this box.
int NativeUiDecl_Find(const int *authoredBox, int slot)
{
	int aspectW = 4;
	int aspectH = 3;
	int found = -1;
	int foundNamesRatio = 0;
	const int count = NativeUiDecl_Count();
	int i;

	if ((authoredBox == NULL) || g_cfg_uiDeclOff)
	{
		return -1;
	}

	CTR_View_GetWorldAspect(&aspectW, &aspectH);

	for (i = 0; i < count; i++)
	{
		const struct NativeUiDecl *d = &g_nativeUiDecls[i];
		int namesRatio;

		if ((i >= NATIVE_UI_DECL_CAPACITY) || !s_declArmed[i])
		{
			continue;
		}

		if ((d->slot >= 0) && (d->slot != slot))
		{
			continue;
		}

		// A row for a ratio other than this one is not a candidate at all. A row
		// for no particular ratio always is.
		namesRatio = (d->aspectW > 0) && (d->aspectH > 0);
		if (namesRatio && ((d->aspectW != aspectW) || (d->aspectH != aspectH)))
		{
			continue;
		}

		if (!NativeUiDecl_Matches(d, authoredBox))
		{
			continue;
		}

		if (found < 0)
		{
			found = i;
			foundNamesRatio = namesRatio;
			continue;
		}

		// The exception wins over the rule it excepts, and that is the ONLY
		// asymmetry allowed here.
		if (namesRatio != foundNamesRatio)
		{
			if (namesRatio)
			{
				found = i;
				foundNamesRatio = 1;
			}
			continue;
		}

		// Two rows of equal standing claiming one box is a mistake in the table,
		// not a tie to be broken quietly. The first still wins so the frame stays
		// deterministic, and the count says the table needs fixing rather than
		// the picture being wrong once.
		g_uiDeclCollisions++;
	}

	return found;
}

// Do two boxes share a row band at all? Two that do not cannot be read as a
// distance by a viewer either, so they carry no gap.
internal int NativeUiDecl_Bands(const int *a, const int *b)
{
	const int lo = (a[1] > b[1]) ? a[1] : b[1];
	const int hi = (a[3] < b[3]) ? a[3] : b[3];

	return (hi - lo) >= 1;
}

// Fill shift[] so that no margin and no gap ends below min(FLOOR, its authored
// value). Returns 1 when the constraints settled, 0 when they did not - a caller
// that ignored the difference would ship a frame mid-oscillation.
// TWO WIDTHS, since the mapper shifts. The authored canvas says how
// large an edge distance MAY be - an element that stands 2 points from the
// edge in the original is not granted 5. The drawn canvas says where the
// right edge LIES, because mapped[] is in its coordinates. At 4:3
// both are the same number, and there this function computes word for word as before.
int NativeUiDecl_Floor(const int (*authored)[4], const int (*mapped)[4], const int *anchor, int count, int virtualWidth,
                       int canvasWidth, int *shift)
{
	int round;
	int i;
	int j;

	for (i = 0; i < count; i++)
	{
		shift[i] = 0;
	}

	if (g_cfg_uiFloorOff || (count <= 0))
	{
		return 1;
	}

	// Margin and gap are solved TOGETHER, not one after the other. Opening a gap
	// moves an element, which can break the margin that was just restored, which
	// moves it back. Bounded, and the caller is told when it did not settle -
	// that state is a fact about the layout, not something to iterate away.
	for (round = 0; round < NATIVE_UI_DECL_MAX_ROUNDS; round++)
	{
		int changed = 0;

		for (i = 0; i < count; i++)
		{
			int x0;
			int x1;
			int wantL;
			int wantR;
			int needL;
			int needR;

			if (anchor[i] == (int)CTR_UI_ANCHOR_FULL_CANVAS)
			{
				continue;
			}

			x0 = mapped[i][0] + shift[i];
			x1 = mapped[i][2] + shift[i];

			wantL = authored[i][0];
			if (wantL > NATIVE_UI_FLOOR_MARGIN) { wantL = NATIVE_UI_FLOOR_MARGIN; }

			wantR = virtualWidth - 1 - authored[i][2];
			if (wantR > NATIVE_UI_FLOOR_MARGIN) { wantR = NATIVE_UI_FLOOR_MARGIN; }

			needL = wantL - x0;
			needR = wantR - (canvasWidth - 1 - x1);

			if (needL > 0)
			{
				shift[i] += needL;
				changed = 1;
			}
			else if (needR > 0)
			{
				shift[i] -= needR;
				changed = 1;
			}
		}

		for (i = 0; i < count; i++)
		{
			if (anchor[i] == (int)CTR_UI_ANCHOR_FULL_CANVAS)
			{
				continue;
			}

			for (j = i + 1; j < count; j++)
			{
				int A[4];
				int B[4];
				int was;
				int want;
				int gap;
				int rightIndex;

				if (anchor[j] == (int)CTR_UI_ANCHOR_FULL_CANVAS)
				{
					continue;
				}

				A[0] = mapped[i][0] + shift[i]; A[1] = mapped[i][1];
				A[2] = mapped[i][2] + shift[i]; A[3] = mapped[i][3];
				B[0] = mapped[j][0] + shift[j]; B[1] = mapped[j][1];
				B[2] = mapped[j][2] + shift[j]; B[3] = mapped[j][3];

				if (!NativeUiDecl_Bands(A, B))
				{
					continue;
				}

				// The authored distance decides what has to be preserved, and it
				// is read from the authored boxes rather than the mapped ones -
				// the mapped pair may already have been moved this round.
				if (authored[i][0] <= authored[j][0])
				{
					was = authored[j][0] - authored[i][2] - 1;
				}
				else
				{
					was = authored[i][0] - authored[j][2] - 1;
				}

				// Two elements that already overlapped when they were authored
				// carry no distance to preserve. Layering is not a gap.
				if (was < 0)
				{
					continue;
				}

				want = (was < NATIVE_UI_FLOOR_GAP) ? was : NATIVE_UI_FLOOR_GAP;

				if (A[0] <= B[0])
				{
					gap = B[0] - A[2] - 1;
					rightIndex = j;
				}
				else
				{
					gap = A[0] - B[2] - 1;
					rightIndex = i;
				}

				if (gap < want)
				{
					shift[rightIndex] += (want - gap);
					changed = 1;
				}
			}
		}

		if (!changed)
		{
			return 1;
		}
	}

	g_uiFloorUnsettled++;
	return 0;
}

#endif
