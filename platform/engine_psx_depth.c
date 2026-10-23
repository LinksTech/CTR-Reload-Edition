#include <platform/engine.h>

// ENGINE PSX DEPTH - the cell of a PS1 world primitive in a frame the engine
// drew the track in (see include/platform/engine.h, PSX DEPTH).
//
// THE WALK. ParsePrimitivesLinkedList walks the ordering table of the frame
// from the far end: the world table of view 0 (cells 0x3ff down to 0), then
// the UI table, then the root. Each entry of the world table is a packet of
// length 0 at its own address, pushBuffer[0].ptrOT + c; whatever the walk
// meets after entry c and before entry c - 1 was linked into cell c - the
// primitives hung there directly and the whole sub-range of every instance the
// queue linked there (RenderBucket_AllocateOTRange writes the cell's head into
// the end of the range, the range's start back into the cell), whose own
// entries lie outside the table and only mark that the walk is inside a
// sub-range now.
//
// A FRAME OF THE ENGINE: one player, the engine on, the track drawn by the
// engine in this frame (Engine_TrackDrawnThisFrame, taken at the start of the
// walk), and an engine marker of view 0 already parsed in this walk - the sky
// (0x3ff) or the track (0x3fe), both before any cell a PS1 world primitive can
// have below them. A menu with the engine's menu scene, two to four players
// and the PS1 path never get a cell.
//
// THE RANGES. The queue registers each range it links into the world table
// (Engine_PsxDepthNoteRange): its entries and the nearest view z of the
// instance. The queue allocates them one after another from the same arena, so
// they come in ascending address order and a sub-range entry the walk meets is
// found by a binary search (the last hit is tried first: a range's entries come
// one after another). The table is started afresh with the first range of a
// new game frame (sdata->frameCounter).

typedef struct
{
	int active;        // the walk runs over the world table of a frame of the engine
	int engineSeen;    // an engine marker of view 0 was parsed in this walk
	int cell;          // the cell of the last entry of the world table, -1 before the first
	int inSubRange;    // a sub-range entry was met after that entry
	float rangeNear;   // the nearest view z of that sub-range's instance, 0 = not registered
	int rangeSecond;   // that sub-range is the instance's second one
	float primVz;      // the view z a producer registered for the packet being parsed, 0 = none
	float zNear;       // of the camera of view 0
	const u32 *table;  // pushBuffer[0].ptrOT of the walk
} EnginePsxDepthWalk;

internal EnginePsxDepthWalk s_epdWalk;

int g_cfg_psxDepth = 1;

typedef struct
{
	uintptr_t first; // address of the range's first entry
	uintptr_t last;  // address of its last entry
	float nearZ;     // nearest view z of the instance
	int secondary;   // the instance's second range
} EnginePsxDepthRange;

typedef struct
{
	uintptr_t first; // the first primitive
	uintptr_t end;   // behind the last one
	float vz;
} EnginePsxDepthPrims;

internal EnginePsxDepthPrims s_epdPrims[ENGINE_PSX_DEPTH_PRIMS_MAX];
internal int s_epdPrimCount;
internal int s_epdPrimFrame = -1;
internal u32 s_epdPrimsDropped;

internal EnginePsxDepthRange s_epdRanges[ENGINE_PSX_DEPTH_RANGES_MAX];
internal int s_epdRangeCount;
internal int s_epdRangeFrame = -1;
internal int s_epdRangeSorted;
internal int s_epdRangeLast;
internal u32 s_epdRangesDropped; // a frame with more ranges than the table holds (they keep no depth test)
internal u32 s_epdSplitsWithDepth;
internal int s_epdReportOn;

internal void Engine_PsxDepthReport(void)
{
	Platform_Log("[CTR PsxDepth] at exit: %s; %u PS1 split(s) drawn with depth, %u range(s) and %u primitive group(s) beyond the table\n",
	             g_cfg_psxDepth ? "on" : "off (--no-psx-depth or the debug menu)", s_epdSplitsWithDepth, s_epdRangesDropped, s_epdPrimsDropped);
}

internal void Engine_PsxDepthReportOn(void)
{
	if (!s_epdReportOn)
	{
		s_epdReportOn = 1;
		Platform_AtExitReport(Engine_PsxDepthReport);
	}
}

internal int Engine_PsxDepthRecording(void)
{
	return g_cfg_engine && (sdata != NULL) && (sdata->gGT != NULL) && (sdata->gGT->numPlyrCurrGame == 1);
}

void Engine_PsxDepthNotePrims(const void *first, const void *end, float vz)
{
	EnginePsxDepthPrims *g;

	if (!Engine_PsxDepthRecording() || (first == NULL) || ((uintptr_t)end <= (uintptr_t)first))
	{
		return;
	}
	if (s_epdPrimFrame != sdata->frameCounter)
	{
		s_epdPrimFrame = sdata->frameCounter;
		s_epdPrimCount = 0;
	}
	if (s_epdPrimCount >= ENGINE_PSX_DEPTH_PRIMS_MAX)
	{
		s_epdPrimsDropped++;
		return;
	}
	g = &s_epdPrims[s_epdPrimCount++];
	g->first = (uintptr_t)first;
	g->end = (uintptr_t)end;
	g->vz = vz;
	Engine_PsxDepthReportOn();
}

// The registered view z of a primitive hung into its cell directly, 0 = none.
// A handful per frame (four quads per retail driver): searched from the end.
internal float Engine_PsxDepthFindPrim(uintptr_t address)
{
	for (int i = s_epdPrimCount - 1; i >= 0; i--)
	{
		if ((address >= s_epdPrims[i].first) && (address < s_epdPrims[i].end))
		{
			return s_epdPrims[i].vz;
		}
	}
	return 0.0f;
}

void Engine_PsxDepthNoteRange(u32 activeRange, int minDepth, int maxDepth, int viewDepth, u32 instFlags, int secondary)
{
	EnginePsxDepthRange *r;
	int shift;
	float unit;

	if (!Engine_PsxDepthRecording() || (activeRange == 0u) || (maxDepth < minDepth))
	{
		return;
	}
	if (s_epdRangeFrame != sdata->frameCounter)
	{
		s_epdRangeFrame = sdata->frameCounter;
		s_epdRangeCount = 0;
		s_epdRangeSorted = 1;
		s_epdRangeLast = 0;
	}
	if (s_epdRangeCount >= ENGINE_PSX_DEPTH_RANGES_MAX)
	{
		s_epdRangesDropped++;
		return;
	}
	// The units of the bins: the queue projects an instance nearer than view z
	// 0x1000 four times as large and a DRAW_HUGE one a quarter as large
	// (RenderBucket_AdjustViewPositionForMvp); a bin is 32 of those units
	// (minZ >> 5). minDepth is two bins in front of the nearest corner.
	shift = ((viewDepth < 0x1000) ? 2 : 0) - (((instFlags & DRAW_HUGE) != 0u) ? 2 : 0);
	unit = (shift >= 0) ? (32.0f / (float)(1 << shift)) : (32.0f * (float)(1 << -shift));
	r = &s_epdRanges[s_epdRangeCount];
	// activeRange = first entry - minDepth entries (RenderBucket_AllocateOTRange).
	r->first = (uintptr_t)activeRange + (uintptr_t)((u32)minDepth * sizeof(u32));
	r->last = r->first + (uintptr_t)((u32)(maxDepth - minDepth) * sizeof(u32));
	r->nearZ = (float)(minDepth + 2) * unit;
	r->secondary = secondary;
	if ((s_epdRangeCount > 0) && (r->first <= s_epdRanges[s_epdRangeCount - 1].first))
	{
		s_epdRangeSorted = 0;
	}
	s_epdRangeCount++;
	Engine_PsxDepthReportOn();
}

// The registered range holding this entry, or -1.
internal int Engine_PsxDepthFindRange(uintptr_t address)
{
	int lo = 0;
	int hi = s_epdRangeCount - 1;

	if ((s_epdRangeLast < s_epdRangeCount) && (address >= s_epdRanges[s_epdRangeLast].first) && (address <= s_epdRanges[s_epdRangeLast].last))
	{
		return s_epdRangeLast;
	}
	if (!s_epdRangeSorted)
	{
		for (int i = 0; i < s_epdRangeCount; i++)
		{
			if ((address >= s_epdRanges[i].first) && (address <= s_epdRanges[i].last))
			{
				return s_epdRangeLast = i;
			}
		}
		return -1;
	}
	while (lo <= hi)
	{
		const int mid = (lo + hi) / 2;

		if (address < s_epdRanges[mid].first)
		{
			hi = mid - 1;
		}
		else if (address > s_epdRanges[mid].last)
		{
			lo = mid + 1;
		}
		else
		{
			return s_epdRangeLast = mid;
		}
	}
	return -1;
}

int g_cfg_psxDepthCalibCount = 0;
int g_cfg_psxDepthCalib[ENGINE_PSX_DEPTH_CALIB_MAX];

// MEASURING: the next vblank of the list, and whether this frame logs.
internal int s_epdCalibNext;
internal int s_epdCalibArmed;
internal u32 s_epdCalibLines[3]; // primitives' triangles, instances, engine triangles of the armed frame

void Engine_PsxDepthFrameBegin(struct GameTracker *gGT)
{
	const EngineCamera *cam;

	s_epdCalibArmed = 0;
	if ((gGT == NULL) || (s_epdCalibNext >= g_cfg_psxDepthCalibCount) || (Platform_GetVBlankCount() < g_cfg_psxDepthCalib[s_epdCalibNext]))
	{
		return;
	}
	s_epdCalibArmed = 1;
	s_epdCalibLines[0] = s_epdCalibLines[1] = s_epdCalibLines[2] = 0;
	cam = Engine_FrameCamera(0);
	Platform_Log("[CTR PsxDepth] calib frame %d vblank %d (asked for %d): players %d, camera %s, H %.1f, zNear %.3f, rect %dx%d\n", (int)sdata->frameCounter,
	             Platform_GetVBlankCount(), g_cfg_psxDepthCalib[s_epdCalibNext], (int)gGT->numPlyrCurrGame, (cam != NULL) ? "valid" : "none",
	             (cam != NULL) ? cam->H : 0.0f, (cam != NULL) ? cam->zNear : 0.0f, (cam != NULL) ? cam->rectW : 0, (cam != NULL) ? cam->rectH : 0);
	s_epdCalibNext++;
}

int Engine_PsxDepthCalibArmed(void)
{
	return s_epdCalibArmed;
}

void Engine_PsxDepthWalkBegin(void)
{
	const struct GameTracker *gGT = (sdata != NULL) ? sdata->gGT : NULL;
	const EngineCamera *cam;

	memset(&s_epdWalk, 0, sizeof(s_epdWalk));
	s_epdWalk.cell = -1;
	if (!g_cfg_engine || (gGT == NULL) || (gGT->numPlyrCurrGame != 1) || (gGT->pushBuffer[0].ptrOT == NULL) || !Engine_TrackDrawnThisFrame())
	{
		return;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		return;
	}
	s_epdWalk.active = 1;
	s_epdWalk.zNear = cam->zNear;
	s_epdWalk.table = (const u32 *)gGT->pushBuffer[0].ptrOT;
}

void Engine_PsxDepthWalkEnd(void)
{
	if (s_epdCalibArmed && s_epdWalk.active)
	{
		Platform_Log("[CTR PsxDepth] calib end: %u primitive triangle(s), %u retail instance(s), %u engine triangle(s)\n", s_epdCalibLines[0],
		             s_epdCalibLines[1], s_epdCalibLines[2]);
		s_epdCalibArmed = 0;
	}
	memset(&s_epdWalk, 0, sizeof(s_epdWalk));
	s_epdWalk.cell = -1;
}

void Engine_PsxDepthMarker(u32 item)
{
	// Item layout: ENGINE_MARKER_ITEM (view in bits 16-17).
	if (s_epdWalk.active && (((item >> 16) & 3u) == 0u))
	{
		s_epdWalk.engineSeen = 1;
	}
}

void Engine_PsxDepthPacket(const void *packet, int length)
{
	uintptr_t address;
	uintptr_t first;

	if (!s_epdWalk.active)
	{
		return;
	}
	address = (uintptr_t)packet;
	if (length != 0)
	{
		// A primitive: inside a sub-range it takes the range's depth, hung
		// into the cell directly the depth its producer registered.
		s_epdWalk.primVz = s_epdWalk.inSubRange ? 0.0f : Engine_PsxDepthFindPrim(address);
		return;
	}
	first = (uintptr_t)s_epdWalk.table;
	if ((address >= first) && (address < first + (ENGINE_PSX_DEPTH_CELL_SKY + 1) * sizeof(u32)))
	{
		s_epdWalk.cell = (int)((address - first) / sizeof(u32));
		s_epdWalk.inSubRange = 0;
		s_epdWalk.rangeNear = 0.0f;
		return;
	}
	{
		const int found = Engine_PsxDepthFindRange(address);

		// The range's first entry is the bin minDepth, two bins in front of
		// the instance's nearest corner and so empty; it links on to what the
		// cell held before the range was hung in (rangeStart[0] = the cell's old
		// head, RenderBucket_AllocateOTRange): from here on the walk is back in
		// the cell itself.
		if ((found >= 0) && (address == s_epdRanges[found].first))
		{
			s_epdWalk.inSubRange = 0;
			s_epdWalk.rangeNear = 0.0f;
			return;
		}
		s_epdWalk.rangeNear = (found >= 0) ? s_epdRanges[found].nearZ : 0.0f;
		s_epdWalk.rangeSecond = (found >= 0) ? s_epdRanges[found].secondary : 0;
	}
	s_epdWalk.inSubRange = 1;
}

int Engine_PsxDepthCell(void)
{
	if (!s_epdWalk.active || !s_epdWalk.engineSeen || (s_epdWalk.cell <= 0) || (s_epdWalk.cell >= ENGINE_PSX_DEPTH_CELL_SKY))
	{
		return -1;
	}
	return s_epdWalk.cell;
}

int Engine_PsxDepthInSubRange(void)
{
	return s_epdWalk.inSubRange;
}

float Engine_PsxDepthNow(void)
{
	float vz;

	if (!g_cfg_psxDepth || (Engine_PsxDepthCell() < 0) || (s_epdWalk.zNear <= 0.0f))
	{
		return 0.0f;
	}
	if (s_epdWalk.inSubRange)
	{
		if (s_epdWalk.rangeNear <= 0.0f)
		{
			return 0.0f;
		}
		vz = s_epdWalk.rangeNear - (s_epdWalk.rangeSecond ? ENGINE_PSX_DEPTH_MARGIN_SECOND : ENGINE_PSX_DEPTH_MARGIN_RANGE);
	}
	else
	{
		if (s_epdWalk.primVz <= 0.0f)
		{
			return 0.0f;
		}
		vz = s_epdWalk.primVz - ENGINE_PSX_DEPTH_MARGIN_PRIM;
	}
	if (vz <= s_epdWalk.zNear)
	{
		return 1.0f;
	}
	return s_epdWalk.zNear / vz;
}

// A PS1 split drawn with depth (native_gpu.c DrawSplit), for the exit line.
void Engine_PsxDepthNoteSplit(void)
{
	s_epdSplitsWithDepth++;
}

void Engine_PsxDepthCalibWalk(float *rangeNear, int *secondary, float *primVz, float *depth)
{
	*rangeNear = s_epdWalk.inSubRange ? s_epdWalk.rangeNear : 0.0f;
	*secondary = s_epdWalk.inSubRange ? s_epdWalk.rangeSecond : 0;
	*primVz = s_epdWalk.inSubRange ? 0.0f : s_epdWalk.primVz;
	*depth = Engine_PsxDepthNow();
}

// The calibration line of a triangle the walk built (native_gpu.c counts it here).
void Engine_PsxDepthCalibNoteTriangle(void)
{
	s_epdCalibLines[0]++;
}

// MEASURING: a retail instance of view 0. Its cell as the queue computes it
// (RenderBucket_AllocateOTRange: (viewDepth >> 6) + depthBiasNormal, clamped
// to 0..0x3ff), its view z through the engine camera, and its depth range
// (RenderBucket_BuildDepthRange: depthOffset = minZ >> 5 - 2 .. maxZ >> 5 + 1 in
// the units of the projection the queue chose: four times for an instance
// nearer than view z 0x1000, a quarter for a DRAW_HUGE one; see
// RenderBucket_AdjustViewPositionForMvp).
void Engine_PsxDepthCalibInstance(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb)
{
	const EngineCamera *cam;
	EngineVec3 world;
	EngineVec3 v;
	int viewDepth;
	int cell;
	int shift;
	float unit;

	if (!s_epdCalibArmed || (inst == NULL) || (idpp == NULL) || (pb != &sdata->gGT->pushBuffer[0]) || ((cam = Engine_FrameCamera(0)) == NULL))
	{
		return;
	}
	world.x = (float)inst->matrix.t[0];
	world.y = (float)inst->matrix.t[1];
	world.z = (float)inst->matrix.t[2];
	v = Engine_Mat4MulPoint(&cam->viewFromWorld, world);
	viewDepth = (int)v.z;
	cell = (viewDepth >> 6) + (int)(s8)inst->depthBiasNormal;
	cell = (cell < 0) ? 0 : ((cell > ENGINE_PSX_DEPTH_CELL_SKY) ? ENGINE_PSX_DEPTH_CELL_SKY : cell);
	shift = ((viewDepth < 0x1000) ? 2 : 0) - (((inst->flags & DRAW_HUGE) != 0) ? 2 : 0);
	unit = (shift >= 0) ? (32.0f / (float)(1 << shift)) : (32.0f * (float)(1 << -shift));
	Platform_Log("[CTR PsxDepth] I %s cell %d vz %.1f near %.1f far %.1f bias %d shift %d flags %08x\n", (inst->model != NULL) ? inst->model->name : "?", cell,
	             v.z, (float)(idpp->depthOffset[0] + 2) * unit, (float)(idpp->depthOffset[1] - 1) * unit, (int)(s8)inst->depthBiasNormal, shift,
	             (unsigned)inst->flags);
	s_epdCalibLines[1]++;
}

// MEASURING: the triangles of a dynamic engine draw of view 0 that tests and
// writes depth, each corner as the screen point of clipFromModel (x / w, y / w)
// and its view z (w).
void Engine_PsxDepthCalibDraw(int view, const EngineDraw *draw)
{
	const EngineMaterial *material;
	const float *m;

	if (!s_epdCalibArmed || (view != 0) || (draw == NULL) || (draw->dynVertices == NULL) || (draw->dynIndices == NULL) ||
	    ((draw->flags & (ENGINE_DRAW_NO_DEPTH | ENGINE_DRAW_TRANSPARENT)) != 0u))
	{
		return;
	}
	material = Engine_RenderMaterialAt(draw->dynMaterial);
	if ((material == NULL) || ((material->flags & ENGINE_MATERIAL_NO_DEPTH_WRITE) != 0u) || (material->blend != ENGINE_BLEND_OPAQUE))
	{
		return;
	}
	m = draw->clipFromModel.m;
	for (u32 t = 0; t + 2u < draw->dynIndexCount; t += 3u)
	{
		float c[3][3];
		int behind = 0;

		for (int k = 0; k < 3; k++)
		{
			const float *p = draw->dynVertices[draw->dynIndices[t + (u32)k]].position;
			const float x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
			const float y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
			const float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];

			if (w <= 1.0f)
			{
				behind = 1;
				break;
			}
			c[k][0] = x / w;
			c[k][1] = y / w;
			c[k][2] = w;
		}
		if (behind)
		{
			continue;
		}
		Platform_Log("[CTR PsxDepth] T %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n", c[0][0], c[0][1], c[0][2], c[1][0], c[1][1], c[1][2], c[2][0], c[2][1],
		             c[2][2]);
		s_epdCalibLines[2]++;
	}
}
