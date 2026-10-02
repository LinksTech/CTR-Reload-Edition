#include <common.h>

#include <stdarg.h>
#include <stdio.h>

#include <platform/native_chars.h>

// THE DRIVER SELECT GRID: THE ROSTER AFTER THE 15 RETAIL TILES.
//
// The one-player driver select (layout 0, D230.characterSelectMeta1P2P) shows
// its 15 tiles in three rows. The custom characters of the roster
// (platform/native_chars.c) get tiles of their own behind them: tile 15 + e is
// roster entry e. The first up to three stand in row 2, directly right of Fake
// Crash; every further six make a row of their own. Three rows are visible, the
// grid scrolls with the cursor, and two arrows beside the rows say that there
// is more above or below.
//
// NO COPY OF THE MENU. MM_Characters.c keeps its proc and asks this file at
// every place where it reads a tile: position, portrait, transition, name, the
// next tile for a d-pad press, the preview model. While the grid is off
// (MM_NativeCharGrid_Active() == 0) every function here answers with the
// retail expression of its call site and changes nothing - no state, no log
// line, no primitive. Without a roster entry the menu is the retail one, byte
// for byte.
//
// THE ENGINE ONLY SEES TEMPLATES. data.characterIDs[0] holds the template of a
// custom tile (0..14); which entry is meant stands as the pick in
// native_chars.c, written every frame (MM_NativeCharGrid_WritePick). A tile
// >= 15 never indexes D230.activeCharacterSelectMeta, D230.characterMenuID or
// D230.characterSelectTransitionMeta: its position comes from the grid, its
// transition from s_gridTransition, its name from the roster.
//
// THE GEOMETRY IS READ FROM THE TABLE, not written down a second time: the
// tiles of D230.characterSelectMeta1P2P grouped into rows by posY, each row
// sorted by posX (MM_NativeCharGrid_Derive). That gives the column pitch
// (0x40), the row pitch (0x27), the centre of a row (0xE0, the left edge of
// the middle position) and six tiles to a full row. A row of n tiles stands
// centred: x_i = centre + (2i - (n - 1)) * pitch / 2. For the full rows 0 and 1,
// and for row 2 without custom tiles, that is exactly the table; with custom
// tiles row 2 is centred anew as a whole. --char-grid-selftest checks the
// numbers.
//
// NAVIGATION: RETAIL FIRST, THEN THE GRID. A retail tile moves as retail
// moves it, a custom tile moves in the grid, and at a dead end both take the
// retail detour (MM_Characters.c:848-923). Two neighbour functions:
//   the table - D230.characterSelectMeta1P2P[tile].nextIconByDirection;
//   the grid  - left and right to the neighbour in the row, up and down one
//               row to the tile with the nearest x (on a tie down takes the
//               right one, up the left one); no wrap-around, off the grid the
//               tile stays (MM_NativeCharGrid_GridNext).
// The detour is retail's for one player (the grid is one-player only, so no
// tile is ever taken by another): the neighbour; at a dead end fallback
// direction 1 after the press, the press after fallback direction 1, then the
// same with fallback direction 2 (D230.characterSelectFallbackDirection1/2);
// else the tile stays (MM_NativeCharGrid_Detour). A press on
//   a retail tile: (1) left or right onto a custom tile of the grid - only
//                  Fake Crash right to entry 0; (2) else the retail press,
//                  the table with its detour (MM_NativeCharGrid_RetailStep),
//                  wherever it moves; (3) else, at a retail dead end, the grid
//                  - only down from row 2 into the custom row 3;
//   a custom tile: (4) the grid with the detour, the grid as its neighbour
//                  function.
// So retail keeps all its 60 presses (Joe left -> Roo, Roo down -> Joe,
// Papu down -> Fake Crash) except Fake Crash right (entry 0 instead of Pura)
// and, from four entries on, Joe, Penta and Fake Crash down (row 3 instead of
// staying). A press changes the row by one at most - the scroll keeps the tile
// before the press in view - and every tile can be reached. A locked retail
// tile is never a neighbour, as MM_Characters_GetNextDriver keeps it.
//
// STATE lives in global_variable: OVR230_ResetRuntimeState copies all of D230
// back on every return from a race (D230.c:855-875), and the scroll, the
// preview tiles and the custom transitions must not go with it.

// Room for the rows of the table: three in the retail layout, six tiles in the
// widest. The derivation refuses a table that does not fit.
#define MM_NATIVE_GRID_RETAIL_ROWS_MAX 4
#define MM_NATIVE_GRID_ROW_TILES_MAX 8

// All tiles at most: the retail icons and a full roster.
#define MM_NATIVE_GRID_TILES_MAX (MM_CHARACTER_SELECT_ICON_COUNT + NATIVE_CHAR_ROSTER_MAX)

// How a custom tile flies in: like every retail tile of the one-player layout
// (D230.c:483-485), straight down by 0xC8.
#define MM_NATIVE_GRID_TRANSITION_DIST_X 0
#define MM_NATIVE_GRID_TRANSITION_DIST_Y 0xC8

// The band in which a menu element keeps the centre anchor in a wide picture
// (CTR_UI_AnchorForBoxEdge, native_view.c:2208-2233): more than a tenth of
// the 512 columns from both edges, so x0 >= 52 and x1 <= 460.
#define MM_NATIVE_GRID_BAND_LEFT ((MM_CHARACTER_SELECT_SCREEN_W / 10) + 1)
#define MM_NATIVE_GRID_BAND_RIGHT (MM_CHARACTER_SELECT_SCREEN_W - MM_NATIVE_GRID_BAND_LEFT)

// THE ARROWS: the page arrow of the high score pages and the pause menu
// (MM_HighScore.c:94-108), the same icon, colours and order table.
//
// DecalHUD_Arrow2D (DecalHUD.c:137ff) takes posX/posY as the CENTRE: the
// corners lie at -h..h+1 around it, h = (size * scale) >> 13, so at scale
// FP(1.0) the arrow has the size of its icon. The icon points right at
// rotation 0 (0x800 points left, MM_HighScore.c); 0x400 turns it to point up,
// 0xC00 down. Turned by a quarter, the HEIGHT of the icon becomes the width on
// the screen: x runs over posX - h .. posX + h + 1 (up) or posX - h - 1 ..
// posX + h (down).
//
// The size of icon 0x38 is in the disc data, not in the code. So the scale is
// taken from the icon itself (MM_NativeCharGrid_ArrowScale): FP(1.0) while the
// half height is at most 7, else smaller until it is. With the centre at
// 0x1C0 that keeps the box in x 440..456, at most 16 columns wide: right of the
// last tile column (it ends at 436) and inside the centred band.
//
// The shift 13 is DECAL_HUD_ARROW_SCALE_SHIFT, read and not copied: it lives in
// the enumeration of DecalHUD.c (DecalHUD.c:7), which stands long before this
// file in the unity build (game_unity.h), like MM_CHARACTER_SELECT_* from
// MM_Characters.c directly before it.
#define MM_NATIVE_GRID_ARROW_ICON_GROUP 4
#define MM_NATIVE_GRID_ARROW_ICON 0x38
#define MM_NATIVE_GRID_ARROW_FLASH_BIT 4
#define MM_NATIVE_GRID_ARROW_X 0x1C0
#define MM_NATIVE_GRID_ARROW_Y_FROM_ROW 16
#define MM_NATIVE_GRID_ARROW_HALF_MAX 7
#define MM_NATIVE_GRID_ARROW_ROT_UP 0x400
#define MM_NATIVE_GRID_ARROW_ROT_DOWN 0xC00
#define MM_NATIVE_GRID_ARROW_BOX_LEFT 437
#define MM_NATIVE_GRID_ARROW_BOX_RIGHT 456

#define MM_NATIVE_GRID_ARROW_UP 1
#define MM_NATIVE_GRID_ARROW_DOWN 2

// The self-test prints at most this many failures; the count says the rest.
#define MM_NATIVE_GRID_TEST_REPORT_MAX 20
#define MM_NATIVE_GRID_TEST_WALK_STEPS 1000

// The dead ends of the retail table that stay with one player
// (MM_NativeCharGrid_TestRetailStep).
#define MM_NATIVE_GRID_TEST_RETAIL_STAYS 13

// The layout the table gives. Rows are numbered from the top, columns from the
// left, both from 0.
struct MM_NativeGridGeometry
{
	int rowCount; // retail rows; also the number of visible rows
	int rowY[MM_NATIVE_GRID_RETAIL_ROWS_MAX];
	int rowSize[MM_NATIVE_GRID_RETAIL_ROWS_MAX];
	s16 rowTile[MM_NATIVE_GRID_RETAIL_ROWS_MAX][MM_NATIVE_GRID_ROW_TILES_MAX];

	int rowSizeMax; // tiles of the widest row
	int firstY;     // y of row 0
	int rowPitch;
	int colPitch;
	int centreX;    // left edge of the middle position
	int leftX;      // left edge of the first position of a full row
};

// ===========================================================================
//  THE LAYOUT, as pure functions of the geometry and the number of roster
//  entries. The self-test calls them for 0..NATIVE_CHAR_ROSTER_MAX entries
//  without a roster; the live functions further down call them with
//  NativeChar_RosterCount().
// ===========================================================================

// Groups the 15 tiles of D230.characterSelectMeta1P2P into rows by posY (top
// first), each sorted by posX, and reads the pitches from them. 0 if the table
// does not form rows of one pitch.
internal int MM_NativeCharGrid_Derive(struct MM_NativeGridGeometry *g)
{
	const struct CharacterSelectMeta *meta = D230.characterSelectMeta1P2P;
	int tile;
	int row;
	int col;
	int widest;

	memset(g, 0, sizeof(*g));

	for (tile = 0; tile < MM_CHARACTER_SELECT_ICON_COUNT; tile++)
	{
		const int y = meta[tile].posY;

		row = 0;
		while ((row < g->rowCount) && (g->rowY[row] < y))
		{
			row++;
		}

		if ((row == g->rowCount) || (g->rowY[row] != y))
		{
			int k;

			if (g->rowCount >= MM_NATIVE_GRID_RETAIL_ROWS_MAX)
			{
				return 0;
			}

			for (k = g->rowCount; k > row; k--)
			{
				g->rowY[k] = g->rowY[k - 1];
				g->rowSize[k] = g->rowSize[k - 1];
				memcpy(&g->rowTile[k][0], &g->rowTile[k - 1][0], sizeof(g->rowTile[k]));
			}

			g->rowY[row] = y;
			g->rowSize[row] = 0;
			g->rowCount++;
		}

		if (g->rowSize[row] >= MM_NATIVE_GRID_ROW_TILES_MAX)
		{
			return 0;
		}

		for (col = g->rowSize[row]; (col > 0) && (meta[g->rowTile[row][col - 1]].posX > meta[tile].posX); col--)
		{
			g->rowTile[row][col] = g->rowTile[row][col - 1];
		}

		g->rowTile[row][col] = (s16)tile;
		g->rowSize[row]++;
	}

	if (g->rowCount < 2)
	{
		return 0;
	}

	g->firstY = g->rowY[0];
	g->rowPitch = g->rowY[1] - g->rowY[0];

	for (row = 1; row < g->rowCount; row++)
	{
		if ((g->rowY[row] - g->rowY[row - 1]) != g->rowPitch)
		{
			return 0;
		}
	}

	widest = 0;
	for (row = 0; row < g->rowCount; row++)
	{
		// Two tiles of one row on one x would make no row.
		for (col = 1; col < g->rowSize[row]; col++)
		{
			if (meta[g->rowTile[row][col]].posX <= meta[g->rowTile[row][col - 1]].posX)
			{
				return 0;
			}
		}

		if (g->rowSize[row] > g->rowSize[widest])
		{
			widest = row;
		}
	}

	g->rowSizeMax = g->rowSize[widest];
	if (g->rowSizeMax < 2)
	{
		return 0;
	}

	g->colPitch = meta[g->rowTile[widest][1]].posX - meta[g->rowTile[widest][0]].posX;

	for (col = 1; col < g->rowSizeMax; col++)
	{
		if ((meta[g->rowTile[widest][col]].posX - meta[g->rowTile[widest][col - 1]].posX) != g->colPitch)
		{
			return 0;
		}
	}

	g->leftX = meta[g->rowTile[widest][0]].posX;
	g->centreX = (g->leftX + meta[g->rowTile[widest][g->rowSizeMax - 1]].posX) / 2;
	return 1;
}

// Three rows are visible: the screen of the retail layout.
internal int MM_NativeCharGrid_VisibleRows(const struct MM_NativeGridGeometry *g)
{
	return g->rowCount;
}

// The places for custom tiles in the last retail row (Fake Crash's row).
internal int MM_NativeCharGrid_Room(const struct MM_NativeGridGeometry *g)
{
	return g->rowSizeMax - g->rowSize[g->rowCount - 1];
}

internal int MM_NativeCharGrid_RowsFor(const struct MM_NativeGridGeometry *g, int entries)
{
	const int extra = entries - MM_NativeCharGrid_Room(g);

	if (extra <= 0)
	{
		return g->rowCount;
	}

	return g->rowCount + ((extra + g->rowSizeMax - 1) / g->rowSizeMax);
}

internal int MM_NativeCharGrid_RowSizeFor(const struct MM_NativeGridGeometry *g, int entries, int row)
{
	const int last = g->rowCount - 1;
	const int room = MM_NativeCharGrid_Room(g);
	int left;

	if ((row < 0) || (row >= MM_NativeCharGrid_RowsFor(g, entries)))
	{
		return 0;
	}

	if (row < last)
	{
		return g->rowSize[row];
	}

	if (row == last)
	{
		return g->rowSize[last] + ((entries < room) ? entries : room);
	}

	left = entries - room - ((row - g->rowCount) * g->rowSizeMax);
	return (left < g->rowSizeMax) ? left : g->rowSizeMax;
}

// The tile in a cell, or -1 for an empty cell.
internal int MM_NativeCharGrid_TileAt(const struct MM_NativeGridGeometry *g, int entries, int row, int col)
{
	const int last = g->rowCount - 1;

	if ((col < 0) || (col >= MM_NativeCharGrid_RowSizeFor(g, entries, row)))
	{
		return -1;
	}

	if (row < g->rowCount)
	{
		if (col < g->rowSize[row])
		{
			return g->rowTile[row][col];
		}

		return MM_CHARACTER_SELECT_ICON_COUNT + (col - g->rowSize[last]);
	}

	return MM_CHARACTER_SELECT_ICON_COUNT + MM_NativeCharGrid_Room(g) + ((row - g->rowCount) * g->rowSizeMax) + col;
}

// The cell of a tile. 0 if the tile is not in the grid.
internal int MM_NativeCharGrid_Place(const struct MM_NativeGridGeometry *g, int entries, int tile, int *outRow, int *outCol)
{
	const int last = g->rowCount - 1;
	const int room = MM_NativeCharGrid_Room(g);
	int entry;
	int row;
	int col;

	if ((tile >= 0) && (tile < MM_CHARACTER_SELECT_ICON_COUNT))
	{
		for (row = 0; row < g->rowCount; row++)
		{
			for (col = 0; col < g->rowSize[row]; col++)
			{
				if (g->rowTile[row][col] == tile)
				{
					*outRow = row;
					*outCol = col;
					return 1;
				}
			}
		}

		return 0;
	}

	entry = tile - MM_CHARACTER_SELECT_ICON_COUNT;
	if ((entry < 0) || (entry >= entries))
	{
		return 0;
	}

	if (entry < room)
	{
		*outRow = last;
		*outCol = g->rowSize[last] + entry;
		return 1;
	}

	entry -= room;
	*outRow = g->rowCount + (entry / g->rowSizeMax);
	*outCol = entry % g->rowSizeMax;
	return 1;
}

// x of a cell: the row stands centred.
internal int MM_NativeCharGrid_XFor(const struct MM_NativeGridGeometry *g, int entries, int row, int col)
{
	const int size = MM_NativeCharGrid_RowSizeFor(g, entries, row);

	return g->centreX + ((((2 * col) - (size - 1)) * g->colPitch) / 2);
}

// The y a row has on an unscrolled screen.
internal int MM_NativeCharGrid_YFor(const struct MM_NativeGridGeometry *g, int row)
{
	return g->firstY + (row * g->rowPitch);
}

// The grid neighbour for a d-pad press (block NAVIGATION). Returns the tile
// itself where there is nothing to go to.
internal int MM_NativeCharGrid_GridNext(const struct MM_NativeGridGeometry *g, int entries, int direction, int tile)
{
	int row;
	int col;
	int target;
	int size;
	int x0;
	int c;
	int best;
	int bestDistance;

	if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
	{
		return tile;
	}

	switch (direction)
	{
	case CHARACTER_SELECT_DIR_LEFT:
		return (col > 0) ? MM_NativeCharGrid_TileAt(g, entries, row, col - 1) : tile;

	case CHARACTER_SELECT_DIR_RIGHT:
		return ((col + 1) < MM_NativeCharGrid_RowSizeFor(g, entries, row)) ? MM_NativeCharGrid_TileAt(g, entries, row, col + 1) : tile;

	case CHARACTER_SELECT_DIR_UP:
		target = row - 1;
		break;

	case CHARACTER_SELECT_DIR_DOWN:
		target = row + 1;
		break;

	default:
		return tile;
	}

	if ((target < 0) || (target >= MM_NativeCharGrid_RowsFor(g, entries)))
	{
		return tile;
	}

	x0 = MM_NativeCharGrid_XFor(g, entries, row, col);
	size = MM_NativeCharGrid_RowSizeFor(g, entries, target);
	best = -1;
	bestDistance = 0;

	for (c = 0; c < size; c++)
	{
		const int distance = abs(MM_NativeCharGrid_XFor(g, entries, target, c) - x0);

		// The columns run left to right: on a tie down moves on to the
		// right one, up keeps the left one (retail: Dingo down -> Penta,
		// Penta up -> Dingo).
		if ((best < 0) || (distance < bestDistance) || ((distance == bestDistance) && (direction == CHARACTER_SELECT_DIR_DOWN)))
		{
			best = c;
			bestDistance = distance;
		}
	}

	return MM_NativeCharGrid_TileAt(g, entries, target, best);
}

internal int MM_NativeCharGrid_ScrollMax(const struct MM_NativeGridGeometry *g, int entries)
{
	const int over = MM_NativeCharGrid_RowsFor(g, entries) - MM_NativeCharGrid_VisibleRows(g);

	return (over > 0) ? over : 0;
}

internal int MM_NativeCharGrid_ScrollClamp(const struct MM_NativeGridGeometry *g, int entries, int scroll)
{
	const int scrollMax = MM_NativeCharGrid_ScrollMax(g, entries);

	if (scroll < 0)
	{
		return 0;
	}

	return (scroll > scrollMax) ? scrollMax : scroll;
}

// The scroll after the cursor moved to a row: just enough that the row is
// visible.
internal int MM_NativeCharGrid_ScrollFollow(const struct MM_NativeGridGeometry *g, int entries, int scroll, int row)
{
	const int visible = MM_NativeCharGrid_VisibleRows(g);

	if (row < scroll)
	{
		scroll = row;
	}
	else if (row > (scroll + visible - 1))
	{
		scroll = row - (visible - 1);
	}

	return MM_NativeCharGrid_ScrollClamp(g, entries, scroll);
}

// The scroll on entering the screen: the row of the cursor as low as it goes.
internal int MM_NativeCharGrid_ScrollEnter(const struct MM_NativeGridGeometry *g, int entries, int row)
{
	return MM_NativeCharGrid_ScrollClamp(g, entries, row - (MM_NativeCharGrid_VisibleRows(g) - 1));
}

// MM_NATIVE_GRID_ARROW_UP while a row lies above the visible ones,
// MM_NATIVE_GRID_ARROW_DOWN while one lies below.
internal int MM_NativeCharGrid_ArrowsFor(const struct MM_NativeGridGeometry *g, int entries, int scroll)
{
	int arrows = 0;

	if (scroll > 0)
	{
		arrows |= MM_NATIVE_GRID_ARROW_UP;
	}

	if ((scroll + MM_NativeCharGrid_VisibleRows(g)) < MM_NativeCharGrid_RowsFor(g, entries))
	{
		arrows |= MM_NATIVE_GRID_ARROW_DOWN;
	}

	return arrows;
}

// When a custom tile starts to fly, from its x: the retail tiles of a full row
// start from the right, one frame apart (D230.c:483-485 - Joe, Penta and Fake
// Crash get 4, 3 and 2 from this).
internal int MM_NativeCharGrid_HeadStartFor(const struct MM_NativeGridGeometry *g, int x)
{
	return (g->rowSizeMax - 1) - ((x - g->leftX) / g->colPitch);
}

// ===========================================================================
//  THE LIVE GRID
// ===========================================================================

global_variable struct MM_NativeGridGeometry s_gridGeometry;

// 0 not read yet, 1 read, -1 the table forms no grid (the grid stays off).
global_variable int s_gridGeometryState = 0;

// The first visible row.
global_variable int s_gridScroll = 0;

// The tile of the cursor of player 1 in this frame: set at the start of the
// frame (MM_NativeCharGrid_CursorTile), after the d-pad (Follow) and with the
// pick (WritePick).
global_variable s16 s_gridCursorTile = 0;

// The tile of the last cursor log line.
global_variable s16 s_gridLoggedTile = -1;

// The preview window of player 1 in tiles, beside the retail
// current/desiredCharacterID: Fake Crash and a custom character on his
// template have the same ID, and the model still has to fly.
global_variable s16 s_gridPreviewCurrentTile = 0;
global_variable s16 s_gridPreviewDesiredTile = 0;

// One transition per roster entry.
global_variable struct TransitionMeta s_gridTransition[NATIVE_CHAR_ROSTER_MAX];

// The arrow line comes once per run.
global_variable int s_gridArrowsLogged = 0;

global_variable const char *const s_gridArrowNames[4] = {"none", "up", "down", "up+down"};

internal const struct MM_NativeGridGeometry *MM_NativeCharGrid_Geometry(void)
{
	if (s_gridGeometryState == 0)
	{
		s_gridGeometryState = MM_NativeCharGrid_Derive(&s_gridGeometry) ? 1 : -1;
	}

	return (s_gridGeometryState == 1) ? &s_gridGeometry : NULL;
}

internal int MM_NativeCharGrid_Entries(void)
{
	const int count = NativeChar_RosterCount();

	if (count < 0)
	{
		return 0;
	}

	return (count > NATIVE_CHAR_ROSTER_MAX) ? NATIVE_CHAR_ROSTER_MAX : count;
}

internal int MM_NativeCharGrid_IsRetailTile(int tile)
{
	return (tile >= 0) && (tile < MM_CHARACTER_SELECT_ICON_COUNT);
}

// The index for the retail tables. A tile 0..14 is itself - the retail
// expression stays exactly as it was. Anything else, which retail would read
// past its table with, reads tile 0 instead.
internal int MM_NativeCharGrid_RetailIndex(int tile)
{
	return MM_NativeCharGrid_IsRetailTile(tile) ? tile : 0;
}

// The roster entry of a custom tile; -1 for a retail tile and for anything
// outside the grid.
internal int MM_NativeCharGrid_EntryOf(int tile)
{
	const int entry = tile - MM_CHARACTER_SELECT_ICON_COUNT;

	return ((entry >= 0) && (entry < MM_NativeCharGrid_Entries())) ? entry : -1;
}

// The retail condition of MM_Characters.c for a drawn icon. unlockFlags is
// read as s16 like there: 0xFFFF must compare as MM_CHARACTER_UNLOCK_ALWAYS,
// or CHECK_ADV_BIT would read far outside the unlock words.
internal int MM_NativeCharGrid_RetailDrawn(int tile)
{
	s16 unlockRequirement = D230.activeCharacterSelectMeta[MM_NativeCharGrid_RetailIndex(tile)].unlockFlags;

	return (unlockRequirement == MM_CHARACTER_UNLOCK_ALWAYS) || CHECK_ADV_BIT(sdata->gameProgress.unlocks, unlockRequirement);
}

// ===========================================================================
//  THE NAVIGATION (block NAVIGATION at the top), as pure functions of the
//  geometry, the number of entries and the D230 tables. locks = 1 reads the
//  unlock bits as MM_Characters_GetNextDriver does; the self-test passes 0,
//  every driver unlocked, as the native game keeps them.
// ===========================================================================

// The neighbour functions of the detour.
#define MM_NATIVE_GRID_NEIGHBOUR_TABLE 0
#define MM_NATIVE_GRID_NEIGHBOUR_GRID 1

// The neighbour in the retail table. Layout 0 only, where
// D230.activeCharacterSelectMeta is this table (MM_Characters.c:432).
internal int MM_NativeCharGrid_RetailNext(int direction, int icon)
{
	return D230.characterSelectMeta1P2P[icon].nextIconByDirection[direction];
}

// One step of a neighbour function. The table is only asked for retail tiles,
// and it only names retail tiles. A locked retail tile is no neighbour: the
// tile stays, as MM_Characters_GetNextDriver keeps it.
internal int MM_NativeCharGrid_Neighbour(const struct MM_NativeGridGeometry *g, int entries, int kind, int locks, int direction, int tile)
{
	int next;

	if (kind == MM_NATIVE_GRID_NEIGHBOUR_TABLE)
	{
		next = MM_NativeCharGrid_RetailNext(direction, tile);
	}
	else
	{
		next = MM_NativeCharGrid_GridNext(g, entries, direction, tile);
	}

	if (locks && MM_NativeCharGrid_IsRetailTile(next) && !MM_NativeCharGrid_RetailDrawn(next))
	{
		return tile;
	}

	return next;
}

// One press with one player as MM_Characters.c:848-923 makes it, on the
// neighbour function kind: the neighbour, and at a dead end the four detours
// in their order. With one player no tile is taken by another, so the retail
// loop runs once and every "taken" test is false.
internal int MM_NativeCharGrid_Detour(const struct MM_NativeGridGeometry *g, int entries, int kind, int locks, int direction, int current)
{
	const int fallback1 = D230.characterSelectFallbackDirection1[direction];
	const int fallback2 = D230.characterSelectFallbackDirection2[direction];
	const int previous = current;
	int alternate = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, direction, previous);
	int next;
	int middle;

	if (alternate != previous)
	{
		return alternate;
	}

	next = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, direction, current);
	middle = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, fallback1, next);
	if ((middle != alternate) && (next != alternate) && (next != middle))
	{
		return middle;
	}

	middle = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, fallback1, current);
	alternate = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, direction, middle);
	if ((alternate != previous) && (middle != previous) && (middle != alternate))
	{
		return alternate;
	}

	middle = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, direction, current);
	alternate = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, fallback2, middle);
	if ((alternate != previous) && (middle != previous) && (middle != alternate))
	{
		return alternate;
	}

	middle = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, fallback2, current);
	alternate = MM_NativeCharGrid_Neighbour(g, entries, kind, locks, direction, middle);
	if ((alternate != previous) && (middle != previous) && (middle != alternate))
	{
		return alternate;
	}

	return current;
}

// The retail press with one player: the table and its detour.
internal int MM_NativeCharGrid_RetailStep(int locks, int direction, int current)
{
	return MM_NativeCharGrid_Detour(NULL, 0, MM_NATIVE_GRID_NEIGHBOUR_TABLE, locks, direction, current);
}

// The next tile for a d-pad press: steps (1) to (4) of block NAVIGATION.
// Returns the tile itself where there is nothing to go to.
internal int MM_NativeCharGrid_NextFor(const struct MM_NativeGridGeometry *g, int entries, int locks, int direction, int tile)
{
	int row;
	int col;
	int next;

	if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
	{
		return tile;
	}

	// (4) A custom tile: the grid, with the detour at a dead end.
	if (!MM_NativeCharGrid_IsRetailTile(tile))
	{
		return MM_NativeCharGrid_Detour(g, entries, MM_NATIVE_GRID_NEIGHBOUR_GRID, locks, direction, tile);
	}

	// (1) Left or right onto a custom tile in the row.
	if ((direction == CHARACTER_SELECT_DIR_LEFT) || (direction == CHARACTER_SELECT_DIR_RIGHT))
	{
		next = MM_NativeCharGrid_Neighbour(g, entries, MM_NATIVE_GRID_NEIGHBOUR_GRID, locks, direction, tile);

		if (!MM_NativeCharGrid_IsRetailTile(next))
		{
			return next;
		}
	}

	// (2) The retail press, wherever it moves.
	next = MM_NativeCharGrid_RetailStep(locks, direction, tile);
	if (next != tile)
	{
		return next;
	}

	// (3) A retail dead end: the grid.
	return MM_NativeCharGrid_Neighbour(g, entries, MM_NATIVE_GRID_NEIGHBOUR_GRID, locks, direction, tile);
}

int MM_NativeCharGrid_Active(void)
{
	// The roster first: in a run without custom characters that is the only
	// question asked.
	if (NativeChar_RosterCount() < 1)
	{
		return 0;
	}

	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		return 0;
	}

	return (sdata->gGT->numPlyrNextGame == 1) && (D230.characterSelectLayoutIndex == 0) && NativeChar_ModeAllowed() &&
	       (MM_NativeCharGrid_Geometry() != NULL);
}

// The cursor tile from the pick: a pick whose template is in characterIDs[0]
// stands on its own tile - a placeholder too - else the cursor is the retail
// tile of the ID. So the cursor comes back to a custom tile after the track
// select or a race, and a pick that no longer matches the ID counts for
// nothing.
internal s16 MM_NativeCharGrid_TileFromPick(s16 retailIcon)
{
	const int pick = NativeChar_Pick();

	if ((pick >= 0) && (pick < MM_NativeCharGrid_Entries()) && (NativeChar_EntryTemplate(pick) == data.characterIDs[0]))
	{
		return (s16)(MM_CHARACTER_SELECT_ICON_COUNT + pick);
	}

	return retailIcon;
}

// One step of MM_TransitionInOut for one entry (MM_MenuFlow.c:3-42), the same
// arithmetic without its swoosh: the retail list plays that once, at its
// index 0, and a second one would sound twice.
internal void MM_NativeCharGrid_Slide(struct TransitionMeta *meta, int framesPassed, int numFrames)
{
	s16 start = meta->headStart;
	s16 framesLeft = ((s16)framesPassed - start);

	if (framesLeft < 1)
	{
		meta->currX = 0;
		meta->currY = 0;
		return;
	}

	if (framesLeft < (s16)numFrames)
	{
		meta->currX = framesLeft * meta->distX / (s16)numFrames;
		meta->currY = framesLeft * meta->distY / (s16)numFrames;
		return;
	}

	meta->currX = meta->distX;
	meta->currY = meta->distY;
}

// Sets every custom transition to its frame of the screen transition.
internal void MM_NativeCharGrid_SlideAll(int framesPassed, int numFrames)
{
	const struct MM_NativeGridGeometry *g = MM_NativeCharGrid_Geometry();
	const int entries = MM_NativeCharGrid_Entries();
	int entry;

	for (entry = 0; entry < entries; entry++)
	{
		struct TransitionMeta *meta = &s_gridTransition[entry];
		int row;
		int col;

		if (!MM_NativeCharGrid_Place(g, entries, MM_CHARACTER_SELECT_ICON_COUNT + entry, &row, &col))
		{
			continue;
		}

		meta->distX = MM_NATIVE_GRID_TRANSITION_DIST_X;
		meta->distY = MM_NATIVE_GRID_TRANSITION_DIST_Y;
		meta->headStart = (s16)MM_NativeCharGrid_HeadStartFor(g, MM_NativeCharGrid_XFor(g, entries, row, col));

		MM_NativeCharGrid_Slide(meta, framesPassed, numFrames);
	}
}

// The cursor of player 1 is on tile: scroll so that its row is visible, and
// one log line when the tile is a new one.
internal void MM_NativeCharGrid_NoteCursor(s16 tile)
{
	const struct MM_NativeGridGeometry *g = MM_NativeCharGrid_Geometry();
	const int entries = MM_NativeCharGrid_Entries();
	int row;
	int col;

	if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
	{
		return;
	}

	s_gridCursorTile = tile;
	s_gridScroll = MM_NativeCharGrid_ScrollFollow(g, entries, s_gridScroll, row);

	if (tile != s_gridLoggedTile)
	{
		Platform_Log("[CTR Char] grid cursor: tile %d '%s' row %d col %d scroll %d arrows %s\n", (int)tile, MM_NativeCharGrid_TileName(tile), row, col,
		             s_gridScroll, s_gridArrowNames[MM_NativeCharGrid_ArrowsFor(g, entries, s_gridScroll)]);
		s_gridLoggedTile = tile;
	}
}

// The scale for the arrow icon: its half height at most
// MM_NATIVE_GRID_ARROW_HALF_MAX (block THE ARROWS above).
internal int MM_NativeCharGrid_ArrowScale(const struct Icon *icon)
{
	const int height = (int)icon->texLayout.v2 - (int)icon->texLayout.v0;

	if (((height * FP(1.0)) >> DECAL_HUD_ARROW_SCALE_SHIFT) <= MM_NATIVE_GRID_ARROW_HALF_MAX)
	{
		return FP(1.0);
	}

	return (MM_NATIVE_GRID_ARROW_HALF_MAX << DECAL_HUD_ARROW_SCALE_SHIFT) / height;
}

int MM_NativeCharGrid_TileCount(void)
{
	if (!MM_NativeCharGrid_Active())
	{
		return MM_CHARACTER_SELECT_ICON_COUNT;
	}

	return MM_CHARACTER_SELECT_ICON_COUNT + MM_NativeCharGrid_Entries();
}

s16 MM_NativeCharGrid_CursorTile(s16 retailIcon)
{
	if (!MM_NativeCharGrid_Active())
	{
		return retailIcon;
	}

	s_gridCursorTile = MM_NativeCharGrid_TileFromPick(retailIcon);
	return s_gridCursorTile;
}

s16 MM_NativeCharGrid_Next(int direction, s16 tile)
{
	int next;

	if (!MM_NativeCharGrid_Active())
	{
		return tile;
	}

	next = MM_NativeCharGrid_NextFor(MM_NativeCharGrid_Geometry(), MM_NativeCharGrid_Entries(), 1, direction, tile);

	// A locked retail tile is never entered: the cursor stays, as
	// MM_Characters_GetNextDriver keeps it. The neighbour functions skip
	// locked tiles already, and the native game unlocks every driver in each
	// menu frame (NativeMenuLock_Tick), so this only guards.
	if (MM_NativeCharGrid_IsRetailTile(next) && !MM_NativeCharGrid_RetailDrawn(next))
	{
		return tile;
	}

	return (s16)next;
}

void MM_NativeCharGrid_Follow(s16 tile)
{
	if (!MM_NativeCharGrid_Active())
	{
		return;
	}

	MM_NativeCharGrid_NoteCursor(tile);
}

int MM_NativeCharGrid_TileDrawn(int tile)
{
	const struct MM_NativeGridGeometry *g;
	int row;
	int col;

	if (!MM_NativeCharGrid_Active())
	{
		return MM_NativeCharGrid_RetailDrawn(tile);
	}

	g = MM_NativeCharGrid_Geometry();

	if (!MM_NativeCharGrid_Place(g, MM_NativeCharGrid_Entries(), tile, &row, &col))
	{
		return 0;
	}

	// Rows outside the visible three get nothing: no portrait, no frame, no
	// background.
	if ((row < s_gridScroll) || (row > (s_gridScroll + MM_NativeCharGrid_VisibleRows(g) - 1)))
	{
		return 0;
	}

	if (MM_NativeCharGrid_IsRetailTile(tile))
	{
		return MM_NativeCharGrid_RetailDrawn(tile);
	}

	return 1;
}

s16 MM_NativeCharGrid_TileX(int tile)
{
	const struct MM_NativeGridGeometry *g;
	const int entries = MM_NativeCharGrid_Entries();
	int row;
	int col;

	if (!MM_NativeCharGrid_Active())
	{
		return D230.activeCharacterSelectMeta[MM_NativeCharGrid_RetailIndex(tile)].posX;
	}

	g = MM_NativeCharGrid_Geometry();

	if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
	{
		return 0;
	}

	return (s16)MM_NativeCharGrid_XFor(g, entries, row, col);
}

s16 MM_NativeCharGrid_TileY(int tile)
{
	const struct MM_NativeGridGeometry *g;
	int row;
	int col;

	if (!MM_NativeCharGrid_Active())
	{
		return D230.activeCharacterSelectMeta[MM_NativeCharGrid_RetailIndex(tile)].posY;
	}

	g = MM_NativeCharGrid_Geometry();

	if (!MM_NativeCharGrid_Place(g, MM_NativeCharGrid_Entries(), tile, &row, &col))
	{
		return 0;
	}

	return (s16)MM_NativeCharGrid_YFor(g, row - s_gridScroll);
}

s16 MM_NativeCharGrid_TileCharacterID(int tile)
{
	int entry;
	int templateId;

	if (!MM_NativeCharGrid_Active() || MM_NativeCharGrid_IsRetailTile(tile))
	{
		return D230.activeCharacterSelectMeta[MM_NativeCharGrid_RetailIndex(tile)].characterID;
	}

	entry = MM_NativeCharGrid_EntryOf(tile);
	templateId = NativeChar_EntryTemplate(entry);

	if ((templateId < 0) || (templateId >= MM_CHARACTER_SELECT_ICON_COUNT))
	{
		return CRASH_BANDICOOT;
	}

	return (s16)templateId;
}

// The portrait of a tile. Retail (grid off or a retail tile) is the retail
// expression of the call site in MM_Characters.c, unchanged - the same pointer,
// so the same primitive. A custom tile asks the roster: its own portrait (CICN)
// or the template's, which a custom tile has always shown (placeholders of
// --dev-grid-fill have no file and stay on the template's).
struct Icon *MM_NativeCharGrid_TileIcon(int tile)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Icon *templateIcon = gGT->ptrIcons[data.MetaDataCharacters[MM_NativeCharGrid_TileCharacterID(tile)].iconID];

	if (!MM_NativeCharGrid_Active() || MM_NativeCharGrid_IsRetailTile(tile))
	{
		return templateIcon;
	}

	return NativeChar_EntryPortrait(MM_NativeCharGrid_EntryOf(tile), templateIcon);
}

struct TransitionMeta *MM_NativeCharGrid_TileTransition(int tile)
{
	int entry;

	if (!MM_NativeCharGrid_Active() || MM_NativeCharGrid_IsRetailTile(tile))
	{
		return &D230.characterSelectTransitionMeta[MM_NativeCharGrid_RetailIndex(tile)];
	}

	entry = MM_NativeCharGrid_EntryOf(tile);
	return &s_gridTransition[(entry >= 0) ? entry : 0];
}

char *MM_NativeCharGrid_TileName(int tile)
{
	if (!MM_NativeCharGrid_Active() || MM_NativeCharGrid_IsRetailTile(tile))
	{
		return sdata->lngStrings[data.MetaDataCharacters[D230.activeCharacterSelectMeta[MM_NativeCharGrid_RetailIndex(tile)].characterID].name_LNG_long];
	}

	return (char *)NativeChar_EntryName(MM_NativeCharGrid_EntryOf(tile));
}

int MM_NativeCharGrid_Selectable(int tile)
{
	int entry;
	int number;
	int e;

	if (!MM_NativeCharGrid_Active())
	{
		return 1;
	}

	entry = MM_NativeCharGrid_EntryOf(tile);
	if (entry < 0)
	{
		return MM_NativeCharGrid_IsRetailTile(tile);
	}

	if (!NativeChar_EntryIsPlaceholder(entry))
	{
		return 1;
	}

	// The placeholders follow the files: n counts them from 1, as their names
	// do.
	number = 0;
	for (e = 0; e <= entry; e++)
	{
		number += NativeChar_EntryIsPlaceholder(e) ? 1 : 0;
	}

	Platform_Log("[CTR Char] grid: placeholder %d cannot be chosen\n", number);
	return 0;
}

void MM_NativeCharGrid_Transition(int framesPassed, int numFrames)
{
	if (!MM_NativeCharGrid_Active())
	{
		return;
	}

	MM_NativeCharGrid_SlideAll(framesPassed, numFrames);
}

void MM_NativeCharGrid_Enter(void)
{
	const struct MM_NativeGridGeometry *g;
	const int entries = MM_NativeCharGrid_Entries();
	const s16 characterID = data.characterIDs[0];
	s16 retailIcon = 0;
	s16 tile;
	int row;
	int col;

	// The own portraits go into the strip again at the first draw of this
	// screen: a race or a video in between may have used the VRAM. Only a
	// flag - without a portrait to show nothing is uploaded.
	NativeChar_PortraitsDirty();

	if (!MM_NativeCharGrid_Active())
	{
		return;
	}

	g = MM_NativeCharGrid_Geometry();

	// RestoreIDs has just filled characterMenuID for the active table.
	if ((characterID >= 0) && (characterID < (s16)(sizeof(D230.characterMenuID) / sizeof(D230.characterMenuID[0]))))
	{
		retailIcon = D230.characterMenuID[characterID];
	}

	tile = MM_NativeCharGrid_TileFromPick(retailIcon);

	if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
	{
		tile = 0;
		row = 0;
		col = 0;
		MM_NativeCharGrid_Place(g, entries, tile, &row, &col);
	}

	s_gridCursorTile = tile;
	s_gridLoggedTile = tile;
	s_gridScroll = MM_NativeCharGrid_ScrollEnter(g, entries, row);

	// RestoreIDs sets current = desired = characterIDs; the tiles go along.
	s_gridPreviewCurrentTile = tile;
	s_gridPreviewDesiredTile = tile;

	// Out of the picture, where the first frame of the screen transition
	// takes them anyway.
	MM_NativeCharGrid_SlideAll((int)D230.characterSelectTransitionFrame, MM_CHARACTER_SELECT_TRANSITION_STEP);

	Platform_Log("[CTR Char] grid: %d custom tile(s), %d rows, cursor tile %d, scroll %d\n", entries, MM_NativeCharGrid_RowsFor(g, entries), (int)tile,
	             s_gridScroll);
}

void MM_NativeCharGrid_WritePick(s16 tile0)
{
	if (!MM_NativeCharGrid_Active())
	{
		NativeChar_SetPick(-1);
		return;
	}

	MM_NativeCharGrid_NoteCursor(tile0);

	// -1 on a retail tile: the funnel must not bind Fake Crash because a
	// custom character shares his template.
	NativeChar_SetPick(MM_NativeCharGrid_EntryOf(tile0));
}

void MM_NativeCharGrid_LeaveBackward(void)
{
	// The way back to the title drops the pick in every case. While the grid
	// is off the pick is -1 already (WritePick), so nothing changes then.
	NativeChar_SetPick(-1);
}

void MM_NativeCharGrid_DrawArrows(void)
{
	const struct MM_NativeGridGeometry *g;
	struct GameTracker *gGT;
	struct Icon **iconPtrArray;
	struct Icon *icon;
	u32 *colorPtr;
	int entries;
	int arrows;
	int scale;
	int k;

	if (!MM_NativeCharGrid_Active())
	{
		return;
	}

	g = MM_NativeCharGrid_Geometry();
	entries = MM_NativeCharGrid_Entries();
	arrows = MM_NativeCharGrid_ArrowsFor(g, entries, s_gridScroll);

	if (arrows == 0)
	{
		return;
	}

	gGT = sdata->gGT;

	// The group may be missing or shorter than the icon (as DecalFont.c:185-190
	// asks): then no arrow.
	if ((gGT->iconGroup[MM_NATIVE_GRID_ARROW_ICON_GROUP] == NULL) ||
	    (gGT->iconGroup[MM_NATIVE_GRID_ARROW_ICON_GROUP]->numIcons <= MM_NATIVE_GRID_ARROW_ICON))
	{
		return;
	}

	iconPtrArray = ICONGROUP_GETICONS(gGT->iconGroup[MM_NATIVE_GRID_ARROW_ICON_GROUP]);
	icon = iconPtrArray[MM_NATIVE_GRID_ARROW_ICON];

	if (icon == NULL)
	{
		return;
	}

	// get color data
	colorPtr = data.ptrColor[((sdata->frameCounter & MM_NATIVE_GRID_ARROW_FLASH_BIT) == 0) ? RED : ORANGE];
	scale = MM_NativeCharGrid_ArrowScale(icon);

	if (!s_gridArrowsLogged)
	{
		const int half = ((int)icon->texLayout.v2 - (int)icon->texLayout.v0) * scale >> DECAL_HUD_ARROW_SCALE_SHIFT;

		Platform_Log("[CTR Char] grid arrows: icon %dx%d, scale 0x%x, box x %d..%d (up) and %d..%d (down)\n",
		             (int)icon->texLayout.u1 - (int)icon->texLayout.u0, (int)icon->texLayout.v2 - (int)icon->texLayout.v0, scale,
		             MM_NATIVE_GRID_ARROW_X - half, MM_NATIVE_GRID_ARROW_X + half + 1, MM_NATIVE_GRID_ARROW_X - half - 1, MM_NATIVE_GRID_ARROW_X + half);
		s_gridArrowsLogged = 1;
	}

	for (k = 0; k < 2; k++)
	{
		const int up = (k == 0);
		int row;
		int tile;
		struct TransitionMeta *tMeta;

		if ((arrows & (up ? MM_NATIVE_GRID_ARROW_UP : MM_NATIVE_GRID_ARROW_DOWN)) == 0)
		{
			continue;
		}

		// Beside the top visible row or the bottom one, flying in with the
		// tile at the right end of that row.
		row = up ? s_gridScroll : (s_gridScroll + MM_NativeCharGrid_VisibleRows(g) - 1);
		tile = MM_NativeCharGrid_TileAt(g, entries, row, MM_NativeCharGrid_RowSizeFor(g, entries, row) - 1);
		tMeta = MM_NativeCharGrid_TileTransition(tile);

		DecalHUD_Arrow2D(icon, (s16)(MM_NATIVE_GRID_ARROW_X + tMeta->currX),
		                 (s16)(MM_NativeCharGrid_YFor(g, row - s_gridScroll) + MM_NATIVE_GRID_ARROW_Y_FROM_ROW + tMeta->currY),
		                 &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT, colorPtr[0], colorPtr[1], colorPtr[2], colorPtr[3], 0, scale,
		                 up ? MM_NATIVE_GRID_ARROW_ROT_UP : MM_NATIVE_GRID_ARROW_ROT_DOWN);
	}
}

int MM_NativeCharGrid_PreviewMoveWanted(int playerIndex)
{
	if (!MM_NativeCharGrid_Active() || (playerIndex != 0))
	{
		return D230.characterSelectPlayerState.currentCharacterID[playerIndex] != data.characterIDs[playerIndex];
	}

	return s_gridPreviewCurrentTile != s_gridCursorTile;
}

void MM_NativeCharGrid_PreviewDesire(int playerIndex)
{
	if (!MM_NativeCharGrid_Active() || (playerIndex != 0))
	{
		return;
	}

	s_gridPreviewDesiredTile = s_gridCursorTile;
}

void MM_NativeCharGrid_PreviewArrive(int playerIndex)
{
	if (!MM_NativeCharGrid_Active() || (playerIndex != 0))
	{
		return;
	}

	s_gridPreviewCurrentTile = s_gridPreviewDesiredTile;
}

void MM_NativeCharGrid_PreviewPose(int playerIndex, struct Instance *driverInst)
{
	struct Model *model;
	int entry;

	if (!MM_NativeCharGrid_Active() || (playerIndex != 0) || (driverInst == NULL))
	{
		return;
	}

	// A placeholder has no model: retail has set the one of its template.
	entry = MM_NativeCharGrid_EntryOf(s_gridPreviewCurrentTile);
	model = NativeChar_EntryModel(entry);

	if (model == NULL)
	{
		return;
	}

	// A race model, not a menu model: animation 0 at its middle frame, the
	// neutral steer (NativeChar_EntryMenuFrame).
	driverInst->model = model;
	driverInst->animIndex = 0;
	driverInst->animFrame = (s16)NativeChar_EntryMenuFrame(entry);
}

int MM_NativeCharGrid_PreviewCustom(void)
{
	// The model PreviewPose puts into the preview window: a roster entry with
	// a model on the current preview tile. A placeholder shows its template.
	// Only while the driver select runs (its exit transition included): the
	// preview tile keeps its last value after any way out - title, track or
	// cup select, a race confirmed before the model had flown - and must not
	// count there.
	if (!MM_NativeCharGrid_Active() || (sdata->ptrActiveMenu != &D230.menuCharacterSelect))
	{
		return 0;
	}

	return NativeChar_EntryModel(MM_NativeCharGrid_EntryOf(s_gridPreviewCurrentTile)) != NULL;
}

int MM_NativeCharGrid_NameShown(int playerIndex)
{
	if (!MM_NativeCharGrid_Active() || (playerIndex != 0))
	{
		return D230.characterSelectPlayerState.currentCharacterID[playerIndex] == data.characterIDs[playerIndex];
	}

	// The cursor tile of this frame (WritePick has run): on the step Fake
	// Crash -> a custom tile on his template the ID stays, the tile does not,
	// and no name must flash up before the model has flown.
	return s_gridPreviewCurrentTile == s_gridCursorTile;
}

const char *MM_NativeCharGrid_SeatName(int index)
{
	int entry;

	if (!MM_NativeCharGrid_Active() || (index != 0))
	{
		return NULL;
	}

	entry = MM_NativeCharGrid_EntryOf(s_gridCursorTile);
	return (entry >= 0) ? NativeChar_EntryName(entry) : NULL;
}

// ===========================================================================
//  THE SELF-TEST (--char-grid-selftest, ctest char_grid_selftest).
//
//  Runs before anything is initialised: no window, no disc data, no roster,
//  no sdata. It reads only the D230 tables, which are initialised statically
//  (D230.c), and calls the pure layout functions above for every entry count
//  from 0 to NATIVE_CHAR_ROSTER_MAX. The expected numbers are the ones of the
//  layout rule, written out here on purpose: the grid derives them from the
//  table, the test holds them against it. The portrait slots of the roster
//  (NativeChar_PortraitSlot, NativeChar_PortraitLayout) are pure as well and
//  checked at the end, and so are the mask cases of NativeChar_MaskSelfTest.
// ===========================================================================

struct MM_NativeGridTest
{
	int checks;
	int failures;
	int entries;
	u32 random;
};

global_variable const char *const s_gridDirectionNames[CHARACTER_SELECT_DIRECTION_COUNT] = {"up", "down", "left", "right"};

// The retail rows as the layout rule names them, left to right.
global_variable const s16 s_gridTestRetailRows[3][6] = {{8, 0, 1, 2, 3, 9}, {10, 4, 5, 6, 7, 11}, {12, 13, 14}};
global_variable const int s_gridTestRetailRowSizes[3] = {6, 6, 3};

internal void MM_NativeCharGrid_Expect(struct MM_NativeGridTest *test, int ok, const char *format, ...)
{
	va_list args;

	test->checks++;

	if (ok)
	{
		return;
	}

	test->failures++;

	if (test->failures <= MM_NATIVE_GRID_TEST_REPORT_MAX)
	{
		printf("char grid selftest FAILED: %d entries: ", test->entries);
		va_start(args, format);
		vprintf(format, args);
		va_end(args);
		printf("\n");
	}
}

// The four presses retail moves only through its detour with one player
// (MM_Characters.c:848-923), written out: Roo down, Papu down, Joe left, Fake
// Crash right. The other 13 dead ends of the table stay.
global_variable const s16 s_gridTestRetailDetours[4][3] = {
    {10, CHARACTER_SELECT_DIR_DOWN, 12}, {11, CHARACTER_SELECT_DIR_DOWN, 14}, {12, CHARACTER_SELECT_DIR_LEFT, 10}, {14, CHARACTER_SELECT_DIR_RIGHT, 7}};

// THE GRID NEIGHBOUR ONCE MORE, from the cells and their x and not from
// MM_NativeCharGrid_GridNext: left and right one column in the row, up and
// down one row to the nearest x, on a tie down the rightmost and up the
// leftmost; off the grid the tile stays.
internal int MM_NativeCharGrid_TestGridNext(const struct MM_NativeGridGeometry *g, int entries, int direction, int tile)
{
	const int rows = MM_NativeCharGrid_RowsFor(g, entries);
	int row;
	int col;
	int target;
	int x0;
	int targetSize;
	int nearest = -1;
	int leftmost = -1;
	int rightmost = -1;
	int c;

	if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
	{
		return tile;
	}

	if ((direction == CHARACTER_SELECT_DIR_LEFT) || (direction == CHARACTER_SELECT_DIR_RIGHT))
	{
		const int size = MM_NativeCharGrid_RowSizeFor(g, entries, row);
		int expectCol = col + ((direction == CHARACTER_SELECT_DIR_LEFT) ? -1 : 1);

		expectCol = (expectCol < 0) ? 0 : ((expectCol >= size) ? (size - 1) : expectCol);
		return MM_NativeCharGrid_TileAt(g, entries, row, expectCol);
	}

	target = row + ((direction == CHARACTER_SELECT_DIR_UP) ? -1 : 1);
	if ((target < 0) || (target >= rows))
	{
		return tile;
	}

	x0 = MM_NativeCharGrid_XFor(g, entries, row, col);
	targetSize = MM_NativeCharGrid_RowSizeFor(g, entries, target);

	for (c = 0; c < targetSize; c++)
	{
		const int distance = abs(MM_NativeCharGrid_XFor(g, entries, target, c) - x0);

		if ((nearest < 0) || (distance < nearest))
		{
			nearest = distance;
		}
	}

	for (c = 0; c < targetSize; c++)
	{
		if (abs(MM_NativeCharGrid_XFor(g, entries, target, c) - x0) == nearest)
		{
			leftmost = (leftmost < 0) ? c : leftmost;
			rightmost = c;
		}
	}

	return MM_NativeCharGrid_TileAt(g, entries, target, (direction == CHARACTER_SELECT_DIR_DOWN) ? rightmost : leftmost);
}

// The retail press against the table and the four detours written out above:
// where the table moves, the press moves the same; at a dead end it takes the
// listed detour or stays.
internal void MM_NativeCharGrid_TestRetailStep(struct MM_NativeGridTest *test)
{
	int stays = 0;
	int tile;
	int direction;
	int k;

	for (tile = 0; tile < MM_CHARACTER_SELECT_ICON_COUNT; tile++)
	{
		for (direction = 0; direction < CHARACTER_SELECT_DIRECTION_COUNT; direction++)
		{
			const int table = MM_NativeCharGrid_RetailNext(direction, tile);
			const int step = MM_NativeCharGrid_RetailStep(0, direction, tile);
			int expect = table;

			if (table == tile)
			{
				for (k = 0; k < 4; k++)
				{
					if ((s_gridTestRetailDetours[k][0] == tile) && (s_gridTestRetailDetours[k][1] == direction))
					{
						expect = s_gridTestRetailDetours[k][2];
					}
				}

				stays += (expect == tile) ? 1 : 0;
			}

			MM_NativeCharGrid_Expect(test, step == expect, "retail tile %d %s gives %d, expected %d", tile, s_gridDirectionNames[direction], step, expect);
		}
	}

	MM_NativeCharGrid_Expect(test, stays == MM_NATIVE_GRID_TEST_RETAIL_STAYS, "%d retail dead ends stay, expected %d", stays,
	                         MM_NATIVE_GRID_TEST_RETAIL_STAYS);
}

internal int MM_NativeCharGrid_BoxesOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
	return (ax < (bx + bw)) && (bx < (ax + aw)) && (ay < (by + bh)) && (by < (ay + ah));
}

// THE LAYOUT RULE ONCE MORE, without the layout functions, so that the test
// does not check them against themselves: rows 0 and 1 hold six tiles; row 2
// Joe, Penta, Fake Crash and the first up to three entries; every further
// row six entries, the last one the rest.
internal int MM_NativeCharGrid_TestRowSize(int entries, int row)
{
	int left;

	if (row < 2)
	{
		return 6;
	}

	if (row == 2)
	{
		return 3 + ((entries < 3) ? entries : 3);
	}

	left = entries - 3 - (6 * (row - 3));
	return (left < 0) ? 0 : ((left > 6) ? 6 : left);
}

// The tile of a cell by the same rule: the entries in reading order.
internal int MM_NativeCharGrid_TestTileAt(int row, int col)
{
	if (row < 2)
	{
		return s_gridTestRetailRows[row][col];
	}

	if (row == 2)
	{
		return (col < 3) ? s_gridTestRetailRows[2][col] : (MM_CHARACTER_SELECT_ICON_COUNT + (col - 3));
	}

	return MM_CHARACTER_SELECT_ICON_COUNT + 3 + (6 * (row - 3)) + col;
}

// The arrow box for every height the texture layout of an icon can hold:
// MM_NativeCharGrid_ArrowScale on an icon on the stack, then the arithmetic
// of DecalHUD_Arrow2D (DecalHUD.c:156, :192). The half height stays at most
// MM_NATIVE_GRID_ARROW_HALF_MAX, an icon up to 15 high keeps its size, and the
// box up and down stays inside x 437..456. v0 moves in the second pass: the
// size is v2 - v0, not v2.
internal void MM_NativeCharGrid_TestArrows(struct MM_NativeGridTest *test)
{
	struct Icon icon;
	int pass;
	int height;

	for (pass = 0; pass < 2; pass++)
	{
		for (height = 0; height <= 255; height++)
		{
			int scale;
			int half;
			int upLeft;
			int upRight;
			int downLeft;
			int downRight;

			memset(&icon, 0, sizeof(icon));
			icon.texLayout.v0 = (u8)((pass == 0) ? 0 : (255 - height));
			icon.texLayout.v2 = (u8)(icon.texLayout.v0 + height);

			scale = MM_NativeCharGrid_ArrowScale(&icon);
			half = (int)(((u32)icon.texLayout.v2 - (u32)icon.texLayout.v0) * (u32)(s16)scale) >> DECAL_HUD_ARROW_SCALE_SHIFT;

			upLeft = MM_NATIVE_GRID_ARROW_X - half;
			upRight = MM_NATIVE_GRID_ARROW_X + half + 1;
			downLeft = MM_NATIVE_GRID_ARROW_X - half - 1;
			downRight = MM_NATIVE_GRID_ARROW_X + half;

			MM_NativeCharGrid_Expect(test, (scale > 0) && (scale <= FP(1.0)), "arrow height %d (v0 %d): scale 0x%x", height, (int)icon.texLayout.v0, scale);
			MM_NativeCharGrid_Expect(test, (height > 15) || (scale == FP(1.0)), "arrow height %d: scale 0x%x, not full size", height, scale);
			MM_NativeCharGrid_Expect(test, half <= MM_NATIVE_GRID_ARROW_HALF_MAX, "arrow height %d (v0 %d): half height %d", height, (int)icon.texLayout.v0,
			                         half);
			MM_NativeCharGrid_Expect(test, (upLeft >= MM_NATIVE_GRID_ARROW_BOX_LEFT) && (upRight <= MM_NATIVE_GRID_ARROW_BOX_RIGHT),
			                         "arrow height %d: up box x %d..%d", height, upLeft, upRight);
			MM_NativeCharGrid_Expect(test, (downLeft >= MM_NATIVE_GRID_ARROW_BOX_LEFT) && (downRight <= MM_NATIVE_GRID_ARROW_BOX_RIGHT),
			                         "arrow height %d: down box x %d..%d", height, downLeft, downRight);
			MM_NativeCharGrid_Expect(test, ((upRight - upLeft) <= 16) && ((downRight - downLeft) <= 16), "arrow height %d: %d columns wide", height,
			                         upRight - upLeft);
		}
	}
}

// The numbers of the layout rule against what the grid reads from the table.
internal void MM_NativeCharGrid_TestTable(struct MM_NativeGridTest *test, const struct MM_NativeGridGeometry *g)
{
	const struct CharacterSelectMeta *meta = D230.characterSelectMeta1P2P;
	const struct TransitionMeta *retailTransition = D230.characterSelectTransition1P2P;
	int row;
	int col;
	int tile;

	MM_NativeCharGrid_Expect(test, g->rowCount == 3, "%d retail rows, expected 3", g->rowCount);
	MM_NativeCharGrid_Expect(test, g->rowSizeMax == 6, "%d tiles in the widest row, expected 6", g->rowSizeMax);
	MM_NativeCharGrid_Expect(test, g->colPitch == 0x40, "column pitch 0x%x, expected 0x40", g->colPitch);
	MM_NativeCharGrid_Expect(test, g->rowPitch == 0x27, "row pitch 0x%x, expected 0x27", g->rowPitch);
	MM_NativeCharGrid_Expect(test, g->centreX == 0xE0, "centre 0x%x, expected 0xE0", g->centreX);
	MM_NativeCharGrid_Expect(test, g->firstY == 0x60, "row 0 at y 0x%x, expected 0x60", g->firstY);
	MM_NativeCharGrid_Expect(test, g->leftX == 0x40, "first column at x 0x%x, expected 0x40", g->leftX);

	if (g->rowCount != 3)
	{
		return;
	}

	for (row = 0; row < 3; row++)
	{
		MM_NativeCharGrid_Expect(test, g->rowSize[row] == s_gridTestRetailRowSizes[row], "row %d has %d tiles, expected %d", row, g->rowSize[row],
		                         s_gridTestRetailRowSizes[row]);

		for (col = 0; (col < g->rowSize[row]) && (col < s_gridTestRetailRowSizes[row]); col++)
		{
			MM_NativeCharGrid_Expect(test, g->rowTile[row][col] == s_gridTestRetailRows[row][col], "row %d col %d is tile %d, expected %d", row, col,
			                         (int)g->rowTile[row][col], (int)s_gridTestRetailRows[row][col]);
		}
	}

	// The transitions: every retail tile flies straight down by 0xC8, and the
	// head start of a custom tile comes out of its x as the retail one of the
	// tile in that place (Joe, Penta, Fake Crash: 4, 3, 2).
	for (tile = 0; tile < MM_CHARACTER_SELECT_ICON_COUNT; tile++)
	{
		MM_NativeCharGrid_Expect(test,
		                         (retailTransition[tile].distX == MM_NATIVE_GRID_TRANSITION_DIST_X) &&
		                             (retailTransition[tile].distY == MM_NATIVE_GRID_TRANSITION_DIST_Y),
		                         "retail transition of tile %d is %d,%d, not 0,0xC8", tile, retailTransition[tile].distX, retailTransition[tile].distY);
	}

	for (col = 0; col < g->rowSize[2]; col++)
	{
		tile = g->rowTile[2][col];
		MM_NativeCharGrid_Expect(test, MM_NativeCharGrid_HeadStartFor(g, meta[tile].posX) == retailTransition[tile].headStart,
		                         "head start %d at x 0x%x, the retail tile %d there has %d", MM_NativeCharGrid_HeadStartFor(g, meta[tile].posX),
		                         meta[tile].posX, tile, retailTransition[tile].headStart);
	}

	// The arrow box at the largest half height the scale allows.
	MM_NativeCharGrid_Expect(test,
	                         ((MM_NATIVE_GRID_ARROW_X - MM_NATIVE_GRID_ARROW_HALF_MAX - 1) >= MM_NATIVE_GRID_ARROW_BOX_LEFT) &&
	                             ((MM_NATIVE_GRID_ARROW_X + MM_NATIVE_GRID_ARROW_HALF_MAX + 1) <= MM_NATIVE_GRID_ARROW_BOX_RIGHT) &&
	                             (((2 * MM_NATIVE_GRID_ARROW_HALF_MAX) + 1) <= 16),
	                         "arrow box x %d..%d not inside %d..%d", MM_NATIVE_GRID_ARROW_X - MM_NATIVE_GRID_ARROW_HALF_MAX - 1,
	                         MM_NATIVE_GRID_ARROW_X + MM_NATIVE_GRID_ARROW_HALF_MAX + 1, MM_NATIVE_GRID_ARROW_BOX_LEFT, MM_NATIVE_GRID_ARROW_BOX_RIGHT);
}

// Rows, places and boxes for one entry count.
internal void MM_NativeCharGrid_TestLayout(struct MM_NativeGridTest *test, const struct MM_NativeGridGeometry *g, int entries)
{
	const struct CharacterSelectMeta *meta = D230.characterSelectMeta1P2P;
	const SVec2 *window = &D230.characterSelectWindowPos[0];
	const int windowW = D230.characterSelectLayout.windowW[0];
	const int windowH = D230.characterSelectLayout.windowH[0];
	const int tiles = MM_CHARACTER_SELECT_ICON_COUNT + entries;
	const int rows = MM_NativeCharGrid_RowsFor(g, entries);
	const int expectRows = (entries <= 3) ? 3 : (3 + (((entries - 3) + 5) / 6));
	int seen[MM_NATIVE_GRID_TILES_MAX];
	int tileX[MM_NATIVE_GRID_TILES_MAX];
	int tileY[MM_NATIVE_GRID_TILES_MAX];
	int tileRow[MM_NATIVE_GRID_TILES_MAX];
	int cells = 0;
	int row;
	int col;
	int tile;
	int other;
	int scroll;

	memset(seen, 0, sizeof(seen));

	MM_NativeCharGrid_Expect(test, rows == expectRows, "%d rows, expected %d", rows, expectRows);

	// Every cell holds the tile the rule puts there (size and tile from the
	// rule written out above, not from the layout functions), every tile one
	// cell.
	for (row = 0; row < rows; row++)
	{
		const int size = MM_NativeCharGrid_RowSizeFor(g, entries, row);
		const int expectSize = MM_NativeCharGrid_TestRowSize(entries, row);

		MM_NativeCharGrid_Expect(test, size == expectSize, "row %d has %d tiles, expected %d", row, size, expectSize);

		for (col = 0; col < size; col++)
		{
			const int expectTile = (col < expectSize) ? MM_NativeCharGrid_TestTileAt(row, col) : -1;
			int placeRow = -1;
			int placeCol = -1;
			int placed;

			tile = MM_NativeCharGrid_TileAt(g, entries, row, col);
			MM_NativeCharGrid_Expect(test, tile == expectTile, "row %d col %d holds tile %d, expected %d", row, col, tile, expectTile);

			if ((tile < 0) || (tile >= tiles))
			{
				continue;
			}

			seen[tile]++;
			cells++;

			placed = MM_NativeCharGrid_Place(g, entries, tile, &placeRow, &placeCol);
			MM_NativeCharGrid_Expect(test, placed && (placeRow == row) && (placeCol == col), "tile %d is in row %d col %d, but placed at %d,%d", tile, row,
			                         col, placeRow, placeCol);
		}

		// A row stands centred, columns one pitch apart (x_i = 0xE0 + (2i - (n - 1)) * 0x20).
		for (col = 0; col < size; col++)
		{
			const int x = MM_NativeCharGrid_XFor(g, entries, row, col);
			const int expectX = 0xE0 + (((2 * col) - (expectSize - 1)) * 0x20);

			MM_NativeCharGrid_Expect(test, x == expectX, "row %d col %d at x 0x%x, expected 0x%x", row, col, x, expectX);
		}
	}

	MM_NativeCharGrid_Expect(test, cells == tiles, "%d cells for %d tiles", cells, tiles);

	for (tile = 0; tile < tiles; tile++)
	{
		MM_NativeCharGrid_Expect(test, seen[tile] == 1, "tile %d is in %d cells", tile, seen[tile]);
	}

	// Places.
	for (tile = 0; tile < tiles; tile++)
	{
		if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
		{
			MM_NativeCharGrid_Expect(test, 0, "tile %d has no place", tile);
			tileX[tile] = 0;
			tileY[tile] = 0;
			tileRow[tile] = -1;
			continue;
		}

		tileX[tile] = MM_NativeCharGrid_XFor(g, entries, row, col);
		tileY[tile] = MM_NativeCharGrid_YFor(g, row);
		tileRow[tile] = row;

		MM_NativeCharGrid_Expect(test,
		                         (tileX[tile] >= MM_NATIVE_GRID_BAND_LEFT) && ((tileX[tile] + MM_CHARACTER_SELECT_ICON_RECT_W) <= MM_NATIVE_GRID_BAND_RIGHT),
		                         "tile %d at x %d..%d leaves the centred band %d..%d", tile, tileX[tile], tileX[tile] + MM_CHARACTER_SELECT_ICON_RECT_W,
		                         MM_NATIVE_GRID_BAND_LEFT, MM_NATIVE_GRID_BAND_RIGHT);

		MM_NativeCharGrid_Expect(test, tileY[tile] == (0x60 + (row * 0x27)), "tile %d in row %d at y 0x%x", tile, row, tileY[tile]);

		// The tile column ends before the arrow box.
		MM_NativeCharGrid_Expect(test, (tileX[tile] + MM_CHARACTER_SELECT_ICON_RECT_W) < MM_NATIVE_GRID_ARROW_BOX_LEFT,
		                         "tile %d reaches x %d, into the arrow box", tile, tileX[tile] + MM_CHARACTER_SELECT_ICON_RECT_W);

		// Rows 0 and 1 are the retail places, row 2 too without custom tiles.
		if ((tile < MM_CHARACTER_SELECT_ICON_COUNT) && ((row < (g->rowCount - 1)) || (entries == 0)))
		{
			MM_NativeCharGrid_Expect(test, (tileX[tile] == meta[tile].posX) && (tileY[tile] == meta[tile].posY),
			                         "tile %d at 0x%x,0x%x, retail 0x%x,0x%x", tile, tileX[tile], tileY[tile], meta[tile].posX, meta[tile].posY);
		}

		// A custom tile starts within the frames of the retail ones.
		if (tile >= MM_CHARACTER_SELECT_ICON_COUNT)
		{
			const int headStart = MM_NativeCharGrid_HeadStartFor(g, tileX[tile]);

			MM_NativeCharGrid_Expect(test, (headStart >= 0) && (headStart <= 5) && (headStart == (5 - ((tileX[tile] - 0x40) / 0x40))),
			                         "tile %d at x 0x%x has head start %d", tile, tileX[tile], headStart);
		}
	}

	// Row 2: Joe, Penta, Fake Crash in their order, entry 0 right of Fake Crash.
	MM_NativeCharGrid_Expect(test,
	                         (MM_NativeCharGrid_TileAt(g, entries, 2, 0) == 12) && (MM_NativeCharGrid_TileAt(g, entries, 2, 1) == 13) &&
	                             (MM_NativeCharGrid_TileAt(g, entries, 2, 2) == 14),
	                         "row 2 begins with %d, %d, %d", MM_NativeCharGrid_TileAt(g, entries, 2, 0), MM_NativeCharGrid_TileAt(g, entries, 2, 1),
	                         MM_NativeCharGrid_TileAt(g, entries, 2, 2));

	if (entries >= 1)
	{
		MM_NativeCharGrid_Expect(test, (tileRow[15] == tileRow[14]) && ((tileX[15] - tileX[14]) == g->colPitch),
		                         "entry 0 at row %d x 0x%x, Fake Crash at row %d x 0x%x", tileRow[15], tileX[15], tileRow[14], tileX[14]);
	}

	// No two tiles overlap.
	for (tile = 0; tile < tiles; tile++)
	{
		for (other = tile + 1; other < tiles; other++)
		{
			MM_NativeCharGrid_Expect(test,
			                         !MM_NativeCharGrid_BoxesOverlap(tileX[tile], tileY[tile], MM_CHARACTER_SELECT_ICON_RECT_W, MM_CHARACTER_SELECT_ICON_RECT_H,
			                                                         tileX[other], tileY[other], MM_CHARACTER_SELECT_ICON_RECT_W,
			                                                         MM_CHARACTER_SELECT_ICON_RECT_H),
			                         "tiles %d and %d overlap", tile, other);
		}
	}

	// Drawn at every scroll: the visible rows lie in the 512 x 216 canvas
	// and clear of the preview window.
	for (scroll = 0; scroll <= MM_NativeCharGrid_ScrollMax(g, entries); scroll++)
	{
		for (tile = 0; tile < tiles; tile++)
		{
			int drawnY;

			if ((tileRow[tile] < scroll) || (tileRow[tile] > (scroll + MM_NativeCharGrid_VisibleRows(g) - 1)))
			{
				continue;
			}

			drawnY = MM_NativeCharGrid_YFor(g, tileRow[tile] - scroll);

			MM_NativeCharGrid_Expect(test,
			                         (tileX[tile] >= 0) && ((tileX[tile] + MM_CHARACTER_SELECT_ICON_RECT_W) <= MM_CHARACTER_SELECT_SCREEN_W) && (drawnY >= 0) &&
			                             ((drawnY + MM_CHARACTER_SELECT_ICON_RECT_H) <= MM_CHARACTER_SELECT_SCREEN_H),
			                         "scroll %d: tile %d drawn at %d,%d leaves the canvas", scroll, tile, tileX[tile], drawnY);

			MM_NativeCharGrid_Expect(test,
			                         !MM_NativeCharGrid_BoxesOverlap(tileX[tile], drawnY, MM_CHARACTER_SELECT_ICON_RECT_W, MM_CHARACTER_SELECT_ICON_RECT_H, window->x,
			                                                         window->y, windowW, windowH),
			                         "scroll %d: tile %d drawn at %d,%d covers the preview window", scroll, tile, tileX[tile], drawnY);
		}
	}
}

// The d-pad from every tile in every direction, and the reach from and back to
// tile 0.
//
// The grid neighbour against the rule written out (TestGridNext). A retail
// tile against the retail press (RetailStep), all 60 pairs, with exactly two
// exceptions: Fake Crash right goes to entry 0 once there is one, and from four
// entries on Joe, Penta and Fake Crash go down into row 3 - retail dead ends,
// where the grid now has a row. A custom tile goes to its grid neighbour; at a
// dead end it takes the first detour through a fallback direction that can
// move (two grid steps) and stays only when none can. Every press changes the
// row by one at most: that is what the scroll relies on.
internal void MM_NativeCharGrid_TestNavigation(struct MM_NativeGridTest *test, const struct MM_NativeGridGeometry *g, int entries)
{
	const int tiles = MM_CHARACTER_SELECT_ICON_COUNT + entries;
	const int fakeCrash = s_gridTestRetailRows[2][2];
	const int expectChanged = ((entries >= 1) ? 1 : 0) + ((entries >= 4) ? 3 : 0);
	int reached[MM_NATIVE_GRID_TILES_MAX];
	int queue[MM_NATIVE_GRID_TILES_MAX];
	int queueHead = 0;
	int queueTail = 0;
	int reachedCount = 0;
	int changed = 0;
	int grown;
	int tile;
	int direction;

	for (tile = 0; tile < tiles; tile++)
	{
		int row;
		int col;

		if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
		{
			continue;
		}

		for (direction = 0; direction < CHARACTER_SELECT_DIRECTION_COUNT; direction++)
		{
			const char *name = s_gridDirectionNames[direction];
			const int next = MM_NativeCharGrid_NextFor(g, entries, 0, direction, tile);
			const int gridNext = MM_NativeCharGrid_TestGridNext(g, entries, direction, tile);
			int nextRow = -1;
			int nextCol = -1;

			MM_NativeCharGrid_Expect(test, MM_NativeCharGrid_GridNext(g, entries, direction, tile) == gridNext, "tile %d %s: grid neighbour %d, expected %d",
			                         tile, name, MM_NativeCharGrid_GridNext(g, entries, direction, tile), gridNext);

			MM_NativeCharGrid_Expect(test, (next >= 0) && (next < tiles) && MM_NativeCharGrid_Place(g, entries, next, &nextRow, &nextCol),
			                         "tile %d %s gives %d", tile, name, next);

			if ((next < 0) || (next >= tiles) || (nextRow < 0))
			{
				continue;
			}

			MM_NativeCharGrid_Expect(test, abs(nextRow - row) <= 1, "tile %d %s goes from row %d to row %d", tile, name, row, nextRow);

			if (tile < MM_CHARACTER_SELECT_ICON_COUNT)
			{
				const int retail = MM_NativeCharGrid_RetailStep(0, direction, tile);

				changed += (next != retail) ? 1 : 0;

				if ((tile == fakeCrash) && (direction == CHARACTER_SELECT_DIR_RIGHT) && (entries >= 1))
				{
					MM_NativeCharGrid_Expect(test, next == MM_CHARACTER_SELECT_ICON_COUNT, "Fake Crash right gives %d, expected entry 0", next);
				}
				else if ((row == (g->rowCount - 1)) && (direction == CHARACTER_SELECT_DIR_DOWN) && (entries >= 4))
				{
					MM_NativeCharGrid_Expect(test, (retail == tile) && (next == gridNext) && (nextRow == (row + 1)),
					                         "tile %d down gives %d in row %d, expected the grid neighbour %d in row %d (retail %d)", tile, next, nextRow,
					                         gridNext, row + 1, retail);
				}
				else
				{
					MM_NativeCharGrid_Expect(test, next == retail, "tile %d %s gives %d, the retail press %d", tile, name, next, retail);
				}
			}
			else if (gridNext != tile)
			{
				MM_NativeCharGrid_Expect(test, next == gridNext, "custom tile %d %s gives %d, the grid neighbour %d", tile, name, next, gridNext);
			}
			else
			{
				const int middle1 = MM_NativeCharGrid_TestGridNext(g, entries, D230.characterSelectFallbackDirection1[direction], tile);
				const int middle2 = MM_NativeCharGrid_TestGridNext(g, entries, D230.characterSelectFallbackDirection2[direction], tile);
				const int viaFallback1 = MM_NativeCharGrid_TestGridNext(g, entries, direction, middle1);
				const int viaFallback2 = MM_NativeCharGrid_TestGridNext(g, entries, direction, middle2);
				const int canFallback1 = (viaFallback1 != tile) && (middle1 != tile) && (middle1 != viaFallback1);
				const int canFallback2 = (viaFallback2 != tile) && (middle2 != tile) && (middle2 != viaFallback2);
				const int expect = canFallback1 ? viaFallback1 : (canFallback2 ? viaFallback2 : tile);

				// A dead end. The press itself does not move here, so of the four
				// detours only the two through a fallback direction can, the first
				// one first; the tile stays only when neither can.
				MM_NativeCharGrid_Expect(test, next == expect, "custom tile %d %s at a dead end gives %d, expected %d (detours %d, %d)", tile, name, next,
				                         expect, viaFallback1, viaFallback2);
			}
		}
	}

	MM_NativeCharGrid_Expect(test, changed == expectChanged, "%d retail presses changed, expected %d", changed, expectChanged);

	// Every tile can be reached from tile 0.
	memset(reached, 0, sizeof(reached));
	reached[0] = 1;
	reachedCount = 1;
	queue[queueTail++] = 0;

	while (queueHead < queueTail)
	{
		const int from = queue[queueHead++];

		for (direction = 0; direction < CHARACTER_SELECT_DIRECTION_COUNT; direction++)
		{
			const int next = MM_NativeCharGrid_NextFor(g, entries, 0, direction, from);

			if ((next >= 0) && (next < tiles) && !reached[next])
			{
				reached[next] = 1;
				reachedCount++;
				queue[queueTail++] = next;
			}
		}
	}

	MM_NativeCharGrid_Expect(test, reachedCount == tiles, "%d of %d tiles reachable from tile 0", reachedCount, tiles);

	// And tile 0 from every tile: grow the set of tiles with a way back.
	memset(reached, 0, sizeof(reached));
	reached[0] = 1;
	reachedCount = 1;

	do
	{
		grown = 0;

		for (tile = 0; tile < tiles; tile++)
		{
			if (reached[tile])
			{
				continue;
			}

			for (direction = 0; direction < CHARACTER_SELECT_DIRECTION_COUNT; direction++)
			{
				const int next = MM_NativeCharGrid_NextFor(g, entries, 0, direction, tile);

				if ((next >= 0) && (next < tiles) && reached[next])
				{
					reached[tile] = 1;
					reachedCount++;
					grown = 1;
					break;
				}
			}
		}
	} while (grown);

	MM_NativeCharGrid_Expect(test, reachedCount == tiles, "%d of %d tiles lead back to tile 0", reachedCount, tiles);
}

// One press on a walk: the scroll follows, the cursor row and the tile before
// the press stay visible, the scroll moves by one at most, and the arrows say
// whether rows lie above and below.
internal void MM_NativeCharGrid_TestStep(struct MM_NativeGridTest *test, const struct MM_NativeGridGeometry *g, int entries, int direction, int *tile,
                                         int *scroll)
{
	const int tiles = MM_CHARACTER_SELECT_ICON_COUNT + entries;
	const int visible = MM_NativeCharGrid_VisibleRows(g);
	const int next = MM_NativeCharGrid_NextFor(g, entries, 0, direction, *tile);
	int beforeRow = -1;
	int row = -1;
	int col;
	int nextScroll;
	int arrows;
	int above = 0;
	int below = 0;
	int t;

	if (!MM_NativeCharGrid_Place(g, entries, *tile, &beforeRow, &col) || !MM_NativeCharGrid_Place(g, entries, next, &row, &col))
	{
		MM_NativeCharGrid_Expect(test, 0, "walk lost its tile at %d %s", *tile, s_gridDirectionNames[direction]);
		return;
	}

	nextScroll = MM_NativeCharGrid_ScrollFollow(g, entries, *scroll, row);

	MM_NativeCharGrid_Expect(test, (row >= nextScroll) && (row <= (nextScroll + visible - 1)), "walk: cursor row %d hidden at scroll %d", row, nextScroll);
	MM_NativeCharGrid_Expect(test, (beforeRow >= nextScroll) && (beforeRow <= (nextScroll + visible - 1)), "walk: row %d before the press hidden at scroll %d",
	                         beforeRow, nextScroll);
	MM_NativeCharGrid_Expect(test, (nextScroll >= 0) && (nextScroll <= MM_NativeCharGrid_ScrollMax(g, entries)), "walk: scroll %d outside 0..%d", nextScroll,
	                         MM_NativeCharGrid_ScrollMax(g, entries));
	MM_NativeCharGrid_Expect(test, abs(nextScroll - *scroll) <= 1, "walk: scroll jumps from %d to %d", *scroll, nextScroll);

	for (t = 0; t < tiles; t++)
	{
		int tRow;
		int tCol;

		if (MM_NativeCharGrid_Place(g, entries, t, &tRow, &tCol))
		{
			above |= (tRow < nextScroll);
			below |= (tRow > (nextScroll + visible - 1));
		}
	}

	arrows = MM_NativeCharGrid_ArrowsFor(g, entries, nextScroll);
	MM_NativeCharGrid_Expect(test, (((arrows & MM_NATIVE_GRID_ARROW_UP) != 0) == above) && (((arrows & MM_NATIVE_GRID_ARROW_DOWN) != 0) == below),
	                         "walk: arrows %s at scroll %d, rows above %d below %d", s_gridArrowNames[arrows], nextScroll, above, below);

	*tile = next;
	*scroll = nextScroll;
}

// Entering on every tile, straight walks down and up from every tile of row
// 0, and random walks.
internal void MM_NativeCharGrid_TestScroll(struct MM_NativeGridTest *test, const struct MM_NativeGridGeometry *g, int entries)
{
	const int tiles = MM_CHARACTER_SELECT_ICON_COUNT + entries;
	const int rows = MM_NativeCharGrid_RowsFor(g, entries);
	int tile;
	int row;
	int col;
	int scroll;
	int k;
	int step;

	for (tile = 0; tile < tiles; tile++)
	{
		if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
		{
			continue;
		}

		scroll = MM_NativeCharGrid_ScrollEnter(g, entries, row);
		MM_NativeCharGrid_Expect(test, scroll == ((row > 2) ? (row - 2) : 0), "entering on tile %d in row %d gives scroll %d", tile, row, scroll);
	}

	for (col = 0; col < g->rowSize[0]; col++)
	{
		tile = g->rowTile[0][col];
		scroll = 0;

		for (k = 0; k < rows; k++)
		{
			MM_NativeCharGrid_TestStep(test, g, entries, CHARACTER_SELECT_DIR_DOWN, &tile, &scroll);
		}

		MM_NativeCharGrid_Expect(test, scroll == MM_NativeCharGrid_ScrollMax(g, entries), "down from row 0 ends at scroll %d, not %d", scroll,
		                         MM_NativeCharGrid_ScrollMax(g, entries));

		for (k = 0; k < rows; k++)
		{
			MM_NativeCharGrid_TestStep(test, g, entries, CHARACTER_SELECT_DIR_UP, &tile, &scroll);
		}

		MM_NativeCharGrid_Expect(test, scroll == 0, "up again ends at scroll %d", scroll);
	}

	for (k = 0; k < 3; k++)
	{
		tile = (k == 0) ? 0 : ((k == 1) ? (tiles - 1) : (int)((test->random >> 8) % (u32)tiles));

		if (!MM_NativeCharGrid_Place(g, entries, tile, &row, &col))
		{
			continue;
		}

		scroll = MM_NativeCharGrid_ScrollEnter(g, entries, row);

		for (step = 0; step < MM_NATIVE_GRID_TEST_WALK_STEPS; step++)
		{
			test->random = (test->random * 1103515245u) + 12345u;
			MM_NativeCharGrid_TestStep(test, g, entries, (int)((test->random >> 16) & 3u), &tile, &scroll);
		}
	}
}

// THE PORTRAIT SLOTS (platform/native_chars.h, NATIVE_CHAR_PORTRAIT_SLOTS),
// the rule written out once more: slot k on page p = k / 5, column s = k % 5,
// texels x 256 + 64p + 11s, y 266, 11 x 26; CLUT x 256 + 16 (k % 16),
// y 292 + k / 16, 16 x 1; all inside the strip x 256..511, y 266..295, no two
// rectangles overlapping; the layout made from a 44 x 26 template points at
// exactly these texels and this CLUT, 4 bit, y base 256, abr kept.
#define MM_NATIVE_GRID_TEST_STRIP_X0 256
#define MM_NATIVE_GRID_TEST_STRIP_X1 512
#define MM_NATIVE_GRID_TEST_STRIP_Y0 266
#define MM_NATIVE_GRID_TEST_STRIP_Y1 296

internal int MM_NativeCharGrid_TestInStrip(int x, int y, int w, int h)
{
	return (w > 0) && (h > 0) && (x >= MM_NATIVE_GRID_TEST_STRIP_X0) && ((x + w) <= MM_NATIVE_GRID_TEST_STRIP_X1) && (y >= MM_NATIVE_GRID_TEST_STRIP_Y0) &&
	       ((y + h) <= MM_NATIVE_GRID_TEST_STRIP_Y1);
}

internal void MM_NativeCharGrid_TestPortraits(struct MM_NativeGridTest *test)
{
	struct NativeCharPortraitSlot slots[NATIVE_CHAR_PORTRAIT_SLOTS];
	struct NativeCharPortraitSlot outside;
	struct TextureLayout templateLayout;
	struct TextureLayout layout;
	int k;
	int j;

	for (k = 0; k < NATIVE_CHAR_PORTRAIT_SLOTS; k++)
	{
		struct NativeCharPortraitSlot *s = &slots[k];
		const int page = k / 5;
		const int column = k % 5;
		int pageX;

		memset(s, 0, sizeof(*s));
		MM_NativeCharGrid_Expect(test, NativeChar_PortraitSlot(k, s) == 1, "portrait slot %d is not given", k);

		MM_NativeCharGrid_Expect(test, (s->texelX == (256 + (64 * page) + (11 * column))) && (s->texelY == 266) && (s->texelW == 11) && (s->texelH == 26),
		                         "portrait slot %d texels at %d,%d %dx%d", k, s->texelX, s->texelY, s->texelW, s->texelH);
		MM_NativeCharGrid_Expect(test, (s->clutX == (256 + (16 * (k % 16)))) && (s->clutY == (292 + (k / 16))) && (s->clutW == 16),
		                         "portrait slot %d CLUT at %d,%d width %d", k, s->clutX, s->clutY, s->clutW);
		MM_NativeCharGrid_Expect(test, MM_NativeCharGrid_TestInStrip(s->texelX, s->texelY, s->texelW, s->texelH),
		                         "portrait slot %d texels leave the strip", k);
		MM_NativeCharGrid_Expect(test, MM_NativeCharGrid_TestInStrip(s->clutX, s->clutY, s->clutW, 1), "portrait slot %d CLUT leaves the strip", k);
		MM_NativeCharGrid_Expect(test, (s->clutX % 16) == 0, "portrait slot %d CLUT x %d is not a multiple of 16", k, s->clutX);

		// The page: a 4-bit page is 64 halfwords wide; the slot lies in it
		// whole, u0 counts 4 texels per halfword from the page edge.
		pageX = (s->pageBits & 0xf) * 64;
		MM_NativeCharGrid_Expect(test, ((s->pageBits & 0xf) == (s->texelX / 64)) && ((s->pageBits & 0x10) != 0) && ((s->pageBits & ~0x1f) == 0),
		                         "portrait slot %d page bits 0x%02x", k, s->pageBits);
		MM_NativeCharGrid_Expect(test, (s->texelX >= pageX) && ((s->texelX + s->texelW) <= (pageX + 64)), "portrait slot %d crosses its page", k);
		MM_NativeCharGrid_Expect(test, (s->u0 == ((s->texelX - pageX) * 4)) && (s->u0 == (44 * column)) && ((s->u0 + 44) <= 255),
		                         "portrait slot %d u0 %d", k, s->u0);
		MM_NativeCharGrid_Expect(test, (s->v0 == (s->texelY - 256)) && ((s->v0 + 26) <= 255), "portrait slot %d v0 %d", k, s->v0);
	}

	for (k = 0; k < NATIVE_CHAR_PORTRAIT_SLOTS; k++)
	{
		for (j = 0; j < NATIVE_CHAR_PORTRAIT_SLOTS; j++)
		{
			const struct NativeCharPortraitSlot *a = &slots[k];
			const struct NativeCharPortraitSlot *b = &slots[j];

			MM_NativeCharGrid_Expect(test, !MM_NativeCharGrid_BoxesOverlap(a->texelX, a->texelY, a->texelW, a->texelH, b->clutX, b->clutY, b->clutW, 1),
			                         "portrait slot %d texels overlap the CLUT of slot %d", k, j);

			if (j <= k)
			{
				continue;
			}

			MM_NativeCharGrid_Expect(test, !MM_NativeCharGrid_BoxesOverlap(a->texelX, a->texelY, a->texelW, a->texelH, b->texelX, b->texelY, b->texelW, b->texelH),
			                         "portrait slots %d and %d overlap", k, j);
			MM_NativeCharGrid_Expect(test, !MM_NativeCharGrid_BoxesOverlap(a->clutX, a->clutY, a->clutW, 1, b->clutX, b->clutY, b->clutW, 1),
			                         "portrait CLUTs %d and %d overlap", k, j);
		}
	}

	// No slot beyond the 20: the entries from 20 on keep the template's.
	MM_NativeCharGrid_Expect(test, NativeChar_PortraitSlot(NATIVE_CHAR_PORTRAIT_SLOTS, &outside) == 0, "portrait slot %d is given", NATIVE_CHAR_PORTRAIT_SLOTS);
	MM_NativeCharGrid_Expect(test, NativeChar_PortraitSlot(-1, &outside) == 0, "portrait slot -1 is given");

	// The layout from a template of 44 x 26 somewhere else, 8 bit, abr 2, y
	// base 0, bit 11 set: everything that names the place is new.
	memset(&templateLayout, 0, sizeof(templateLayout));
	templateLayout.u0 = 100;
	templateLayout.u2 = 100;
	templateLayout.u1 = 144;
	templateLayout.u3 = 144;
	templateLayout.v0 = 50;
	templateLayout.v1 = 50;
	templateLayout.v2 = 76;
	templateLayout.v3 = 76;
	templateLayout.clut = 0x7abc;
	templateLayout.tpage = (u16)(0x0800 | 0x0080 | 0x0040 | 0x0009);

	for (k = 0; k < NATIVE_CHAR_PORTRAIT_SLOTS; k++)
	{
		const struct NativeCharPortraitSlot *s = &slots[k];
		const int ok = NativeChar_PortraitLayout(k, &templateLayout, &layout);
		const int clutX = (layout.clut & 0x3f) << 4;
		const int clutY = layout.clut >> 6;
		const int pageX = (layout.tpage & 0xf) * 64;
		const int pageY = ((layout.tpage & 0x10) != 0) ? 256 : 0;

		MM_NativeCharGrid_Expect(test, ok, "portrait layout %d refused a 44 x 26 template", k);
		MM_NativeCharGrid_Expect(test, (clutX == s->clutX) && (clutY == s->clutY), "portrait layout %d CLUT 0x%04x is %d,%d", k, layout.clut, clutX, clutY);
		MM_NativeCharGrid_Expect(test, (((layout.tpage >> 7) & 3) == 0) && ((layout.tpage & 0x60) == 0x40) && ((layout.tpage & 0x800) == 0),
		                         "portrait layout %d tpage 0x%04x: not 4 bit, abr lost or bit 11 kept", k, layout.tpage);
		MM_NativeCharGrid_Expect(test, ((pageX + (layout.u0 / 4)) == s->texelX) && ((pageY + layout.v0) == s->texelY),
		                         "portrait layout %d samples from %d,%d", k, pageX + (layout.u0 / 4), pageY + layout.v0);
		MM_NativeCharGrid_Expect(test,
		                         (layout.u2 == layout.u0) && (layout.u1 == (layout.u0 + 44)) && (layout.u3 == layout.u1) && (layout.v1 == layout.v0) &&
		                             (layout.v2 == (layout.v0 + 26)) && (layout.v3 == layout.v2),
		                         "portrait layout %d corners", k);
		MM_NativeCharGrid_Expect(test, ((pageX + ((layout.u1 + 3) / 4)) <= (s->texelX + s->texelW)) && ((pageY + layout.v2) <= (s->texelY + s->texelH)),
		                         "portrait layout %d samples beyond its slot", k);
	}

	// A template larger than a slot is not given a slot layout.
	templateLayout.u1 = 145;
	MM_NativeCharGrid_Expect(test, NativeChar_PortraitLayout(0, &templateLayout, &layout) == 0, "portrait layout took a 45 wide template");
	templateLayout.u1 = 144;
	templateLayout.v2 = 77;
	MM_NativeCharGrid_Expect(test, NativeChar_PortraitLayout(0, &templateLayout, &layout) == 0, "portrait layout took a 27 high template");
	templateLayout.v2 = 76;
	MM_NativeCharGrid_Expect(test, NativeChar_PortraitLayout(NATIVE_CHAR_PORTRAIT_SLOTS, &templateLayout, &layout) == 0, "portrait layout given for slot %d",
	                         NATIVE_CHAR_PORTRAIT_SLOTS);

	// The race icon of a driver pack: 43 x 25, 4 bit, page x 256 y 0, abr 3
	// (tpage 0x64). Into slot 0 it keeps abr and takes the strip's page.
	memset(&templateLayout, 0, sizeof(templateLayout));
	templateLayout.u1 = 43;
	templateLayout.v2 = 25;
	templateLayout.tpage = 0x64;
	MM_NativeCharGrid_Expect(test,
	                         NativeChar_PortraitLayout(0, &templateLayout, &layout) && (layout.tpage == 0x74) && (layout.u1 == 43) && (layout.v2 == 10 + 25),
	                         "race portrait into slot 0: tpage 0x%04x, u1 %d, v2 %d", (unsigned)layout.tpage, (int)layout.u1, (int)layout.v2);

	// No seat is bound here: the race asks for every seat and gets the
	// template's icon itself, the pointer unchanged.
	{
		struct Icon templateIcon;

		memset(&templateIcon, 0, sizeof(templateIcon));
		for (k = -1; k <= 8; k++)
		{
			MM_NativeCharGrid_Expect(test, NativeChar_SeatPortrait(k, &templateIcon) == &templateIcon, "seat %d without a binding: not the template's icon", k);
		}
	}
}

int MM_NativeCharGrid_SelfTest(void)
{
	struct MM_NativeGridGeometry geometry;
	struct MM_NativeGridTest test;
	int entries;

	memset(&test, 0, sizeof(test));
	test.random = 20261001u;

	MM_NativeCharGrid_Expect(&test, MM_NativeCharGrid_Derive(&geometry), "D230.characterSelectMeta1P2P forms no rows of one pitch");

	if (test.failures == 0)
	{
		MM_NativeCharGrid_TestTable(&test, &geometry);

		// The retail press the navigation is checked against.
		MM_NativeCharGrid_TestRetailStep(&test);
	}

	// The layout functions need three rows; with others the numbers above
	// have failed already.
	if (test.failures == 0)
	{
		for (entries = 0; entries <= NATIVE_CHAR_ROSTER_MAX; entries++)
		{
			test.entries = entries;
			MM_NativeCharGrid_TestLayout(&test, &geometry, entries);
			MM_NativeCharGrid_TestNavigation(&test, &geometry, entries);
			MM_NativeCharGrid_TestScroll(&test, &geometry, entries);
		}
	}

	// The arrows need no table, nor do the portrait slots.
	test.entries = 0;
	MM_NativeCharGrid_TestArrows(&test);
	MM_NativeCharGrid_TestPortraits(&test);

	// The mask of the CHRI flags and of a bound seat (platform/native_chars.c).
	NativeChar_MaskSelfTest(&test.checks, &test.failures);

	if (test.failures != 0)
	{
		printf("char grid selftest FAILED: %d of %d checks\n", test.failures, test.checks);
		return 1;
	}

	printf("char grid selftest passed: %d checks, 0 failures\n", test.checks);
	return 0;
}
