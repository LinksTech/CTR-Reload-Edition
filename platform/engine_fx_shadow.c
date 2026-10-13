#include <platform/engine.h>

// ENGINE FX SHADOW - the ground shadow of custom drivers drawn by the engine (step 4).
// See include/platform/engine.h, EFFECTS OF CUSTOM DRIVERS.
//
// The retail code (game/Vehicle/VehGroundShadow.c, VehGroundShadow_Main) builds
// nine points per driver and view in a CAMERA-RELATIVE space: s16 positions
// (driver position minus the camera position, times 4) plus the rotated local
// axes of the shadow. It projects them with the view ROTATION only (the GTE
// translation is zero there), then emits four POLY_FT4 quads from the points
// (8,0,1,2) (8,0,7,6) (4,0,3,2) (4,0,5,6), alternating the two shadow icons,
// all with the same colour (code 0x2e: textured, semi-transparent), all linked
// into the OT cell (depth of point 0 >> 8) + depthBias, clamped to 0..0x3ff.
// The tpage of the icons carries semi-transparency mode 2 (B - F).
//
// The engine draws the same: the four quads as dynamic geometry in that same
// space, with a camera whose translation is zeroed, in the same cell.

#define ENG_SHADOW_QUADS 4
#define ENG_SHADOW_OT_MAX 0x3ff
#define ENG_SHADOW_CACHE 16

global_variable const u8 s_engShadowQuadPoint[ENG_SHADOW_QUADS][4] = {
    {8, 0, 1, 2},
    {8, 0, 7, 6},
    {4, 0, 3, 2},
    {4, 0, 5, 6},
};

typedef struct
{
	EngineMaterial material;
	int index;
} EngineShadowMat;

global_variable EngineShadowMat s_engShadowMat[ENG_SHADOW_CACHE];
global_variable int s_engShadowMatCount = 0;

// The material of (tpage, clut): VRAM source, semi-transparency of the tpage,
// only STP texels blend (the PS1 rule for a semi-transparent textured prim).
// Cached; before every use the table entry is checked (the table is reset at
// every level change) and registered again when it differs.
internal int EngineFx_ShadowMaterial(u16 tpage, u16 clut)
{
	EngineMaterial want;
	int i;
	int abr = (int)((tpage >> 5) & 3u);

	memset(&want, 0, sizeof(want));
	want.texSource = (u8)ENGINE_TEX_VRAM;
	want.blend = (u8)Engine_BlendFromAbr(abr);
	want.flags = (u16)(ENGINE_MATERIAL_SEMI_STP | ENGINE_MATERIAL_DOUBLE_SIDED);
	want.tpage = tpage;
	want.clut = clut;
	want.tint[0] = want.tint[1] = want.tint[2] = want.tint[3] = 1.0f;
	want.depthBias = ENGINE_GROUND_DEPTH_BIAS; // on the ground: in front of an engine track

	for (i = 0; i < s_engShadowMatCount; i++)
	{
		EngineShadowMat *c = &s_engShadowMat[i];
		const EngineMaterial *cur;

		if ((c->material.tpage != tpage) || (c->material.clut != clut))
		{
			continue;
		}
		cur = Engine_Material(c->index);
		if ((cur != NULL) && (memcmp(cur, &want, sizeof(want)) == 0))
		{
			return c->index;
		}
		// Stale (level change): register again into this slot.
		c->index = Engine_MaterialRegister(&want);
		c->material = want;
		return c->index;
	}

	{
		int index = Engine_MaterialRegister(&want);

		if ((index >= 0) && (s_engShadowMatCount < ENG_SHADOW_CACHE))
		{
			s_engShadowMat[s_engShadowMatCount].material = want;
			s_engShadowMat[s_engShadowMatCount].index = index;
			s_engShadowMatCount++;
		}
		return index;
	}
}

// points: the retail nine points (camera-relative scaled space, s16);
// tex0/tex1: the two shadow icons as VehGroundShadow_Subset1 made them;
// color: the retail colour word (low byte = intensity); depth: the view depth
// of point 0 as the GTE gave it (SZ); depthBias: entry->depthBias; ot: the
// view's OT (pb->ptrOT). Called only for an owned driver, after the retail
// code ran all its checks and the projection.
void EngineFx_Shadow(const struct Driver *d, int view, const SVec3 points[9], const struct TextureLayout *tex0, const struct TextureLayout *tex1,
                     u32 color, int depth, int depthBias, u32 *ot)
{
	const EngineCamera *cam;
	EngineCamera local;
	EngineMat4 screenFromWorld;
	int cell;
	int q;
	int submitted = 0;
	u8 colour[4];

	(void)d;
	if (!g_cfg_engine || (ot == NULL) || (points == NULL) || (tex0 == NULL) || (tex1 == NULL))
	{
		return;
	}
	cam = Engine_FrameCamera(view);
	if ((cam == NULL) || !cam->valid)
	{
		return;
	}

	// The retail projection has translation zero: rotation only.
	local = *cam;
	local.viewFromWorld.m[12] = 0.0f;
	local.viewFromWorld.m[13] = 0.0f;
	local.viewFromWorld.m[14] = 0.0f;
	Engine_CameraScreenFromWorld(&local, &screenFromWorld);

	cell = (depth >> 8) + depthBias;
	if (cell < 0)
	{
		cell = 0;
	}
	else if (cell > ENG_SHADOW_OT_MAX)
	{
		cell = ENG_SHADOW_OT_MAX;
	}

	colour[0] = (u8)(color & 0xffu);
	colour[1] = (u8)((color >> 8) & 0xffu);
	colour[2] = (u8)((color >> 16) & 0xffu);
	colour[3] = 255;

	for (q = 0; q < ENG_SHADOW_QUADS; q++)
	{
		static const u16 idx[6] = {0, 1, 2, 1, 3, 2};
		const struct TextureLayout *tex = (q & 1) ? tex1 : tex0;
		const float us[4] = {(float)tex->u0, (float)tex->u1, (float)tex->u2, (float)tex->u3};
		const float vs[4] = {(float)tex->v0, (float)tex->v1, (float)tex->v2, (float)tex->v3};
		EngineVertex v[4];
		EngineDraw draw;
		int i;
		int mat = EngineFx_ShadowMaterial(tex->tpage, tex->clut);

		if (mat < 0)
		{
			continue;
		}
		for (i = 0; i < 4; i++)
		{
			const SVec3 *p = &points[s_engShadowQuadPoint[q][i]];

			v[i].position[0] = (float)p->x;
			v[i].position[1] = (float)p->y;
			v[i].position[2] = (float)p->z;
			v[i].texcoord[0] = us[i];
			v[i].texcoord[1] = vs[i];
			memcpy(v[i].color, colour, 4);
		}

		memset(&draw, 0, sizeof(draw));
		draw.mesh = -1;
		draw.materialOverride = -1;
		draw.clipFromModel = screenFromWorld;
		draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
		draw.sortDepth = (float)depth;
		draw.otCell = (u32)cell;
		draw.flags = ENGINE_DRAW_TRANSPARENT;
		draw.dynVertices = v;
		draw.dynVertexCount = 4;
		draw.dynIndices = idx;
		draw.dynIndexCount = 6;
		draw.dynMaterial = mat;
		if (Engine_RenderSubmitLinked(view, &draw, ot, cell))
		{
			submitted++;
		}
	}
	if (submitted > 0)
	{
		Engine_FxNoteDrawn(ENGINE_FX_SHADOW);
	}
}
