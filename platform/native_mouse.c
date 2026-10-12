#include <platform/native_controls.h>

// main.c undefines RECT (the Windows one) before the platform files; the menu
// code speaks the PSX RECT with x, y, w, h. The header is the same either way.
#undef RECT
#define RECT RECT16
#include <platform/native_mouse.h>

// THE MOUSE IN MENUS - see include/platform/native_mouse.h.
//
// Three jobs, in this order of importance:
//
//   1. STATE. The mouse is "active" from the first real movement of the pointer
//      until the next key or pad press. While it is not active every answer is
//      0 and the system cursor is hidden, so a player on a pad never sees a
//      pointer and a menu never sees a click it should not.
//   2. MAPPING. The pointer is a window position; a menu thinks in the 512 wide
//      space its elements were authored in. NativeMouse_MenuPoint walks the
//      way back that a drawn element took: out of the letterbox or pillarbox,
//      out of the canvas scaling, and out of the widescreen shift that this
//      one element got.
//   3. THE CURSOR. The system cursor, with a shape of our own drawn in code.
//      It is not drawn into the picture, so a screenshot or a capture of the
//      game never contains it.
//
// MEASURING MODE (--deterministic) is sealed in the event loop of
// native_platform.c: mouse events are counted there and never reach this file.
// Nothing here may change a rendered frame: the cursor is hidden once and not
// touched again, and every query answers 0. The one exception is the dev-only
// --dev-mouse-at, which places the pointer for a hover screenshot.

extern SDL_Window *g_window;
extern int g_cfg_deterministic; // --deterministic, native_platform.c

// The rectangle the finished picture occupies in the window, in window pixels
// (letterbox or pillarbox already taken off). The one getter this package adds
// to the renderer; the value is s_presentViewport in native_renderer.c.
void NativeRenderer_GetPresentViewport(int *x, int *y, int *w, int *h);

// Mouse button slots of the Take functions: 1 left, 2 right (the header).
#define NATIVE_MOUSE_BUTTONS 2

// The size of the pointer image. The hotspot is the tip, at (0,0).
#define NATIVE_MOUSE_CURSOR_SIZE 32

// How a menu element got its place on the canvas. The decision is made by the
// UI layer per element, in this order (native_gpu.c, the "decided ONCE, per
// element" loop, ~3600-3670): spans the canvas -> FULL; a declaration row claims
// it -> DECL; the screen has a policy -> POLICY; in a menu -> EDGE; otherwise
// the thirds rule -> THIRDS. NativeMouse_MenuPoint repeats that order.
enum NativeMouseRule
{
	NATIVE_MOUSE_RULE_FULL = 0,
	NATIVE_MOUSE_RULE_DECL,
	NATIVE_MOUSE_RULE_POLICY,
	NATIVE_MOUSE_RULE_EDGE,
	NATIVE_MOUSE_RULE_THIRDS
};

// Everything the pure arithmetic needs to know about the window and the picture.
struct NativeMouseGeom
{
	int vpX; // the picture inside the window, in window pixels
	int vpY;
	int vpW;
	int vpH;
	int canvasW; // 512, 682 or 918 columns
	int canvasH; // 216 rows
};

global_variable int s_mouseActive = 0;       // moved since the last key or pad press
global_variable int s_mouseHavePos = 0;      // a first position is known (it never activates by itself)
global_variable float s_mousePx = 0.0f;      // last pointer position, window PIXELS
global_variable float s_mousePy = 0.0f;
global_variable int s_mouseMoved = 0;        // moved since TakeMoved
global_variable int s_mouseClicks[NATIVE_MOUSE_BUTTONS];
global_variable float s_mouseWheel = 0.0f;   // notches, fractions kept for smooth wheels
global_variable int s_mouseWasActive = 0;    // what the last Update saw
global_variable int s_mouseCursorShown = -1; // -1 = never set, else 0/1
global_variable int s_mouseDetHidden = 0;
global_variable SDL_Cursor *s_mouseCursor = NULL;
global_variable int s_mouseCursorFailed = 0;
global_variable int s_mouseFocusOverride = -1; // selftest only: -1 ask the window, else 0/1

// --dev-mouse-at
global_variable int s_mouseDevSet = 0;
global_variable int s_mouseDevX = 0;
global_variable int s_mouseDevY = 0;
global_variable int s_mouseDevPlaced = 0;

//----------------------------------------------------------------------------------------
// THE STATE

internal int NativeMouse_WindowHasFocus(void)
{
	if (s_mouseFocusOverride >= 0)
	{
		return s_mouseFocusOverride;
	}

	if (g_window == NULL)
	{
		return 0;
	}

	return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

// May the mouse be used at all right now? (Whether it has moved is a separate
// question.) Never in measuring mode, never without focus, never in a race.
internal int NativeMouse_Allowed(void)
{
	if (g_cfg_deterministic)
	{
		return 0;
	}

	return NativeMouse_WindowHasFocus() && !NativeControlsPage_RaceContext();
}

internal void NativeMouse_DropPending(void)
{
	int i;

	for (i = 0; i < NATIVE_MOUSE_BUTTONS; i++)
	{
		s_mouseClicks[i] = 0;
	}

	s_mouseWheel = 0.0f;
	s_mouseMoved = 0;
}

internal void NativeMouse_SetInactive(void)
{
	s_mouseActive = 0;
	NativeMouse_DropPending();
}

void NativeMouse_OnMotion(float windowX, float windowY)
{
	float px = windowX;
	float py = windowY;

	if (g_cfg_deterministic)
	{
		return;
	}

	// SDL reports LOGICAL window coordinates; the picture is placed in PIXELS
	// (NativeRenderer_SyncWindowSize). They differ on a display with a scale
	// factor. Keep the pixel position.
	if (g_window != NULL)
	{
		int logicalW = 0;
		int logicalH = 0;
		int pixelW = 0;
		int pixelH = 0;

		SDL_GetWindowSize(g_window, &logicalW, &logicalH);
		SDL_GetWindowSizeInPixels(g_window, &pixelW, &pixelH);

		if ((logicalW > 0) && (logicalH > 0) && (pixelW > 0) && (pixelH > 0))
		{
			px = windowX * (float)pixelW / (float)logicalW;
			py = windowY * (float)pixelH / (float)logicalH;
		}
	}

	// The first position only says where the pointer is. A motion event that
	// does not move (SDL sends one when the window gains focus or the cursor
	// is warped) is not a movement either.
	if (!s_mouseHavePos)
	{
		s_mouseHavePos = 1;
		s_mousePx = px;
		s_mousePy = py;
		return;
	}

	if ((px == s_mousePx) && (py == s_mousePy))
	{
		return;
	}

	s_mousePx = px;
	s_mousePy = py;

	if (!NativeMouse_Allowed())
	{
		return;
	}

	s_mouseActive = 1;
	s_mouseMoved = 1;
}

void NativeMouse_OnButton(int button, int down)
{
	int slot;

	if (!down || g_cfg_deterministic || !NativeMouse_Allowed())
	{
		return;
	}

	// A click on a hidden pointer wakes the mouse up and is not yet a click
	// on anything - the player has not seen where it is.
	if (!s_mouseActive)
	{
		s_mouseActive = 1;
		return;
	}

	slot = (button == SDL_BUTTON_LEFT) ? 0 : ((button == SDL_BUTTON_RIGHT) ? 1 : -1);

	if (slot >= 0)
	{
		s_mouseClicks[slot] = 1;
	}
}

void NativeMouse_OnWheel(float y)
{
	if (g_cfg_deterministic || !s_mouseActive || !NativeMouse_Allowed())
	{
		return;
	}

	s_mouseWheel += y;
}

void NativeMouse_OnKeyOrPad(void)
{
	NativeMouse_SetInactive();
}

int NativeMouse_Active(void)
{
	// The dev placement stays on in measuring mode: it exists to take one
	// hover screenshot of a deterministic run.
	if (s_mouseDevPlaced && g_cfg_deterministic)
	{
		return !NativeControlsPage_RaceContext();
	}

	return s_mouseActive && NativeMouse_Allowed();
}

int NativeMouse_TakeMoved(void)
{
	const int moved = s_mouseMoved && NativeMouse_Active();

	s_mouseMoved = 0;
	return moved;
}

int NativeMouse_TakeClick(int button)
{
	int taken;

	if ((button < 1) || (button > NATIVE_MOUSE_BUTTONS))
	{
		return 0;
	}

	taken = s_mouseClicks[button - 1] && NativeMouse_Active();
	s_mouseClicks[button - 1] = 0;
	return taken;
}

int NativeMouse_TakeWheel(void)
{
	int notches;

	if (!NativeMouse_Active())
	{
		return 0;
	}

	notches = (int)s_mouseWheel; // truncates towards 0, the rest waits
	s_mouseWheel -= (float)notches;
	return notches;
}

//----------------------------------------------------------------------------------------
// THE POINTER

// A classic arrow, white with a 1 pixel black outline, drawn into a 32x32
// surface: the fill is a polygon (even-odd rule on the pixel centres), the
// outline every empty pixel that touches the fill. The tip is at (0,0).
internal int NativeMouse_InsideArrow(float x, float y)
{
	static const float poly[][2] = {{1.0f, 1.0f}, {1.0f, 25.0f}, {7.0f, 19.5f}, {12.0f, 29.0f}, {16.5f, 27.0f}, {11.5f, 18.0f}, {19.5f, 18.0f}};
	const int count = (int)(sizeof(poly) / sizeof(poly[0]));
	int inside = 0;
	int i;
	int j = count - 1;

	for (i = 0; i < count; i++)
	{
		const float xi = poly[i][0];
		const float yi = poly[i][1];
		const float xj = poly[j][0];
		const float yj = poly[j][1];

		if (((yi > y) != (yj > y)) && (x < ((xj - xi) * (y - yi) / (yj - yi) + xi)))
		{
			inside = !inside;
		}

		j = i;
	}

	return inside;
}

internal void NativeMouse_CreateCursor(void)
{
	static unsigned char fill[NATIVE_MOUSE_CURSOR_SIZE][NATIVE_MOUSE_CURSOR_SIZE];
	SDL_Surface *surface;
	int x;
	int y;

	if ((s_mouseCursor != NULL) || s_mouseCursorFailed)
	{
		return;
	}

	surface = SDL_CreateSurface(NATIVE_MOUSE_CURSOR_SIZE, NATIVE_MOUSE_CURSOR_SIZE, SDL_PIXELFORMAT_ARGB8888);

	if (surface == NULL)
	{
		s_mouseCursorFailed = 1;
		Platform_Log("[CTR Mouse] pointer image failed: %s - the system arrow stays\n", SDL_GetError());
		return;
	}

	for (y = 0; y < NATIVE_MOUSE_CURSOR_SIZE; y++)
	{
		for (x = 0; x < NATIVE_MOUSE_CURSOR_SIZE; x++)
		{
			fill[y][x] = (unsigned char)NativeMouse_InsideArrow((float)x + 0.5f, (float)y + 0.5f);
		}
	}

	for (y = 0; y < NATIVE_MOUSE_CURSOR_SIZE; y++)
	{
		unsigned int *row = (unsigned int *)((unsigned char *)surface->pixels + (size_t)y * (size_t)surface->pitch);

		for (x = 0; x < NATIVE_MOUSE_CURSOR_SIZE; x++)
		{
			unsigned int argb = 0x00000000u;

			if (fill[y][x])
			{
				argb = 0xFFFFFFFFu;
			}
			else
			{
				int dy;
				int dx;

				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						const int nx = x + dx;
						const int ny = y + dy;

						if ((nx >= 0) && (ny >= 0) && (nx < NATIVE_MOUSE_CURSOR_SIZE) && (ny < NATIVE_MOUSE_CURSOR_SIZE) && fill[ny][nx])
						{
							argb = 0xFF000000u;
						}
					}
				}
			}

			row[x] = argb;
		}
	}

	s_mouseCursor = SDL_CreateColorCursor(surface, 0, 0);
	SDL_DestroySurface(surface);

	if (s_mouseCursor == NULL)
	{
		s_mouseCursorFailed = 1;
		Platform_Log("[CTR Mouse] pointer cursor failed: %s - the system arrow stays\n", SDL_GetError());
	}
}

internal void NativeMouse_ApplyCursor(int shown)
{
	if (shown == s_mouseCursorShown)
	{
		return;
	}

	s_mouseCursorShown = shown;

	if (shown)
	{
		NativeMouse_CreateCursor();

		if (s_mouseCursor != NULL)
		{
			SDL_SetCursor(s_mouseCursor);
		}

		SDL_ShowCursor();
	}
	else
	{
		SDL_HideCursor();
	}
}

void NativeMouse_Update(void)
{
	int active;

	// --dev-mouse-at: from the first menu frame on, the pointer is "there".
	if (s_mouseDevSet && !s_mouseDevPlaced && CTR_UI_MenuMode())
	{
		s_mouseDevPlaced = 1;
		s_mouseHavePos = 1;
		s_mousePx = (float)s_mouseDevX;
		s_mousePy = (float)s_mouseDevY;
		s_mouseActive = 1;
		s_mouseMoved = 1;
		Platform_Log("[CTR Mouse] --dev-mouse-at %d,%d: pointer placed, hover active (dev)\n", s_mouseDevX, s_mouseDevY);
	}

	// Measuring mode: hidden once, then never touched - nothing that could
	// reach a rendered frame.
	if (g_cfg_deterministic)
	{
		if (!s_mouseDetHidden && (g_window != NULL))
		{
			s_mouseDetHidden = 1;
			SDL_HideCursor();
		}

		return;
	}

	// Focus lost, or a race started: the mouse falls silent until it moves.
	if (!NativeMouse_Allowed())
	{
		NativeMouse_SetInactive();
	}

	active = NativeMouse_Active();

	if (!active && s_mouseWasActive)
	{
		NativeMouse_DropPending();
	}

	s_mouseWasActive = active;

	if (g_window != NULL)
	{
		NativeMouse_ApplyCursor(active);
	}
}

//----------------------------------------------------------------------------------------
// THE MAPPING (pure part, no global state)

// Window pixel -> canvas column and row. 0 when the pixel is outside the
// picture (in a letterbox or pillarbox bar). The picture is the viewport
// computed in NativeRenderer_UpdatePresentationViewport (native_renderer.c) and
// it shows the whole canvas: columns 0..canvasW-1, rows 0..canvasH-1, in the
// vertex coordinates the UI layer maps into.
internal int NativeMouse_WindowToCanvas(const struct NativeMouseGeom *g, float px, float py, int *cx, int *cy)
{
	float u;
	float v;

	if ((g == NULL) || (g->vpW <= 0) || (g->vpH <= 0) || (g->canvasW <= 0) || (g->canvasH <= 0))
	{
		return 0;
	}

	if ((px < (float)g->vpX) || (py < (float)g->vpY) || (px >= (float)(g->vpX + g->vpW)) || (py >= (float)(g->vpY + g->vpH)))
	{
		return 0;
	}

	u = (px - (float)g->vpX) * (float)g->canvasW / (float)g->vpW;
	v = (py - (float)g->vpY) * (float)g->canvasH / (float)g->vpH;

	*cx = (int)u;
	*cy = (int)v;

	if (*cx >= g->canvasW) { *cx = g->canvasW - 1; }
	if (*cy >= g->canvasH) { *cy = g->canvasH - 1; }

	return 1;
}

// The view the UI layer builds with CTR_UI_BuildSafeArea (native_view.c:696),
// minus the globals: authored width 512, the given canvas, and in a menu the
// composition zone of the 16:9 canvas centred in a wider one.
internal void NativeMouse_BuildView(struct CTR_UIViewParameters *view, int canvasW, int menuMode)
{
	view->virtualWidth = CTR_CANVAS_REFERENCE_WIDTH;
	view->virtualHeight = CTR_CANVAS_HEIGHT;
	view->centreX = CTR_CANVAS_REFERENCE_WIDTH / 2;
	view->canvasWidth = (canvasW < CTR_CANVAS_REFERENCE_WIDTH) ? CTR_CANVAS_REFERENCE_WIDTH : canvasW;
	view->zoneLeft = 0;
	view->zoneWidth = view->canvasWidth;

	if (menuMode)
	{
		const int zoneWidth = CTR_Canvas_Width(CTR_ASPECT_MODE_16_9);

		if ((zoneWidth >= CTR_CANVAS_REFERENCE_WIDTH) && (zoneWidth < view->canvasWidth))
		{
			view->zoneWidth = zoneWidth;
			view->zoneLeft = (view->canvasWidth - zoneWidth) / 2;
		}
	}
}

// The column shift an element got, as the UI layer computes it. box is the
// element frame in the authored 512 space (x0, y0, x1, y1). Returns the
// ADDITION to the authored x; for NATIVE_MOUSE_RULE_FULL the layer stretches
// instead (see NativeMouse_UnmapX) and 0 comes back.
//
// Forward rules mirrored (native_gpu.c, per-group loop at ~3600-3670; the
// anchor itself and the shifts are the CTR_UI_* functions of native_view.c):
//   DECL    anchor of the claiming row; x' = CTR_UI_MapX(x, anchor)
//   POLICY  x' = CTR_UI_MapX(x, anchor) + (TransitShift - AnchorShift)
//   EDGE    anchor = CTR_UI_AnchorForBoxEdge; x' = MapX + CTR_UI_MenuEdgeTransit
//   THIRDS  anchor = CTR_UI_AnchorForBox (outside menus); x' = MapX
internal int NativeMouse_ElementShift(const struct CTR_UIViewParameters *view, int rule, int anchor, const int *box)
{
	switch (rule)
	{
	case NATIVE_MOUSE_RULE_DECL:
		return CTR_UI_MapX(view, 0, anchor);
	case NATIVE_MOUSE_RULE_POLICY:
		return CTR_UI_TransitShift(view, CTR_UI_AnchorShift(view, anchor), box[0], box[2]);
	case NATIVE_MOUSE_RULE_EDGE:
		anchor = CTR_UI_AnchorForBoxEdge(box[0], box[2], view->virtualWidth);
		return CTR_UI_MapX(view, 0, anchor) + CTR_UI_MenuEdgeTransit(view, anchor, box[0], box[2]);
	case NATIVE_MOUSE_RULE_THIRDS:
		anchor = CTR_UI_AnchorForBox(box[0], box[2], view->virtualWidth);
		return CTR_UI_MapX(view, 0, anchor);
	default:
		return 0;
	}
}

// Canvas column -> authored (menu) x for one element: the inverse of the
// forward rule above. The shift depends on the element's box, not on x, so the
// inverse is a subtraction; FULL (and a declaration with FULL_CANVAS) is the
// stretch x * canvasW / 512 and is undone by the division.
internal int NativeMouse_UnmapX(int canvasW, int menuMode, int rule, int anchor, const int *box, int cx)
{
	struct CTR_UIViewParameters view;

	NativeMouse_BuildView(&view, canvasW, menuMode);

	if ((rule == NATIVE_MOUSE_RULE_FULL) || ((rule == NATIVE_MOUSE_RULE_DECL) && (anchor == (int)CTR_UI_ANCHOR_FULL_CANVAS)))
	{
		return (int)(((long long)cx * view.virtualWidth) / view.canvasWidth);
	}

	return cx - NativeMouse_ElementShift(&view, rule, anchor, box);
}

//----------------------------------------------------------------------------------------
// THE MAPPING (the impure part: asks the live frame)

int NativeMouse_MenuPoint(const RECT *element, int *x, int *y)
{
	struct NativeMouseGeom geom;
	int cx = 0;
	int cy = 0;
	int box[4];
	int rule;
	int anchor = (int)CTR_UI_ANCHOR_CENTRE;
	const int menuMode = CTR_UI_MenuMode();

	if ((element == NULL) || (x == NULL) || (y == NULL) || !NativeMouse_Active())
	{
		return 0;
	}

	NativeRenderer_GetPresentViewport(&geom.vpX, &geom.vpY, &geom.vpW, &geom.vpH);
	geom.canvasW = CTR_Canvas_ActiveWidth();
	geom.canvasH = CTR_Canvas_ActiveHeight();

	if (!NativeMouse_WindowToCanvas(&geom, s_mousePx, s_mousePy, &cx, &cy))
	{
		return 0;
	}

	box[0] = element->x;
	box[1] = element->y;
	box[2] = element->x + element->w;
	box[3] = element->y + element->h;

	// The decision order of native_gpu.c ~3595-3670 (the "decided ONCE, per
	// element" loop):
	//  1. an element that spans the canvas is stretched (slot -2);
	//  2. a declaration row (NativeUiDecl_Find) claims it - searched without a
	//     HUD slot, so rows keyed on a slot never match here;
	//  3. in a menu: the screen's policy, else the edge rule;
	//  4. outside menus: the thirds rule.
	if ((box[0] <= NATIVE_UI_TOUCH_GAP) && (box[2] >= (CTR_CANVAS_REFERENCE_WIDTH - NATIVE_UI_TOUCH_GAP)))
	{
		rule = NATIVE_MOUSE_RULE_FULL;
	}
	else
	{
		const int decl = NativeUiDecl_Find(box, -1);

		if (decl >= 0)
		{
			rule = NATIVE_MOUSE_RULE_DECL;
			anchor = NativeUiDecl_At(decl)->anchor;
		}
		else if (menuMode && ((anchor = NativeUiDecl_MenuPolicy(NULL)) >= 0))
		{
			rule = NATIVE_MOUSE_RULE_POLICY;
		}
		else if (menuMode)
		{
			rule = NATIVE_MOUSE_RULE_EDGE;
		}
		else
		{
			rule = NATIVE_MOUSE_RULE_THIRDS;
		}
	}

	*x = NativeMouse_UnmapX(geom.canvasW, menuMode, rule, anchor, box, cx);
	*y = cy; // rows are not shifted: the mapper leaves y alone
	return 1;
}

//----------------------------------------------------------------------------------------
// --dev-mouse-at

// Window pixel position, from the first menu frame on. Returns 0 for a value
// that is not "<x>,<y>" with two numbers from 0 to 16384.
int NativeMouse_SetDevAt(const char *value)
{
	char *end = NULL;
	long px;
	long py;

	if (value == NULL)
	{
		return 0;
	}

	px = strtol(value, &end, 10);

	if ((end == value) || (*end != ','))
	{
		return 0;
	}

	value = end + 1;
	py = strtol(value, &end, 10);

	if ((end == value) || (*end != '\0') || (px < 0) || (px > 16384) || (py < 0) || (py > 16384))
	{
		return 0;
	}

	s_mouseDevSet = 1;
	s_mouseDevX = (int)px;
	s_mouseDevY = (int)py;
	return 1;
}

//----------------------------------------------------------------------------------------
// SELFTEST (--mouse-selftest, ctest mouse_selftest)

#define NATIVE_MOUSE_EXPECT(cond, ...)                                                                                                                      \
	do                                                                                                                                                     \
	{                                                                                                                                                      \
		checks++;                                                                                                                                          \
		if (!(cond))                                                                                                                                       \
		{                                                                                                                                                  \
			printf("mouse selftest: " __VA_ARGS__);                                                                                                        \
			printf("\n");                                                                                                                                  \
			failures++;                                                                                                                                    \
		}                                                                                                                                                  \
	} while (0)

internal int NativeMouse_AbsDiff(int a, int b)
{
	return (a > b) ? (a - b) : (b - a);
}

int NativeMouse_SelfTest(void)
{
	int checks = 0;
	int failures = 0;
	int cx = -1;
	int cy = -1;

	// ---- the window -> canvas step: pillarbox, letterbox, outside ----
	{
		// 1920x1080 window, 4:3 picture: bars of 240 left and right.
		struct NativeMouseGeom pillar = {240, 0, 1440, 1080, 512, 216};
		// 1000x1000 window, 4:3 picture: bars of 125 above and below.
		struct NativeMouseGeom letter = {0, 125, 1000, 750, 512, 216};

		NATIVE_MOUSE_EXPECT(NativeMouse_WindowToCanvas(&pillar, 240.0f, 0.0f, &cx, &cy) && (cx == 0) && (cy == 0), "pillarbox top-left is canvas 0,0");
		NATIVE_MOUSE_EXPECT(NativeMouse_WindowToCanvas(&pillar, 1679.0f, 1079.0f, &cx, &cy) && (cx == 511) && (cy == 215),
		                    "pillarbox bottom-right is canvas 511,215 (got %d,%d)", cx, cy);
		NATIVE_MOUSE_EXPECT(NativeMouse_WindowToCanvas(&pillar, 960.0f, 540.0f, &cx, &cy) && (cx == 256) && (cy == 108),
		                    "pillarbox centre is canvas 256,108 (got %d,%d)", cx, cy);
		NATIVE_MOUSE_EXPECT(!NativeMouse_WindowToCanvas(&pillar, 239.0f, 500.0f, &cx, &cy), "left bar is outside");
		NATIVE_MOUSE_EXPECT(!NativeMouse_WindowToCanvas(&pillar, 1680.0f, 500.0f, &cx, &cy), "right bar is outside");
		NATIVE_MOUSE_EXPECT(!NativeMouse_WindowToCanvas(&pillar, 900.0f, 1080.0f, &cx, &cy), "below the window is outside");
		NATIVE_MOUSE_EXPECT(!NativeMouse_WindowToCanvas(&pillar, -1.0f, 5.0f, &cx, &cy), "negative position is outside");
		NATIVE_MOUSE_EXPECT(!NativeMouse_WindowToCanvas(&letter, 500.0f, 124.0f, &cx, &cy), "top bar is outside");
		NATIVE_MOUSE_EXPECT(NativeMouse_WindowToCanvas(&letter, 500.0f, 125.0f, &cx, &cy) && (cy == 0) && (cx == 256),
		                    "first picture row of the letterbox is row 0");
		NATIVE_MOUSE_EXPECT(!NativeMouse_WindowToCanvas(&letter, 500.0f, 875.0f, &cx, &cy), "bottom bar is outside");
		NATIVE_MOUSE_EXPECT(NativeMouse_WindowToCanvas(&letter, 500.0f, 874.0f, &cx, &cy) && (cy == 215),
		                    "last picture row of the letterbox is row 215 (got %d)", cy);
	}

	// ---- the shift of single elements, written out as numbers ----
	{
		const int left[4] = {0, 20, 60, 100};
		const int centre[4] = {200, 20, 300, 100};
		const int right[4] = {460, 20, 512, 100};

		// 4:3: no shift for anybody, in a menu or not.
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(512, 1, NATIVE_MOUSE_RULE_EDGE, 0, left, 30) == 30, "4:3 menu left is the identity");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(512, 1, NATIVE_MOUSE_RULE_EDGE, 0, right, 480) == 480, "4:3 menu right is the identity");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(512, 0, NATIVE_MOUSE_RULE_THIRDS, 0, centre, 250) == 250, "4:3 thirds is the identity");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(512, 0, NATIVE_MOUSE_RULE_FULL, 0, left, 511) == 511, "4:3 full canvas is the identity");

		// 16:9, canvas 682. Zone = canvas, so LEFT +0, CENTRE +85, RIGHT +170.
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(682, 1, NATIVE_MOUSE_RULE_EDGE, 0, left, 30) == 30, "682 menu left holds the left edge");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(682, 1, NATIVE_MOUSE_RULE_EDGE, 0, centre, 335) == 250, "682 menu centre is +85");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(682, 1, NATIVE_MOUSE_RULE_EDGE, 0, right, 650) == 480, "682 menu right is +170");

		// 43:18, canvas 918. In a menu the zone is 682 wide from 118: LEFT +118,
		// CENTRE +203, RIGHT +288. Outside menus: 0, +203, +406.
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(918, 1, NATIVE_MOUSE_RULE_EDGE, 0, left, 148) == 30, "918 menu left is +118");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(918, 1, NATIVE_MOUSE_RULE_EDGE, 0, centre, 453) == 250, "918 menu centre is +203");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(918, 1, NATIVE_MOUSE_RULE_EDGE, 0, right, 768) == 480, "918 menu right is +288");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(918, 0, NATIVE_MOUSE_RULE_THIRDS, 0, left, 30) == 30, "918 thirds left is +0");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(918, 0, NATIVE_MOUSE_RULE_THIRDS, 0, right, 886) == 480, "918 thirds right is +406");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(918, 1, NATIVE_MOUSE_RULE_POLICY, (int)CTR_UI_ANCHOR_CENTRE, centre, 453) == 250,
		                    "918 policy centre is +203");
		NATIVE_MOUSE_EXPECT(NativeMouse_UnmapX(918, 1, NATIVE_MOUSE_RULE_FULL, 0, left, 918) == 512, "918 full canvas is the stretch");
	}

	// ---- round trip: forward map (menu point -> canvas -> window) -> back ----
	{
		static const int canvases[] = {512, 682, 918};
		static const int boxes[][4] = {{0, 20, 60, 100}, {10, 0, 120, 60}, {200, 20, 300, 100}, {150, 80, 360, 140}, {460, 20, 512, 100}, {400, 0, 505, 40}};
		static const int rules[] = {NATIVE_MOUSE_RULE_DECL, NATIVE_MOUSE_RULE_POLICY, NATIVE_MOUSE_RULE_EDGE, NATIVE_MOUSE_RULE_THIRDS};
		static const int anchors[] = {(int)CTR_UI_ANCHOR_LEFT, (int)CTR_UI_ANCHOR_CENTRE, (int)CTR_UI_ANCHOR_RIGHT};
		int ci;
		int bi;
		int ri;
		int ai;
		int mode;

		for (ci = 0; ci < 3; ci++)
		{
			const int canvasW = canvases[ci];
			// A window with a bar on each side and 4 window pixels per canvas
			// column, so the pixel grid is finer than the 1 unit allowed.
			struct NativeMouseGeom geom = {37, 11, canvasW * 4, 216 * 4, canvasW, 216};
			int px;

			for (mode = 0; mode < 2; mode++)
			{
				for (bi = 0; bi < 6; bi++)
				{
					for (ri = 0; ri < 4; ri++)
					{
						const int anchorCount = ((rules[ri] == NATIVE_MOUSE_RULE_DECL) || (rules[ri] == NATIVE_MOUSE_RULE_POLICY)) ? 3 : 1;

						for (ai = 0; ai < anchorCount; ai++)
						{
							struct CTR_UIViewParameters view;
							const int *box = boxes[bi];
							const int anchor = anchors[ai];
							int shift;

							NativeMouse_BuildView(&view, canvasW, mode);
							shift = NativeMouse_ElementShift(&view, rules[ri], anchor, box);

							for (px = box[0]; px <= box[2]; px += 7)
							{
								const int forwardX = px + shift;
								const float winX = (float)geom.vpX + ((float)forwardX + 0.5f) * (float)geom.vpW / (float)canvasW;
								const float winY = (float)geom.vpY + 50.5f * (float)geom.vpH / 216.0f;
								int rx;

								if ((forwardX < 0) || (forwardX >= canvasW))
								{
									continue; // the element is off the picture there
								}

								if (!NativeMouse_WindowToCanvas(&geom, winX, winY, &cx, &cy))
								{
									NATIVE_MOUSE_EXPECT(0, "round trip lost the picture: canvas %d mode %d box %d rule %d x %d", canvasW, mode, bi,
									                    rules[ri], px);
									continue;
								}

								rx = NativeMouse_UnmapX(canvasW, mode, rules[ri], anchor, box, cx);
								NATIVE_MOUSE_EXPECT((NativeMouse_AbsDiff(rx, px) <= 1) && (cy == 50),
								                    "round trip canvas %d mode %d box %d rule %d anchor %d: %d came back as %d,%d", canvasW, mode, bi,
								                    rules[ri], anchor, px, rx, cy);
							}
						}
					}
				}
			}

			// A full-canvas element is stretched by the layer: a grid of points.
			for (px = 0; px < 512; px += 11)
			{
				const int forwardX = (int)(((long long)px * canvasW) / 512);
				const float winX = (float)geom.vpX + ((float)forwardX + 0.5f) * (float)geom.vpW / (float)canvasW;
				int rx;

				NativeMouse_WindowToCanvas(&geom, winX, 100.0f, &cx, &cy);
				rx = NativeMouse_UnmapX(canvasW, 1, NATIVE_MOUSE_RULE_FULL, 0, boxes[0], cx);
				NATIVE_MOUSE_EXPECT(NativeMouse_AbsDiff(rx, px) <= 1, "full canvas round trip canvas %d: %d came back as %d", canvasW, px, rx);
			}
		}
	}

	// ---- the state machine (focus faked, no window) ----
	{
		RECT frame = {0, 0, 100, 50};
		int mx = 0;
		int my = 0;

		s_mouseFocusOverride = 1;

		NATIVE_MOUSE_EXPECT(!NativeMouse_Active(), "inactive at the start");
		NATIVE_MOUSE_EXPECT(!NativeMouse_MenuPoint(&frame, &mx, &my), "inactive: MenuPoint answers 0");
		NATIVE_MOUSE_EXPECT(!NativeMouse_MenuPoint(NULL, &mx, &my), "NULL element answers 0");

		NativeMouse_OnMotion(100.0f, 100.0f);
		NATIVE_MOUSE_EXPECT(!NativeMouse_Active(), "the first position alone does not activate");
		NativeMouse_OnMotion(100.0f, 100.0f);
		NATIVE_MOUSE_EXPECT(!NativeMouse_Active(), "a motion event without movement does not activate");
		NativeMouse_OnMotion(120.0f, 100.0f);
		NATIVE_MOUSE_EXPECT(NativeMouse_Active(), "movement activates");
		NATIVE_MOUSE_EXPECT(NativeMouse_TakeMoved() == 1, "moved is reported");
		NATIVE_MOUSE_EXPECT(NativeMouse_TakeMoved() == 0, "moved is consumed");

		NativeMouse_OnButton(SDL_BUTTON_LEFT, 1);
		NativeMouse_OnButton(SDL_BUTTON_RIGHT, 1);
		NativeMouse_OnButton(SDL_BUTTON_LEFT, 0);
		NATIVE_MOUSE_EXPECT((NativeMouse_TakeClick(1) == 1) && (NativeMouse_TakeClick(1) == 0), "left click is recorded once and consumed");
		NATIVE_MOUSE_EXPECT((NativeMouse_TakeClick(2) == 1) && (NativeMouse_TakeClick(2) == 0), "right click is recorded once and consumed");

		NativeMouse_OnWheel(1.0f);
		NativeMouse_OnWheel(2.0f);
		NativeMouse_OnWheel(-0.5f);
		NATIVE_MOUSE_EXPECT(NativeMouse_TakeWheel() == 2, "wheel adds up and keeps the fraction");
		NativeMouse_OnWheel(0.5f);
		NATIVE_MOUSE_EXPECT((NativeMouse_TakeWheel() == 1) && (NativeMouse_TakeWheel() == 0), "the kept fraction completes the next notch");

		NativeMouse_OnButton(SDL_BUTTON_LEFT, 1);
		NativeMouse_OnWheel(3.0f);
		NativeMouse_OnKeyOrPad();
		NATIVE_MOUSE_EXPECT(!NativeMouse_Active(), "a key press deactivates");
		NATIVE_MOUSE_EXPECT((NativeMouse_TakeClick(1) == 0) && (NativeMouse_TakeWheel() == 0), "pending click and wheel are dropped");

		NativeMouse_OnWheel(1.0f);
		NATIVE_MOUSE_EXPECT(NativeMouse_TakeWheel() == 0, "wheel while inactive is not recorded");
		NativeMouse_OnButton(SDL_BUTTON_LEFT, 1);
		NATIVE_MOUSE_EXPECT(NativeMouse_Active(), "a click while inactive activates");
		NATIVE_MOUSE_EXPECT(NativeMouse_TakeClick(1) == 0, "but it is not counted as a click");

		s_mouseFocusOverride = 0;
		NATIVE_MOUSE_EXPECT(!NativeMouse_Active(), "no focus: inactive");
		NativeMouse_OnKeyOrPad();
		NativeMouse_OnMotion(300.0f, 300.0f);
		NATIVE_MOUSE_EXPECT(!NativeMouse_Active(), "no focus: motion does not activate");

		s_mouseFocusOverride = 1;
		g_cfg_deterministic = 1;
		NativeMouse_OnMotion(400.0f, 400.0f);
		NATIVE_MOUSE_EXPECT(!NativeMouse_Active(), "measuring mode: never active");
		g_cfg_deterministic = 0;

		NativeMouse_OnKeyOrPad();
		s_mouseFocusOverride = -1;
		s_mouseHavePos = 0;
	}

	printf("mouse selftest %s: %d checks, %d failures\n", (failures == 0) ? "passed" : "FAILED", checks, failures);
	return (failures == 0) ? 0 : 1;
}

#undef RECT
