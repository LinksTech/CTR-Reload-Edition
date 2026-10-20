// THE EXTERNAL TRACK'S HUD MINIMAP (--ext-track, see platform/engine_exttrack.c;
// day round 1 of 2026-10-20).
//
// HOW THE GAME DRAWS IT. game/UI/UI_RenderFrame.c (one player, the map and not
// the speedometer) calls UI_Map_DrawDrivers with the table of the LEV
// (pointers[ST1_MAP] of level1->ptrSpawnType1, struct UIMapSpawnMetadata) and
// UI_Map_DrawMap with the two halves of the picture, gGT->ptrIcons[3] (top) and
// [4] (bottom): two semi-transparent POLY_FT4, right edge x 500, bottom edge
// y 195 in the 512 x 216 HUD space, one texel per pixel. A marker sits where
// UI_Map_GetIconPos (game/UI/UI_Map.c) puts it: the table's start plus world x/z
// times size / range, by the table's mode (0, 90, 180, 270 degrees), minus 16 in
// y, the icon drawn from there to the right and down (DecalHUD_DrawPolyGT4).
// For the external track picture and table are the donor's.
//
// WHAT THIS DOES, ONLY WITH --ext-track AND A "map" SECTION ({"line": [[x, z],
// ...]} in world units, the driven line projected; cnk\tools\tr1_map.py), at
// stage 8 of the slot's level (Engine_ExtTrackLoad), drawing data only:
//   - the table: a copy of the donor's (its topHalfMode kept) with a mode, start,
//     size and range chosen so that the line fills the picture's rectangle; the
//     ratio of the y to the x scale is the donor's (the retail pictures carry the
//     HUD's pixel aspect); pointers[ST1_MAP] of the loaded LEV points to it;
//   - the picture: the donor's texels are read from VRAM (StoreImage), its most
//     frequent index is the background, the next the line (and a third, if it is
//     common, the line's border); the picture is cleared to the background and
//     the line rastered with the SAME integer formula UI_Map_GetIconPos uses for
//     the markers (plus the marker icon's half size: its middle sits on the
//     line), as wide as the donor's line, and written back (LoadImage2). The
//     donor's colour table and semi-transparency stay.
// Without the section: nothing changes. Log: one line at load, one line every
// 30 drawn frames with the player marker and its distance to the line (HUD
// pixels), one summary at exit (cnk\tools\tr1_map.py eval, gate A6).

#define EXT_MAP_POINTS_MAX 1024
#define EXT_MAP_RANGE 32000 // world range of the made table on both axes (-16000..16000)
#define EXT_MAP_RIGHT 500   // UI_RenderFrame.c: mapPosX
#define EXT_MAP_BOTTOM 195  // UI_RenderFrame.c: mapPosY
#define EXT_MAP_ICON_Y 16   // UI_Map.c: UI_MAP_ICON_Y_OFFSET
#define EXT_MAP_HUMAN_ICON 0x32
#define EXT_MAP_ICON_GROUP 5
#define EXT_MAP_TEXELS_MAX (256 * 256)
#define EXT_MAP_LOG_EVERY 30

global_variable struct
{
	int ready;
	int levelId;
	const void *level1;
	struct UIMapSpawnMetadata meta; // pointers[ST1_MAP] of the loaded LEV points here
	int points;
	s16 line[EXT_MAP_POINTS_MAX][2];
	int iconW, iconH;
	int reportOn;
	// the marker statistics (gate A6)
	u32 frames;
	u32 samples;
	u32 within6;
	double distSum;
	double distMax;
} s_extMap;

// --------------------------------------------------------------------------
// Pure helpers (the selftest checks them without a game)

// UI_Map_GetIconPos (game/UI/UI_Map.c) for one player, the same integer
// formula: the marker icon's top-left corner in HUD pixels.
void ExtMap_IconPos(const struct UIMap *map, int x, int z, int *outX, int *outY)
{
	const int rangeX = map->worldEndX - map->worldStartX;
	const int rangeY = map->worldEndY - map->worldStartY;
	int addX = 0;
	int addY = 0;

	if ((rangeX == 0) || (rangeY == 0))
	{
		*outX = map->iconStartX;
		*outY = map->iconStartY - EXT_MAP_ICON_Y;
		return;
	}
	switch (map->mode)
	{
	case 0:
		addX = (x * map->iconSizeX) / rangeX;
		addY = (z * map->iconSizeY * 2) / rangeY;
		break;
	case 1:
		addX = -(z * map->iconSizeX) / rangeY;
		addY = (x * map->iconSizeY * 2) / rangeX;
		break;
	case 2:
		addX = -(x * map->iconSizeX) / rangeX;
		addY = -(z * map->iconSizeY * 2) / rangeY;
		break;
	default:
		addX = (z * map->iconSizeX) / rangeY;
		addY = -(x * map->iconSizeY * 2) / rangeX;
		break;
	}
	*outX = map->iconStartX + addX;
	*outY = map->iconStartY + addY - EXT_MAP_ICON_Y;
}

// The ratio of the y scale to the x scale of a table (HUD pixels per world unit), 0 when unusable.
double ExtMap_Aspect(const struct UIMap *map)
{
	const double rx = (double)(map->worldEndX - map->worldStartX);
	const double ry = (double)(map->worldEndY - map->worldStartY);
	double sx;
	double sy;

	if ((rx == 0.0) || (ry == 0.0) || (map->iconSizeX == 0) || (map->iconSizeY == 0))
	{
		return 0.0;
	}
	// Modes 1 and 3 take x's scale from rangeY and y's from rangeX.
	sx = ((map->mode & 1) == 0) ? ((double)map->iconSizeX / rx) : ((double)map->iconSizeX / ry);
	sy = ((map->mode & 1) == 0) ? (2.0 * (double)map->iconSizeY / ry) : (2.0 * (double)map->iconSizeY / rx);
	sx = (sx < 0.0) ? -sx : sx;
	sy = (sy < 0.0) ? -sy : sy;
	return (sx > 0.0) ? (sy / sx) : 0.0;
}

// The table for the line: the mode that lets it fill the rectangle (x0, y0,
// w, h in HUD pixels; margin kept free on every side) most, the y scale aspect
// times the x scale, the marker icon's middle (iconW/2, iconH/2 from its corner)
// on the line. Only the fields of struct UIMap are set. 1 = done.
int ExtMap_Fit(const s16 (*line)[2], int count, int x0, int y0, int w, int h, int margin, double aspect, int iconW, int iconH, struct UIMap *out)
{
	double best = 0.0;
	int bestMode = -1;
	int mode;
	int i;
	int dxMin = 0, dxMax = 0, dyMin = 0, dyMax = 0;

	if ((line == NULL) || (count < 2) || (aspect <= 0.0) || (w - 2 * margin < 4) || (h - 2 * margin < 4))
	{
		return 0;
	}
	for (mode = 0; mode < 4; mode++)
	{
		double uMin = 1e9, uMax = -1e9, vMin = 1e9, vMax = -1e9;
		double s;

		for (i = 0; i < count; i++)
		{
			const double x = line[i][0];
			const double z = line[i][1];
			const double u = (mode == 0) ? x : ((mode == 1) ? -z : ((mode == 2) ? -x : z));
			const double v = (mode == 0) ? z : ((mode == 1) ? x : ((mode == 2) ? -z : -x));

			uMin = (u < uMin) ? u : uMin;
			uMax = (u > uMax) ? u : uMax;
			vMin = (v < vMin) ? v : vMin;
			vMax = (v > vMax) ? v : vMax;
		}
		if ((uMax - uMin < 1.0) || (vMax - vMin < 1.0))
		{
			return 0;
		}
		s = (double)(w - 2 * margin) / (uMax - uMin);
		if ((double)(h - 2 * margin) / (aspect * (vMax - vMin)) < s)
		{
			s = (double)(h - 2 * margin) / (aspect * (vMax - vMin));
		}
		if (s > best * 1.0001)
		{
			best = s;
			bestMode = mode;
		}
	}
	memset(out, 0, sizeof(*out));
	out->mode = (s16)bestMode;
	out->worldStartX = out->worldStartY = (s16)(-EXT_MAP_RANGE / 2);
	out->worldEndX = out->worldEndY = (s16)(EXT_MAP_RANGE / 2);
	{
		// Rounded down, so the line stays inside the rectangle.
		const int sizeX = (int)(best * EXT_MAP_RANGE);
		const int sizeY = (int)(best * aspect * EXT_MAP_RANGE / 2.0);

		if ((sizeX < 1) || (sizeY < 1) || (sizeX > 32767) || (sizeY > 32767))
		{
			return 0;
		}
		out->iconSizeX = (s16)sizeX;
		out->iconSizeY = (s16)sizeY;
	}
	// The offsets with the table's own formula (start 0 and the y offset taken out), centred.
	for (i = 0; i < count; i++)
	{
		int px;
		int py;

		ExtMap_IconPos(out, line[i][0], line[i][1], &px, &py);
		py += EXT_MAP_ICON_Y;
		if ((i == 0) || (px < dxMin))
		{
			dxMin = px;
		}
		if ((i == 0) || (px > dxMax))
		{
			dxMax = px;
		}
		if ((i == 0) || (py < dyMin))
		{
			dyMin = py;
		}
		if ((i == 0) || (py > dyMax))
		{
			dyMax = py;
		}
	}
	out->iconStartX = (s16)(x0 + w / 2 - iconW / 2 - (dxMin + dxMax) / 2);
	out->iconStartY = (s16)(y0 + h / 2 - iconH / 2 + EXT_MAP_ICON_Y - (dyMin + dyMax) / 2);
	return 1;
}

// Where the marker's middle is for world x/z (HUD pixels).
internal void ExtMap_MarkerMiddle(const struct UIMap *map, int x, int z, int iconW, int iconH, double *mx, double *my)
{
	int px;
	int py;

	ExtMap_IconPos(map, x, z, &px, &py);
	*mx = (double)px + (double)iconW * 0.5;
	*my = (double)py + (double)iconH * 0.5;
}

// Distance of point (px, py) to the segment a-b.
internal double ExtMap_SegDist(double px, double py, double ax, double ay, double bx, double by)
{
	const double dx = bx - ax;
	const double dy = by - ay;
	const double l2 = dx * dx + dy * dy;
	double t = (l2 > 0.0) ? (((px - ax) * dx + (py - ay) * dy) / l2) : 0.0;

	t = (t < 0.0) ? 0.0 : ((t > 1.0) ? 1.0 : t);
	return sqrt((px - ax - t * dx) * (px - ax - t * dx) + (py - ay - t * dy) * (py - ay - t * dy));
}

// Distance (HUD pixels) of the point to the closed line as the table draws it.
double ExtMap_LineDistance(const struct UIMap *map, const s16 (*line)[2], int count, int iconW, int iconH, double px, double py)
{
	double best = 1e9;
	int i;

	for (i = 0; i < count; i++)
	{
		double ax, ay, bx, by, d;

		ExtMap_MarkerMiddle(map, line[i][0], line[i][1], iconW, iconH, &ax, &ay);
		ExtMap_MarkerMiddle(map, line[(i + 1) % count][0], line[(i + 1) % count][1], iconW, iconH, &bx, &by);
		d = ExtMap_SegDist(px, py, ax, ay, bx, by);
		best = (d < best) ? d : best;
	}
	return best;
}

// Rasters the closed line into idx (w x h texels, the picture whose top-left
// texel is HUD pixel x0, y0): texels within radius of the line get `fill`,
// within radius + 1 `border` (if border >= 0); the rest stays as it is.
// Returns the texels set to fill.
int ExtMap_Raster(u8 *idx, int w, int h, int x0, int y0, const struct UIMap *map, const s16 (*line)[2], int count, int iconW, int iconH,
                  double radius, int fill, int border)
{
	static double mid[EXT_MAP_POINTS_MAX][2];
	int set = 0;
	int ty;
	int i;

	if ((count < 2) || (count > EXT_MAP_POINTS_MAX))
	{
		return 0;
	}
	// The line's points as the table places them, once.
	for (i = 0; i < count; i++)
	{
		ExtMap_MarkerMiddle(map, line[i][0], line[i][1], iconW, iconH, &mid[i][0], &mid[i][1]);
	}
	for (ty = 0; ty < h; ty++)
	{
		int tx;

		for (tx = 0; tx < w; tx++)
		{
			// the texel's middle in HUD pixels, its distance to the closed line
			const double px = (double)(x0 + tx) + 0.5;
			const double py = (double)(y0 + ty) + 0.5;
			double d = 1e9;

			for (i = 0; i < count; i++)
			{
				const int j = (i + 1) % count;
				const double e = ExtMap_SegDist(px, py, mid[i][0], mid[i][1], mid[j][0], mid[j][1]);

				d = (e < d) ? e : d;
			}

			if (d <= radius)
			{
				idx[ty * w + tx] = (u8)fill;
				set++;
			}
			else if ((border >= 0) && (d <= radius + 1.0))
			{
				idx[ty * w + tx] = (u8)border;
			}
		}
	}
	return set;
}

// --------------------------------------------------------------------------
// VRAM: one icon's texel rectangle (4 or 8 bit) to and from an index buffer

// THE QUAD'S UV CORNERS (round 2 of 2026-10-20): UI_Map_DrawMap draws each half
// on the screen as wide as u1 - u0 and as tall as v2 - v0, its corners taking
// the four UV corners of the icon as they are - a half may lie turned or
// mirrored in VRAM (the first build assumed rows v0.. and columns u0.. and drew
// the line into the wrong texels). So the texels are the box around the four
// UV corners, and each texel takes the screen pixel the quad maps it to (the
// affine map of the quad's first triangle: corners 0, 1, 2).
typedef struct
{
	int baseX, baseY, depth; // page origin (16-bit words), 0 = 4 bit, 1 = 8 bit
	int u0, v0, w, h;        // the texel box around the UV corners
	int sw, sh;              // the half on the screen (HUD pixels): u1 - u0, v2 - v0
	int cu[4], cv[4];        // the UV corners 0..3 (top left, top right, bottom left, bottom right)
	RECT16 rect;             // the VRAM words that hold the box
} ExtMapIconRect;

internal int ExtMap_IconRect(const struct Icon *icon, ExtMapIconRect *r)
{
	const struct TextureLayout *t = &icon->texLayout;
	const u16 tpage = t->tpage;
	int umin, umax, vmin, vmax, k;

	memset(r, 0, sizeof(*r));
	r->baseX = (tpage & 0xf) * 64;
	r->baseY = ((tpage >> 4) & 1) * 256;
	r->depth = (tpage >> 7) & 3;
	r->cu[0] = t->u0;
	r->cv[0] = t->v0;
	r->cu[1] = t->u1;
	r->cv[1] = t->v1;
	r->cu[2] = t->u2;
	r->cv[2] = t->v2;
	r->cu[3] = t->u3;
	r->cv[3] = t->v3;
	r->sw = (int)t->u1 - (int)t->u0;
	r->sh = (int)t->v2 - (int)t->v0;
	umin = umax = r->cu[0];
	vmin = vmax = r->cv[0];
	for (k = 1; k < 4; k++)
	{
		umin = (r->cu[k] < umin) ? r->cu[k] : umin;
		umax = (r->cu[k] > umax) ? r->cu[k] : umax;
		vmin = (r->cv[k] < vmin) ? r->cv[k] : vmin;
		vmax = (r->cv[k] > vmax) ? r->cv[k] : vmax;
	}
	r->u0 = umin;
	r->v0 = vmin;
	r->w = umax - umin;
	r->h = vmax - vmin;
	if ((r->depth > 1) || (r->sw <= 0) || (r->sh <= 0) || (r->w <= 0) || (r->h <= 0) || (r->w * r->h > EXT_MAP_TEXELS_MAX))
	{
		return 0;
	}
	{
		const int per = (r->depth == 0) ? 4 : 2;
		const int first = r->u0 / per;
		const int last = (r->u0 + r->w - 1) / per;

		r->rect.x = (short)(r->baseX + first);
		r->rect.y = (short)(r->baseY + r->v0);
		r->rect.w = (short)(last - first + 1);
		r->rect.h = (short)r->h;
	}
	return 1;
}

// The screen pixel (sx, sy) of the half that texel (u, v) of its box shows
// (u, v relative to the box). 1 = inside the half.
int ExtMap_TexelToScreen(const ExtMapIconRect *r, int u, int v, int *sx, int *sy)
{
	const double e1u = (double)(r->cu[1] - r->cu[0]) / (double)r->sw;
	const double e1v = (double)(r->cv[1] - r->cv[0]) / (double)r->sw;
	const double e2u = (double)(r->cu[2] - r->cu[0]) / (double)r->sh;
	const double e2v = (double)(r->cv[2] - r->cv[0]) / (double)r->sh;
	const double det = e1u * e2v - e1v * e2u;
	const double du = (double)(r->u0 + u) + 0.5 - (double)r->cu[0];
	const double dv = (double)(r->v0 + v) + 0.5 - (double)r->cv[0];
	double x, y;

	if ((det > -1e-9) && (det < 1e-9))
	{
		return 0;
	}
	x = (du * e2v - dv * e2u) / det;
	y = (e1u * dv - e1v * du) / det;
	if ((x < 0.0) || (y < 0.0) || (x >= (double)r->sw) || (y >= (double)r->sh))
	{
		return 0;
	}
	*sx = (int)x;
	*sy = (int)y;
	return 1;
}

// Texel (u, v) of the icon (relative to u0, v0) in the words of its rectangle.
internal int ExtMap_GetTexel(const ExtMapIconRect *r, const u16 *words, int u, int v)
{
	const int per = (r->depth == 0) ? 4 : 2;
	const int at = r->u0 + u;
	const int word = at / per - r->u0 / per;
	const int shift = (at % per) * ((r->depth == 0) ? 4 : 8);
	const u16 wv = words[v * r->rect.w + word];

	return (wv >> shift) & ((r->depth == 0) ? 0xf : 0xff);
}

internal void ExtMap_SetTexel(const ExtMapIconRect *r, u16 *words, int u, int v, int value)
{
	const int per = (r->depth == 0) ? 4 : 2;
	const int at = r->u0 + u;
	const int word = at / per - r->u0 / per;
	const int shift = (at % per) * ((r->depth == 0) ? 4 : 8);
	const u16 mask = (u16)(((r->depth == 0) ? 0xf : 0xff) << shift);
	u16 *wv = &words[v * r->rect.w + word];

	*wv = (u16)((*wv & ~mask) | (((u16)value << shift) & mask));
}

// --------------------------------------------------------------------------
// Load (Engine_ExtTrackLoad, stage 8 of the slot's level)

internal void ExtMap_Report(void)
{
	if (s_extMap.samples == 0u)
	{
		Platform_Log("[CTR ExtMap] at exit: %u frame(s) drawn with the map, no marker sample\n", s_extMap.frames);
		return;
	}
	Platform_Log("[CTR ExtMap] at exit: %u marker sample(s) of seat 0: distance to the line mean %.2f, max %.2f HUD px, within 6 px %u (%.1f %%)\n",
	             s_extMap.samples, s_extMap.distSum / (double)s_extMap.samples, s_extMap.distMax, s_extMap.within6,
	             100.0 * (double)s_extMap.within6 / (double)s_extMap.samples);
}

// The line of the section into s_extMap.line. Returns the points read, 0 on any bad entry.
internal int ExtMap_ReadLine(const ExtJsonDoc *doc, int section)
{
	const int line = ExtJson_Get(doc, section, "line");
	const int count = ExtJson_Count(doc, line);
	int node;
	int n = 0;

	if ((ExtJson_Type(doc, line) != EXT_JSON_ARRAY) || (count < 3) || (count > EXT_MAP_POINTS_MAX))
	{
		return 0;
	}
	for (node = ExtJson_First(doc, line); node >= 0; node = ExtJson_Next(doc, node))
	{
		double p[2];

		if ((ExtJson_Type(doc, node) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, node) != 2) || (ExtJson_Numbers(doc, node, p, 2) != 2) ||
		    (p[0] < -32767.0) || (p[0] > 32767.0) || (p[1] < -32767.0) || (p[1] > 32767.0))
		{
			return 0;
		}
		s_extMap.line[n][0] = (s16)p[0];
		s_extMap.line[n][1] = (s16)p[1];
		n++;
	}
	return n;
}

// THE COLOUR TABLES (round 2 of 2026-10-20): the retail map halves share their
// texels and differ in the colour table only (Sunset Vista: icons 3 and 4 on the
// same 95x43 texels of one page, CLUT 0x7424 shows bits 2-3 of an index, CLUT
// 0x7425 bits 0-1 - two 2-bit pictures in one 4-bit one). The first build wrote
// the top picture and then the bottom one into the same texels. So the colours
// are read through each half's table, and each texel gets the index whose
// colours, through both tables, come closest to what both halves want there.
typedef struct
{
	int n;            // entries: 16 (4 bit) or 256 (8 bit)
	u16 colour[256];  // the table as in VRAM
	int bg, fill, border; // the donor picture's colours through this table (-1 none)
} ExtMapClut;

internal void ExtMap_ReadClut(const struct Icon *icon, int depth, ExtMapClut *c)
{
	const u16 clut = icon->texLayout.clut;
	RECT16 rect;

	memset(c, 0, sizeof(*c));
	c->n = (depth == 0) ? 16 : 256;
	rect.x = (short)((clut & 0x3f) * 16);
	rect.y = (short)((clut >> 6) & 0x1ff);
	rect.w = (short)c->n;
	rect.h = 1;
	StoreImage(&rect, (u32 *)c->colour);
	c->bg = c->fill = c->border = -1;
}

// The donor picture's colours of one half through its table: the most frequent
// is the background, the next the line, the next (if at least a tenth of the
// line) the border.
internal void ExtMap_ClutColours(const ExtMapIconRect *r, const u16 *words, ExtMapClut *c)
{
	static u32 count[65536];
	u32 best[3] = {0, 0, 0};
	int colour[3] = {-1, -1, -1};
	int v;
	int k;

	memset(count, 0, sizeof(count));
	for (v = 0; v < r->h; v++)
	{
		int u;

		for (u = 0; u < r->w; u++)
		{
			int sx;
			int sy;

			if (ExtMap_TexelToScreen(r, u, v, &sx, &sy))
			{
				count[c->colour[ExtMap_GetTexel(r, words, u, v)]]++;
			}
		}
	}
	for (k = 0; k < c->n; k++)
	{
		const int col = c->colour[k];
		const u32 n = count[col];
		int j;

		if ((n == 0u) || (col == colour[0]) || (col == colour[1]) || (col == colour[2]))
		{
			continue;
		}
		for (j = 0; j < 3; j++)
		{
			if (n > best[j])
			{
				int m;

				for (m = 2; m > j; m--)
				{
					best[m] = best[m - 1];
					colour[m] = colour[m - 1];
				}
				best[j] = n;
				colour[j] = col;
				break;
			}
		}
	}
	c->bg = colour[0];
	c->fill = colour[1];
	c->border = ((colour[2] >= 0) && (best[2] * 10u >= best[1])) ? colour[2] : -1;
}

// The colour a half wants for a picture category (0 background, 1 line, 2 border).
internal int ExtMap_Want(const ExtMapClut *c, int category)
{
	if (category == 1)
	{
		return c->fill;
	}
	if ((category == 2) && (c->border >= 0))
	{
		return c->border;
	}
	return (category == 2) ? c->fill : c->bg;
}

// The index for a texel that half T wants category dT (-1: T does not show the
// texel) and half B dB; keep = its index now (taken when nothing is better).
int ExtMap_PickIndex(const ExtMapClut *ct, int dT, const ExtMapClut *cb, int dB, int keep)
{
	static const int weight[3] = {3, 4, 1}; // a missing line weighs most, a wrong background next
	int best = keep;
	int bestCost = 1 << 30;
	int i;

	for (i = -1; i < ct->n; i++)
	{
		const int idx = (i < 0) ? keep : i;
		int cost = 0;

		if ((dT >= 0) && ((int)ct->colour[idx] != ExtMap_Want(ct, dT)))
		{
			cost += weight[dT];
		}
		if ((dB >= 0) && ((int)cb->colour[idx] != ExtMap_Want(cb, dB)))
		{
			cost += weight[dB];
		}
		if (cost < bestCost)
		{
			bestCost = cost;
			best = idx;
		}
	}
	return best;
}

void ExtMap_LevelLoad(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const ExtJsonDoc *doc = ExtTrack_Json();
	const int section = ExtTrack_Section("map");
	struct Level *lev;
	void **pointers;
	struct UIMapSpawnMetadata *donor;
	struct Icon *top;
	struct Icon *bottom;
	ExtMapIconRect rt = {0}, rb = {0};
	static u16 wordsTop[EXT_MAP_TEXELS_MAX];
	static u16 wordsBottom[EXT_MAP_TEXELS_MAX];
	static u8 picture[2 * EXT_MAP_TEXELS_MAX];
	static s8 wantTop[EXT_MAP_TEXELS_MAX];
	static s8 wantBottom[EXT_MAP_TEXELS_MAX];
	static ExtMapClut clutTop, clutBottom;
	int drawTop;
	int shared;
	int wMax, hAll, x0, y0;
	double aspect;
	double radius = 2.0;
	int set;
	u32 changed = 0;
	u32 mismatched = 0;

	s_extMap.ready = 0;
	if (!ExtTrack_Active() || (doc == NULL) || (section < 0) || (gGT == NULL) || (gGT->level1 == NULL) || !ExtTrack_ActiveForLevel(gGT->levelID))
	{
		return;
	}
	lev = gGT->level1;
	s_extMap.points = ExtMap_ReadLine(doc, section);
	if (s_extMap.points == 0)
	{
		Platform_Log("[CTR ExtMap] level %d: map.line missing or not 3..%d pairs [x, z] in s16 - the donor's map stays\n", gGT->levelID, EXT_MAP_POINTS_MAX);
		return;
	}
	if ((lev->ptrSpawnType1 == NULL) || (lev->ptrSpawnType1->count == 0))
	{
		Platform_Log("[CTR ExtMap] level %d: the donor has no map table - no map\n", gGT->levelID);
		return;
	}
	pointers = ST1_GETPOINTERS(lev->ptrSpawnType1);
	donor = (struct UIMapSpawnMetadata *)pointers[ST1_MAP];
	top = gGT->ptrIcons[3];
	bottom = gGT->ptrIcons[4];
	if ((donor == NULL) || (bottom == NULL) || ((donor->topHalfMode == 0) && (top == NULL)))
	{
		Platform_Log("[CTR ExtMap] level %d: the donor has no map table or picture - no map\n", gGT->levelID);
		return;
	}
	if (donor == &s_extMap.meta)
	{
		// The level stayed loaded (a restart): table and picture are ours already.
		s_extMap.ready = 1;
		return;
	}
	drawTop = (donor->topHalfMode == 0);
	if (!ExtMap_IconRect(bottom, &rb) || (drawTop && !ExtMap_IconRect(top, &rt)) || (drawTop && (rt.depth != rb.depth)))
	{
		Platform_Log("[CTR ExtMap] level %d: the map picture is not a 4/8-bit icon pair of a usable size - no map\n", gGT->levelID);
		return;
	}
	// The halves on the same texels (the retail way) share one buffer.
	shared = drawTop && (rt.rect.x == rb.rect.x) && (rt.rect.y == rb.rect.y) && (rt.rect.w == rb.rect.w) && (rt.rect.h == rb.rect.h) && (rt.u0 == rb.u0) &&
	         (rt.v0 == rb.v0) && (rt.w == rb.w) && (rt.h == rb.h);
	if (drawTop && !shared && (rt.rect.x < rb.rect.x + rb.rect.w) && (rb.rect.x < rt.rect.x + rt.rect.w) && (rt.rect.y < rb.rect.y + rb.rect.h) &&
	    (rb.rect.y < rt.rect.y + rt.rect.h))
	{
		Platform_Log("[CTR ExtMap] level %d: the map halves overlap in VRAM without sharing their texels - no map\n", gGT->levelID);
		return;
	}
	StoreImage(&rb.rect, (u32 *)wordsBottom);
	if (drawTop && !shared)
	{
		StoreImage(&rt.rect, (u32 *)wordsTop);
	}
	ExtMap_ReadClut(bottom, rb.depth, &clutBottom);
	ExtMap_ClutColours(&rb, wordsBottom, &clutBottom);
	if (drawTop)
	{
		ExtMap_ReadClut(top, rt.depth, &clutTop);
		ExtMap_ClutColours(&rt, shared ? wordsBottom : wordsTop, &clutTop);
	}
	if ((clutBottom.fill < 0) || (drawTop && (clutTop.fill < 0)))
	{
		Platform_Log("[CTR ExtMap] level %d: a map half shows one colour only - no map\n", gGT->levelID);
		return;
	}
	// The donor line's width: the median length of the horizontal runs of the line colour in the bottom half.
	{
		u32 runs[64];
		int nRuns = 0;
		int v;

		memset(runs, 0, sizeof(runs));
		for (v = 0; v < rb.h; v++)
		{
			int u = 0;

			while (u < rb.w)
			{
				int len = 0;

				while ((u < rb.w) && ((int)clutBottom.colour[ExtMap_GetTexel(&rb, wordsBottom, u, v)] == clutBottom.fill))
				{
					len++;
					u++;
				}
				if (len > 0)
				{
					runs[(len < 63) ? len : 63]++;
					nRuns++;
				}
				u++;
			}
		}
		if (nRuns > 0)
		{
			int acc = 0;
			int len;

			for (len = 1; len < 64; len++)
			{
				acc += (int)runs[len];
				if (2 * acc >= nRuns)
				{
					break;
				}
			}
			radius = (double)len * 0.5;
		}
		radius = (radius < 1.0) ? 1.0 : ((radius > 4.0) ? 4.0 : radius);
	}
	// The marker icon (its middle sits on the line).
	s_extMap.iconW = 0;
	s_extMap.iconH = 0;
	if (gGT->iconGroup[EXT_MAP_ICON_GROUP] != NULL)
	{
		struct Icon **icons = ICONGROUP_GETICONS(gGT->iconGroup[EXT_MAP_ICON_GROUP]);
		struct Icon *marker = icons[EXT_MAP_HUMAN_ICON];

		if (marker != NULL)
		{
			s_extMap.iconW = (int)marker->texLayout.u1 - (int)marker->texLayout.u0;
			s_extMap.iconH = (int)marker->texLayout.v2 - (int)marker->texLayout.v0;
		}
	}
	// The rectangle of the picture in HUD pixels (UI_Map_DrawMap).
	wMax = drawTop ? ((rt.sw > rb.sw) ? rt.sw : rb.sw) : rb.sw;
	hAll = (drawTop ? rt.sh : 0) + rb.sh;
	x0 = EXT_MAP_RIGHT - wMax;
	y0 = EXT_MAP_BOTTOM - hAll;
	aspect = ExtMap_Aspect(&donor->map);
	if (aspect <= 0.0)
	{
		aspect = 1.0;
	}
	s_extMap.meta = *donor;
	if (!ExtMap_Fit((const s16(*)[2])s_extMap.line, s_extMap.points, x0, y0, wMax, hAll, (int)radius + 2, aspect, s_extMap.iconW, s_extMap.iconH,
	                &s_extMap.meta.map))
	{
		Platform_Log("[CTR ExtMap] level %d: the line does not fit the %dx%d picture - the donor's map stays\n", gGT->levelID, wMax, hAll);
		return;
	}
	// The picture in categories: 0 background, 1 line, 2 border.
	memset(picture, 0, (size_t)(wMax * hAll));
	set = ExtMap_Raster(picture, wMax, hAll, x0, y0, &s_extMap.meta.map, (const s16(*)[2])s_extMap.line, s_extMap.points, s_extMap.iconW,
	                    s_extMap.iconH, radius, 1, 2);
	// What each half wants of each texel of its box: the category of the screen
	// pixel its quad shows the texel at (-1: not shown). Each half is
	// right-aligned at x 500, the top one above the bottom one.
	{
		int half;

		memset(wantTop, -1, sizeof(wantTop));
		memset(wantBottom, -1, sizeof(wantBottom));
		for (half = drawTop ? 0 : 1; half < 2; half++)
		{
			const ExtMapIconRect *r = (half == 0) ? &rt : &rb;
			s8 *want = (half == 0) ? wantTop : wantBottom;
			const int rowOffset = (half == 0) ? 0 : (drawTop ? rt.sh : 0);
			const int shift = wMax - r->sw;
			int v;

			for (v = 0; v < r->h; v++)
			{
				int u;

				for (u = 0; u < r->w; u++)
				{
					int sx;
					int sy;

					if (ExtMap_TexelToScreen(r, u, v, &sx, &sy))
					{
						want[v * r->w + u] = (s8)picture[(rowOffset + sy) * wMax + shift + sx];
					}
				}
			}
		}
	}
	// Each texel: the index whose colours through both tables come closest.
	{
		int pass;

		for (pass = 0; pass < (shared ? 1 : 2); pass++)
		{
			const int isBottom = shared || (pass == 0);
			const ExtMapIconRect *r = isBottom ? &rb : &rt;
			u16 *words = isBottom ? wordsBottom : wordsTop;
			int v;

			if (!isBottom && !drawTop)
			{
				continue;
			}
			for (v = 0; v < r->h; v++)
			{
				int u;

				for (u = 0; u < r->w; u++)
				{
					const int dT = (shared || !isBottom) ? wantTop[v * r->w + u] : -1;
					const int dB = isBottom ? wantBottom[v * r->w + u] : -1;
					const int keep = ExtMap_GetTexel(r, words, u, v);
					const ExtMapClut *ct = drawTop ? &clutTop : &clutBottom;
					int idx;

					if ((dT < 0) && (dB < 0))
					{
						continue;
					}
					idx = ExtMap_PickIndex(ct, dT, &clutBottom, dB, keep);
					if (((dT >= 0) && ((int)ct->colour[idx] != ExtMap_Want(ct, dT))) || ((dB >= 0) && ((int)clutBottom.colour[idx] != ExtMap_Want(&clutBottom, dB))))
					{
						mismatched++;
					}
					if (idx != keep)
					{
						ExtMap_SetTexel(r, words, u, v, idx);
						changed++;
					}
				}
			}
		}
	}
	if (drawTop && !shared)
	{
		LoadImage2(&rt.rect, wordsTop);
	}
	LoadImage2(&rb.rect, wordsBottom);
	pointers[ST1_MAP] = &s_extMap.meta;
	s_extMap.ready = 1;
	s_extMap.levelId = gGT->levelID;
	s_extMap.level1 = lev;
	if (!s_extMap.reportOn)
	{
		s_extMap.reportOn = 1;
		Platform_AtExitReport(ExtMap_Report);
	}
	Platform_Log("[CTR ExtMap] level %d: map from %d points: picture %dx%d at %d,%d (top %s, halves %s), %d bit, radius %.1f, %d line texels; "
	             "%u texel(s) changed, %u without an index that fits both halves; table mode %d, start %d,%d, size %d,%d, range %d..%d; aspect %.3f "
	             "(the donor's: mode %d, size %d,%d, range x %d..%d y %d..%d); marker icon %dx%d\n",
	             gGT->levelID, s_extMap.points, wMax, hAll, x0, y0, drawTop ? "yes" : "no", shared ? "share their texels" : "apart", (rb.depth == 0) ? 4 : 8,
	             radius, set, changed, mismatched, s_extMap.meta.map.mode, s_extMap.meta.map.iconStartX, s_extMap.meta.map.iconStartY,
	             s_extMap.meta.map.iconSizeX, s_extMap.meta.map.iconSizeY, s_extMap.meta.map.worldStartX, s_extMap.meta.map.worldEndX, aspect, donor->map.mode,
	             donor->map.iconSizeX, donor->map.iconSizeY, donor->map.worldStartX, donor->map.worldEndX, donor->map.worldStartY, donor->map.worldEndY,
	             s_extMap.iconW, s_extMap.iconH);
	{
		int half;

		for (half = drawTop ? 0 : 1; half < 2; half++)
		{
			const ExtMapIconRect *r = (half == 0) ? &rt : &rb;
			const ExtMapClut *c = (half == 0) ? &clutTop : &clutBottom;

			Platform_Log("[CTR ExtMap] level %d: %s half: screen %dx%d, uv corners %d,%d %d,%d %d,%d %d,%d, texel box %d,%d %dx%d, VRAM %d,%d %dx%d, "
			             "clut 0x%04x: background %04x, line %04x, border %04x\n",
			             gGT->levelID, (half == 0) ? "top" : "bottom", r->sw, r->sh, r->cu[0], r->cv[0], r->cu[1], r->cv[1], r->cu[2], r->cv[2], r->cu[3],
			             r->cv[3], r->u0, r->v0, r->w, r->h, r->rect.x, r->rect.y, r->rect.w, r->rect.h, (half == 0) ? top->texLayout.clut : bottom->texLayout.clut,
			             c->bg & 0xffff, c->fill & 0xffff, (c->border >= 0) ? c->border : 0xffff);
		}
	}
}

// Every drawn frame of the slot (Engine_ExtTrackCollect): every 30th the player's
// marker, as UI_Map_DrawDrivers places it, against the line. Draw side only.
void ExtMap_Frame(const struct GameTracker *gGT)
{
	const struct Driver *d;
	double mx;
	double my;
	double dist;
	int x;
	int z;

	if (!s_extMap.ready || (gGT == NULL) || ((const void *)gGT->level1 != s_extMap.level1) || ((int)gGT->levelID != s_extMap.levelId))
	{
		return;
	}
	s_extMap.frames++;
	if ((s_extMap.frames % EXT_MAP_LOG_EVERY) != 0u)
	{
		return;
	}
	d = gGT->drivers[0];
	if ((d == NULL) || (d->instSelf == NULL))
	{
		return;
	}
	x = d->instSelf->matrix.t[0];
	z = d->instSelf->matrix.t[2];
	ExtMap_MarkerMiddle(&s_extMap.meta.map, x, z, s_extMap.iconW, s_extMap.iconH, &mx, &my);
	dist = ExtMap_LineDistance(&s_extMap.meta.map, (const s16(*)[2])s_extMap.line, s_extMap.points, s_extMap.iconW, s_extMap.iconH, mx, my);
	s_extMap.samples++;
	s_extMap.distSum += dist;
	s_extMap.distMax = (dist > s_extMap.distMax) ? dist : s_extMap.distMax;
	s_extMap.within6 += (dist <= 6.0) ? 1u : 0u;
	Platform_Log("[CTR ExtMap] frame %u vblank %d seat 0 world %d %d marker %.1f %.1f line distance %.2f\n", s_extMap.frames, Platform_GetVBlankCount(), x,
	             z, mx, my, dist);
}

// --------------------------------------------------------------------------
// Selftest (called by ExtJson_Selftest, --ext-selftest json)

int ExtMap_Selftest(int *n, int *failures)
{
	static const s16 square[4][2] = {{-4000, -6000}, {4000, -6000}, {4000, 6000}, {-4000, 6000}};
	struct UIMap m;
	int before = *failures;
	int px, py;
	int k;

#define EXT_MAP_CHECK(cond, what)                     \
	do                                                \
	{                                                 \
		(*n)++;                                       \
		if (!(cond))                                  \
		{                                             \
			(*failures)++;                            \
			printf("ext selftest map: %s\n", (what)); \
		}                                             \
	} while (0)

	// The formula of UI_Map_GetIconPos, by hand.
	memset(&m, 0, sizeof(m));
	m.worldStartX = -1000;
	m.worldEndX = 1000;
	m.worldStartY = -500;
	m.worldEndY = 500;
	m.iconSizeX = 100;
	m.iconSizeY = 40;
	m.iconStartX = 450;
	m.iconStartY = 150;
	m.mode = 0;
	ExtMap_IconPos(&m, 300, -250, &px, &py);
	EXT_MAP_CHECK((px == 450 + 15) && (py == 150 - 20 - 16), "mode 0 position");
	m.mode = 1;
	ExtMap_IconPos(&m, 300, -250, &px, &py);
	EXT_MAP_CHECK((px == 450 + 25) && (py == 150 + 12 - 16), "mode 1 position");
	m.mode = 2;
	ExtMap_IconPos(&m, 300, -250, &px, &py);
	EXT_MAP_CHECK((px == 450 - 15) && (py == 150 + 20 - 16), "mode 2 position");
	m.mode = 3;
	ExtMap_IconPos(&m, 300, -250, &px, &py);
	EXT_MAP_CHECK((px == 450 - 25) && (py == 150 - 12 - 16), "mode 3 position");
	m.mode = 0;
	ExtMap_IconPos(&m, -7, 0, &px, &py);
	EXT_MAP_CHECK(px == 450, "truncation towards zero as in C (-7 * 100 / 2000 = 0)");
	m.worldEndX = m.worldStartX;
	ExtMap_IconPos(&m, 300, 300, &px, &py);
	EXT_MAP_CHECK((px == 450) && (py == 150 - 16), "range 0 puts the marker at the start");
	m.worldEndX = 1000;
	EXT_MAP_CHECK(fabs(ExtMap_Aspect(&m) - (2.0 * 40.0 / 1000.0) / (100.0 / 2000.0)) < 1e-9, "aspect of a mode 0 table");

	// A tall square line in a wide picture: the fit turns it (mode 1 or 3) and keeps it inside.
	EXT_MAP_CHECK(ExtMap_Fit(square, 4, 436, 131, 64, 48, 3, 1.0, 8, 8, &m), "fit");
	EXT_MAP_CHECK((m.mode == 1) || (m.mode == 3), "a line taller than wide is turned for a wide picture");
	for (k = 0; k < 4; k++)
	{
		double mx, my;

		ExtMap_MarkerMiddle(&m, square[k][0], square[k][1], 8, 8, &mx, &my);
		EXT_MAP_CHECK((mx >= 436.0) && (mx <= 500.0) && (my >= 131.0) && (my <= 179.0), "every corner inside the picture");
		EXT_MAP_CHECK(ExtMap_LineDistance(&m, square, 4, 8, 8, mx, my) < 1e-9, "a corner lies on the line");
	}
	EXT_MAP_CHECK(!ExtMap_Fit(square, 1, 436, 131, 64, 48, 3, 0.5, 8, 8, &m) && !ExtMap_Fit(square, 4, 436, 131, 64, 48, 3, 0.0, 8, 8, &m),
	              "a line of one point or no aspect is refused");
	EXT_MAP_CHECK(ExtMap_Fit(square, 4, 436, 131, 64, 48, 3, 1.0, 8, 8, &m), "fit again");
	{
		static u8 pic[64 * 48];
		int set;
		double mx, my;
		int tx, ty;

		memset(pic, 0, sizeof(pic));
		set = ExtMap_Raster(pic, 64, 48, 436, 131, &m, square, 4, 8, 8, 2.5, 5, 7);
		EXT_MAP_CHECK(set > 40, "the line sets texels");
		ExtMap_MarkerMiddle(&m, 0, -6000, 8, 8, &mx, &my);
		tx = (int)mx - 436;
		ty = (int)my - 131;
		EXT_MAP_CHECK((tx >= 0) && (tx < 64) && (ty >= 0) && (ty < 48) && (pic[ty * 64 + tx] == 5), "the middle of an edge is line colour");
		ExtMap_MarkerMiddle(&m, 0, 0, 8, 8, &mx, &my);
		tx = (int)mx - 436;
		ty = (int)my - 131;
		EXT_MAP_CHECK(pic[ty * 64 + tx] == 0, "the middle of the square stays background");
	}
	// 4-bit texels in words that start mid-word.
	{
		ExtMapIconRect r;
		u16 words[4] = {0, 0, 0, 0};

		memset(&r, 0, sizeof(r));
		r.depth = 0;
		r.u0 = 2;
		r.w = 5;
		r.h = 1;
		r.rect.w = 2;
		ExtMap_SetTexel(&r, words, 0, 0, 0xa);
		ExtMap_SetTexel(&r, words, 2, 0, 0x3);
		EXT_MAP_CHECK((words[0] == 0x0a00) && (words[1] == 0x0003), "4-bit texels land in their nibbles");
		EXT_MAP_CHECK((ExtMap_GetTexel(&r, words, 0, 0) == 0xa) && (ExtMap_GetTexel(&r, words, 2, 0) == 0x3) && (ExtMap_GetTexel(&r, words, 1, 0) == 0),
		              "4-bit texels read back");
	}
	// The quad's UV corners: a plain half maps texel to pixel one to one, a mirrored one from the right.
	{
		ExtMapIconRect r;
		int sx = -1, sy = -1;

		memset(&r, 0, sizeof(r));
		r.sw = 10;
		r.sh = 4;
		r.cu[0] = 20, r.cv[0] = 100, r.cu[1] = 30, r.cv[1] = 100, r.cu[2] = 20, r.cv[2] = 104, r.cu[3] = 30, r.cv[3] = 104;
		r.u0 = 20, r.v0 = 100, r.w = 10, r.h = 4;
		EXT_MAP_CHECK(ExtMap_TexelToScreen(&r, 3, 2, &sx, &sy) && (sx == 3) && (sy == 2), "plain half: texel 3,2 is pixel 3,2");
		r.cu[0] = 30, r.cu[1] = 20, r.cu[2] = 30, r.cu[3] = 20;
		EXT_MAP_CHECK(ExtMap_TexelToScreen(&r, 3, 2, &sx, &sy) && (sx == 6) && (sy == 2), "mirrored half: texel 3,2 is pixel 6,2");
	}
	// Two 2-bit pictures in one 4-bit one (the retail way, Sunset Vista: the top table shows bits 2-3, the bottom one bits 0-1).
	{
		static const u16 shades[4] = {0x8000, 0x7fff, 0xa108, 0xc210};
		static ExtMapClut ct, cb;
		int i;

		memset(&ct, 0, sizeof(ct));
		memset(&cb, 0, sizeof(cb));
		ct.n = cb.n = 16;
		for (i = 0; i < 16; i++)
		{
			ct.colour[i] = shades[i >> 2];
			cb.colour[i] = shades[i & 3];
		}
		ct.bg = cb.bg = 0x8000;
		ct.fill = cb.fill = 0x7fff;
		ct.border = cb.border = 0xa108;
		EXT_MAP_CHECK(ExtMap_PickIndex(&ct, 1, &cb, 0, 0) == 4, "top line over bottom background: index 4");
		EXT_MAP_CHECK(ExtMap_PickIndex(&ct, 0, &cb, 1, 0) == 1, "top background over bottom line: index 1");
		EXT_MAP_CHECK(ExtMap_PickIndex(&ct, 1, &cb, 2, 0) == 6, "top line over bottom border: index 6");
		EXT_MAP_CHECK(ExtMap_PickIndex(&ct, -1, &cb, 1, 13) == 13, "only the bottom shows it: the index kept when it fits");
		EXT_MAP_CHECK(ExtMap_PickIndex(&ct, 0, &cb, 0, 0) == 0, "both background: index 0 kept");
	}
#undef EXT_MAP_CHECK
	return *failures - before;
}
