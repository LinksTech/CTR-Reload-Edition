#include <platform/engine.h>
#include <math.h>
#include <stdio.h>

// ENGINE FX - CPU geometry builders for effects (see include/platform/engine.h).
//
// Common rules
//   - The colour is copied into every vertex; the builders return the number
//     of vertices written (4 for the quads) or 0.
//   - Indices are written relative to the first vertex of the call, plus
//     baseIndex (the index of that first vertex in the caller buffer).
//   - Quad vertex order v0..v3, indices {0,1,2, 0,2,3} + baseIndex, texcoords
//     v0 (0,0), v1 (1,0), v2 (1,1), v3 (0,1).
//   - NULL pointers or non-positive sizes give 0.
//   - Own small vector helpers only; no dependency on engine_math/camera.
//
// Engine_FxBillboard
//   The camera right and up axes in world space are the first and second
//   ROWS of the 3x3 rotation part of cam->viewFromWorld (column-major, so row r
//   is m[r], m[4 + r], m[8 + r]); each row is normalised because the game
//   matrix scales the Y row for the aspect. Screen y grows DOWN, so "up on
//   screen" is -row1. (right, up) are rotated by roll (4096 = 360 degrees,
//   positive = clockwise on screen): right' = right*cos - up*sin,
//   up' = up*cos + right*sin. Then
//     v0 = c - right*halfW + up*halfH   (top-left on screen)
//     v1 = c + right*halfW + up*halfH   (top-right)
//     v2 = c + right*halfW - up*halfH   (bottom-right)
//     v3 = c - right*halfW - up*halfH   (bottom-left)
//
// Engine_FxGroundQuad
//   n = normalise(normal) (zero -> 0); f' = normalise(forward - n*dot(forward,n))
//   (degenerate -> 0); s = normalise(cross(n, f')). With c the centre:
//     v0 = c + f'*halfLength + s*halfWidth
//     v1 = c + f'*halfLength - s*halfWidth
//     v2 = c - f'*halfLength - s*halfWidth
//     v3 = c - f'*halfLength + s*halfWidth
//
// Engine_FxRibbon
//   points >= 2. Vertex 2i = left[i], 2i+1 = right[i]. Texcoord u = uScale *
//   (distance along the CENTRE line, the midpoints of left/right, from point 0
//   to point i), v = 0 for left and 1 for right. Colour = colour[i] (colour
//   NULL = white). Indices per segment i (0..points-2):
//   {2i, 2i+1, 2i+3, 2i, 2i+3, 2i+2} + baseIndex. Returns 2*points, or 0 if
//   maxVertices < 2*points, maxIndices < 6*(points-1), any pointer is NULL, or
//   2*points - 1 + baseIndex > 65535.

internal EngineVec3 EngineFx_V3(float x, float y, float z)
{
	EngineVec3 v;
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

internal EngineVec3 EngineFx_Add(EngineVec3 a, EngineVec3 b)
{
	return EngineFx_V3(a.x + b.x, a.y + b.y, a.z + b.z);
}

internal EngineVec3 EngineFx_Sub(EngineVec3 a, EngineVec3 b)
{
	return EngineFx_V3(a.x - b.x, a.y - b.y, a.z - b.z);
}

internal EngineVec3 EngineFx_Scale(EngineVec3 a, float s)
{
	return EngineFx_V3(a.x * s, a.y * s, a.z * s);
}

internal float EngineFx_Dot(EngineVec3 a, EngineVec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

internal EngineVec3 EngineFx_Cross(EngineVec3 a, EngineVec3 b)
{
	return EngineFx_V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

// Normalises in place; returns 0 when the vector is too short (or not finite).
internal int EngineFx_Normalise(EngineVec3 *v)
{
	float len = sqrtf(EngineFx_Dot(*v, *v));
	if (!(len > 1e-12f))
	{
		return 0;
	}
	*v = EngineFx_Scale(*v, 1.0f / len);
	return 1;
}

internal void EngineFx_SetVertex(EngineVertex *v, EngineVec3 p, float u, float t, const u8 colour[4])
{
	v->position[0] = p.x;
	v->position[1] = p.y;
	v->position[2] = p.z;
	v->texcoord[0] = u;
	v->texcoord[1] = t;
	v->color[0] = colour[0];
	v->color[1] = colour[1];
	v->color[2] = colour[2];
	v->color[3] = colour[3];
	// Texcoords 0..1 of the quad or ribbon, no VRAM page: the whole page as UV box
	// (engine.h, THE UV BOX). A caller that puts page texels in instead sets the
	// box of its corners after them (Engine_VertexBoxOfCorners).
	Engine_VertexBoxFull(v, 1);
}

// Writes the 4 quad vertices (corner order v0..v3) and the 6 indices.
internal int EngineFx_WriteQuad(const EngineVec3 corner[4], const u8 colour[4], EngineVertex *outV, u16 *outI, u16 baseIndex)
{
	static const float us[4] = {0.0f, 1.0f, 1.0f, 0.0f};
	static const float vs[4] = {0.0f, 0.0f, 1.0f, 1.0f};
	static const u16 idx[6] = {0, 1, 2, 0, 2, 3};
	int i;
	for (i = 0; i < 4; i++)
	{
		EngineFx_SetVertex(&outV[i], corner[i], us[i], vs[i], colour);
	}
	for (i = 0; i < 6; i++)
	{
		outI[i] = (u16)(idx[i] + baseIndex);
	}
	return 4;
}

int Engine_FxBillboard(const EngineCamera *cam, EngineVec3 centre, float halfW, float halfH, s16 roll, const u8 colour[4], EngineVertex *outV, u16 *outI, u16 baseIndex)
{
	EngineVec3 right, up, r2, u2, corner[4];
	float angle, c, s;

	if (!cam || !colour || !outV || !outI || !(halfW > 0.0f) || !(halfH > 0.0f))
	{
		return 0;
	}
	right = EngineFx_V3(cam->viewFromWorld.m[0], cam->viewFromWorld.m[4], cam->viewFromWorld.m[8]);
	up = EngineFx_V3(-cam->viewFromWorld.m[1], -cam->viewFromWorld.m[5], -cam->viewFromWorld.m[9]);
	if (!EngineFx_Normalise(&right) || !EngineFx_Normalise(&up))
	{
		return 0;
	}
	angle = (float)roll * (6.28318530717958647692f / 4096.0f);
	c = cosf(angle);
	s = sinf(angle);
	if (roll == 0)
	{
		c = 1.0f;
		s = 0.0f;
	}
	r2 = EngineFx_Sub(EngineFx_Scale(right, c), EngineFx_Scale(up, s));
	u2 = EngineFx_Add(EngineFx_Scale(up, c), EngineFx_Scale(right, s));
	right = EngineFx_Scale(r2, halfW);
	up = EngineFx_Scale(u2, halfH);
	corner[0] = EngineFx_Add(EngineFx_Sub(centre, right), up);
	corner[1] = EngineFx_Add(EngineFx_Add(centre, right), up);
	corner[2] = EngineFx_Sub(EngineFx_Add(centre, right), up);
	corner[3] = EngineFx_Sub(EngineFx_Sub(centre, right), up);
	return EngineFx_WriteQuad(corner, colour, outV, outI, baseIndex);
}

int Engine_FxGroundQuad(EngineVec3 centre, EngineVec3 normal, EngineVec3 forward, float halfLength, float halfWidth, const u8 colour[4], EngineVertex *outV, u16 *outI, u16 baseIndex)
{
	EngineVec3 n = normal, f, s, fl, sw, corner[4];

	if (!colour || !outV || !outI || !(halfLength > 0.0f) || !(halfWidth > 0.0f))
	{
		return 0;
	}
	if (!EngineFx_Normalise(&n))
	{
		return 0;
	}
	f = EngineFx_Sub(forward, EngineFx_Scale(n, EngineFx_Dot(forward, n)));
	if (!EngineFx_Normalise(&f))
	{
		return 0;
	}
	s = EngineFx_Cross(n, f);
	if (!EngineFx_Normalise(&s))
	{
		return 0;
	}
	fl = EngineFx_Scale(f, halfLength);
	sw = EngineFx_Scale(s, halfWidth);
	corner[0] = EngineFx_Add(EngineFx_Add(centre, fl), sw);
	corner[1] = EngineFx_Sub(EngineFx_Add(centre, fl), sw);
	corner[2] = EngineFx_Sub(EngineFx_Sub(centre, fl), sw);
	corner[3] = EngineFx_Add(EngineFx_Sub(centre, fl), sw);
	return EngineFx_WriteQuad(corner, colour, outV, outI, baseIndex);
}

int Engine_FxRibbon(const EngineVec3 *left, const EngineVec3 *right, const u8 (*colour)[4], int points, float uScale, EngineVertex *outV, int maxVertices, u16 *outI, int maxIndices, u16 baseIndex)
{
	static const u8 white[4] = {255, 255, 255, 255};
	EngineVec3 prevCentre = {0.0f, 0.0f, 0.0f};
	float dist = 0.0f;
	int i;

	if (!left || !right || !outV || !outI || points < 2)
	{
		return 0;
	}
	if (maxVertices < 2 * points || maxIndices < 6 * (points - 1))
	{
		return 0;
	}
	if ((long)2 * points - 1 + (long)baseIndex > 65535)
	{
		return 0;
	}
	for (i = 0; i < points; i++)
	{
		EngineVec3 centre = EngineFx_Scale(EngineFx_Add(left[i], right[i]), 0.5f);
		const u8 *col = colour ? colour[i] : white;
		float u;
		if (i > 0)
		{
			EngineVec3 d = EngineFx_Sub(centre, prevCentre);
			dist += sqrtf(EngineFx_Dot(d, d));
		}
		prevCentre = centre;
		u = uScale * dist;
		EngineFx_SetVertex(&outV[2 * i], left[i], u, 0.0f, col);
		EngineFx_SetVertex(&outV[2 * i + 1], right[i], u, 1.0f, col);
	}
	for (i = 0; i < points - 1; i++)
	{
		u16 *o = &outI[6 * i];
		o[0] = (u16)(2 * i + baseIndex);
		o[1] = (u16)(2 * i + 1 + baseIndex);
		o[2] = (u16)(2 * i + 3 + baseIndex);
		o[3] = (u16)(2 * i + baseIndex);
		o[4] = (u16)(2 * i + 3 + baseIndex);
		o[5] = (u16)(2 * i + 2 + baseIndex);
	}
	return 2 * points;
}

// --------------------------------------------------------------------------
// SELFTEST

global_variable int g_engineFxChecks;
global_variable int g_engineFxFailures;

internal void EngineFx_Check(int ok, const char *what)
{
	g_engineFxChecks++;
	if (!ok)
	{
		g_engineFxFailures++;
		printf("engine selftest: fx: %s\n", what);
	}
}

internal int EngineFx_Near(float a, float b)
{
	return fabsf(a - b) < 1e-4f;
}

internal int EngineFx_VertexAt(const EngineVertex *v, float x, float y, float z)
{
	return EngineFx_Near(v->position[0], x) && EngineFx_Near(v->position[1], y) && EngineFx_Near(v->position[2], z);
}

internal int EngineFx_UvAt(const EngineVertex *v, float u, float t)
{
	return EngineFx_Near(v->texcoord[0], u) && EngineFx_Near(v->texcoord[1], t);
}

internal int EngineFx_ColourIs(const EngineVertex *v, u8 r, u8 g, u8 b, u8 a)
{
	return v->color[0] == r && v->color[1] == g && v->color[2] == b && v->color[3] == a;
}

internal int EngineFx_QuadIndices(const u16 *i, u16 base)
{
	static const u16 e[6] = {0, 1, 2, 0, 2, 3};
	int k;
	for (k = 0; k < 6; k++)
	{
		if (i[k] != (u16)(e[k] + base))
		{
			return 0;
		}
	}
	return 1;
}

internal void EngineFx_QuadCommon(const EngineVertex *v, const u16 *i, u16 base, const char *name)
{
	char msg[96];
	snprintf(msg, sizeof(msg), "%s texcoords", name);
	EngineFx_Check(EngineFx_UvAt(&v[0], 0, 0) && EngineFx_UvAt(&v[1], 1, 0) && EngineFx_UvAt(&v[2], 1, 1) && EngineFx_UvAt(&v[3], 0, 1), msg);
	snprintf(msg, sizeof(msg), "%s indices", name);
	EngineFx_Check(EngineFx_QuadIndices(i, base), msg);
	snprintf(msg, sizeof(msg), "%s colours", name);
	EngineFx_Check(EngineFx_ColourIs(&v[0], 10, 20, 30, 40) && EngineFx_ColourIs(&v[1], 10, 20, 30, 40) && EngineFx_ColourIs(&v[2], 10, 20, 30, 40) && EngineFx_ColourIs(&v[3], 10, 20, 30, 40), msg);
}

internal void EngineFx_TestBillboard(void)
{
	EngineCamera cam;
	EngineVertex v[4];
	u16 idx[6];
	const u8 col[4] = {10, 20, 30, 40};
	EngineVec3 c = {1.0f, 2.0f, 3.0f};
	int i, n;

	for (i = 0; i < 16; i++)
	{
		cam.viewFromWorld.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	}
	// Identity: right = +X, row1 = +Y so screen-up = -Y.
	n = Engine_FxBillboard(&cam, c, 2.0f, 3.0f, 0, col, v, idx, 7);
	EngineFx_Check(n == 4, "billboard identity count");
	EngineFx_Check(EngineFx_VertexAt(&v[0], -1, -1, 3) && EngineFx_VertexAt(&v[1], 3, -1, 3) && EngineFx_VertexAt(&v[2], 3, 5, 3) && EngineFx_VertexAt(&v[3], -1, 5, 3), "billboard identity positions");
	EngineFx_QuadCommon(v, idx, 7, "billboard");

	// Roll 1024 (90 degrees clockwise): right' = -up = +Y, up' = right = +X.
	n = Engine_FxBillboard(&cam, c, 2.0f, 3.0f, 1024, col, v, idx, 0);
	EngineFx_Check(n == 4, "billboard roll count");
	// right' * halfW = (0,2,0), up' * halfH = (3,0,0)
	EngineFx_Check(EngineFx_VertexAt(&v[0], 4, 0, 3) && EngineFx_VertexAt(&v[1], 4, 4, 3) && EngineFx_VertexAt(&v[2], -2, 4, 3) && EngineFx_VertexAt(&v[3], -2, 0, 3), "billboard roll 1024 positions");
	n = Engine_FxBillboard(&cam, c, 2.0f, 3.0f, 4096 / 2, col, v, idx, 0);
	EngineFx_Check(n == 4 && EngineFx_VertexAt(&v[0], 3, 5, 3) && EngineFx_VertexAt(&v[2], -1, -1, 3), "billboard roll 2048 flips");

	// Camera turned 90 degrees about Y, built by hand (column-major, m[col*4+row]):
	// row0 = (0,0,1) = right, row1 = (0,2,0) (aspect scaled), row2 = (-1,0,0).
	for (i = 0; i < 16; i++)
	{
		cam.viewFromWorld.m[i] = 0.0f;
	}
	cam.viewFromWorld.m[8] = 1.0f;  // col2,row0
	cam.viewFromWorld.m[5] = 2.0f;  // col1,row1
	cam.viewFromWorld.m[2] = -1.0f; // col0,row2
	cam.viewFromWorld.m[15] = 1.0f;
	n = Engine_FxBillboard(&cam, c, 2.0f, 3.0f, 0, col, v, idx, 0);
	// right = +Z, up = -Y: v0 = c - Z*2 + up*3 = (1,-1,1)
	EngineFx_Check(n == 4 && EngineFx_VertexAt(&v[0], 1, -1, 1) && EngineFx_VertexAt(&v[1], 1, -1, 5) && EngineFx_VertexAt(&v[2], 1, 5, 5) && EngineFx_VertexAt(&v[3], 1, 5, 1), "billboard rotated camera positions");

	EngineFx_Check(Engine_FxBillboard(NULL, c, 2, 3, 0, col, v, idx, 0) == 0, "billboard NULL cam");
	EngineFx_Check(Engine_FxBillboard(&cam, c, 2, 3, 0, NULL, v, idx, 0) == 0, "billboard NULL colour");
	EngineFx_Check(Engine_FxBillboard(&cam, c, 2, 3, 0, col, NULL, idx, 0) == 0, "billboard NULL vertices");
	EngineFx_Check(Engine_FxBillboard(&cam, c, 2, 3, 0, col, v, NULL, 0) == 0, "billboard NULL indices");
	EngineFx_Check(Engine_FxBillboard(&cam, c, 0, 3, 0, col, v, idx, 0) == 0, "billboard zero width");
	EngineFx_Check(Engine_FxBillboard(&cam, c, 2, -1, 0, col, v, idx, 0) == 0, "billboard negative height");
}

internal void EngineFx_TestGroundQuad(void)
{
	EngineVertex v[4];
	u16 idx[6];
	const u8 col[4] = {10, 20, 30, 40};
	EngineVec3 c = {1.0f, 2.0f, 3.0f};
	EngineVec3 n0 = {0, 1, 0}, f0 = {1, 0, 0};
	EngineVec3 nt = {0, 3, 4}; // not unit, tilted
	EngineVec3 ft = {0, 0, 1}; // not in the plane
	EngineVec3 zero = {0, 0, 0};
	EngineVec3 nn, fp, d;
	int n, i;
	float len;

	// Flat: n = +Y, forward = +X: s = cross(Y, X) = -Z.
	n = Engine_FxGroundQuad(c, n0, f0, 4.0f, 2.0f, col, v, idx, 100);
	EngineFx_Check(n == 4, "ground flat count");
	EngineFx_Check(EngineFx_VertexAt(&v[0], 5, 2, 1) && EngineFx_VertexAt(&v[1], 5, 2, 5) && EngineFx_VertexAt(&v[2], -3, 2, 5) && EngineFx_VertexAt(&v[3], -3, 2, 1), "ground flat positions");
	EngineFx_QuadCommon(v, idx, 100, "ground");

	// Tilted plane: unit normal (0, .6, .8); f' = normalise(z - n*0.8).
	n = Engine_FxGroundQuad(c, nt, ft, 4.0f, 2.0f, col, v, idx, 0);
	EngineFx_Check(n == 4, "ground tilted count");
	nn = EngineFx_V3(0.0f, 0.6f, 0.8f);
	fp = EngineFx_V3(0.0f, -0.48f, 0.36f);
	EngineFx_Normalise(&fp);
	for (i = 0; i < 4; i++)
	{
		d = EngineFx_V3(v[i].position[0] - c.x, v[i].position[1] - c.y, v[i].position[2] - c.z);
		EngineFx_Check(fabsf(EngineFx_Dot(d, nn)) < 1e-4f, "ground tilted vertex on plane");
	}
	d = EngineFx_V3(v[0].position[0] - v[3].position[0], v[0].position[1] - v[3].position[1], v[0].position[2] - v[3].position[2]);
	len = sqrtf(EngineFx_Dot(d, d));
	EngineFx_Check(EngineFx_Near(len, 8.0f), "ground tilted forward edge length");
	EngineFx_Normalise(&d);
	EngineFx_Check(EngineFx_Near(d.x, fp.x) && EngineFx_Near(d.y, fp.y) && EngineFx_Near(d.z, fp.z), "ground tilted forward edge direction");
	d = EngineFx_V3(v[0].position[0] - v[1].position[0], v[0].position[1] - v[1].position[1], v[0].position[2] - v[1].position[2]);
	len = sqrtf(EngineFx_Dot(d, d));
	EngineFx_Check(EngineFx_Near(len, 4.0f), "ground tilted side edge length");
	EngineFx_Check(EngineFx_Near(EngineFx_Dot(d, fp), 0.0f), "ground tilted edges perpendicular");

	EngineFx_Check(Engine_FxGroundQuad(c, zero, f0, 4, 2, col, v, idx, 0) == 0, "ground zero normal");
	EngineFx_Check(Engine_FxGroundQuad(c, n0, zero, 4, 2, col, v, idx, 0) == 0, "ground zero forward");
	EngineFx_Check(Engine_FxGroundQuad(c, n0, n0, 4, 2, col, v, idx, 0) == 0, "ground forward parallel to normal");
	EngineFx_Check(Engine_FxGroundQuad(c, n0, f0, 0, 2, col, v, idx, 0) == 0, "ground zero length");
	EngineFx_Check(Engine_FxGroundQuad(c, n0, f0, 4, -2, col, v, idx, 0) == 0, "ground negative width");
	EngineFx_Check(Engine_FxGroundQuad(c, n0, f0, 4, 2, NULL, v, idx, 0) == 0, "ground NULL colour");
	EngineFx_Check(Engine_FxGroundQuad(c, n0, f0, 4, 2, col, NULL, idx, 0) == 0, "ground NULL vertices");
	EngineFx_Check(Engine_FxGroundQuad(c, n0, f0, 4, 2, col, v, NULL, 0) == 0, "ground NULL indices");
}

internal void EngineFx_TestRibbon(void)
{
	// Centre line: (0,0,0) -> (3,0,0) -> (3,4,0): distances 0, 3, 7.
	EngineVec3 left[3] = {{0, 0, -1}, {3, 0, -1}, {3, 4, -1}};
	EngineVec3 right[3] = {{0, 0, 1}, {3, 0, 1}, {3, 4, 1}};
	u8 colour[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
	EngineVertex v[8];
	u16 idx[12];
	static const u16 expect[12] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
	int n, i;

	n = Engine_FxRibbon(left, right, (const u8(*)[4])colour, 3, 0.5f, v, 8, idx, 12, 10);
	EngineFx_Check(n == 6, "ribbon count");
	EngineFx_Check(EngineFx_VertexAt(&v[0], 0, 0, -1) && EngineFx_VertexAt(&v[1], 0, 0, 1) && EngineFx_VertexAt(&v[4], 3, 4, -1) && EngineFx_VertexAt(&v[5], 3, 4, 1), "ribbon positions");
	EngineFx_Check(EngineFx_UvAt(&v[0], 0, 0) && EngineFx_UvAt(&v[1], 0, 1) && EngineFx_UvAt(&v[2], 1.5f, 0) && EngineFx_UvAt(&v[3], 1.5f, 1) && EngineFx_UvAt(&v[4], 3.5f, 0) && EngineFx_UvAt(&v[5], 3.5f, 1), "ribbon u accumulates along centre line");
	EngineFx_Check(EngineFx_ColourIs(&v[0], 1, 2, 3, 4) && EngineFx_ColourIs(&v[1], 1, 2, 3, 4) && EngineFx_ColourIs(&v[2], 5, 6, 7, 8) && EngineFx_ColourIs(&v[5], 9, 10, 11, 12), "ribbon per-point colours");
	for (i = 0; i < 12; i++)
	{
		if (idx[i] != (u16)(expect[i] + 10))
		{
			break;
		}
	}
	EngineFx_Check(i == 12, "ribbon index pattern with baseIndex");

	n = Engine_FxRibbon(left, right, NULL, 2, 1.0f, v, 8, idx, 12, 0);
	EngineFx_Check(n == 4 && EngineFx_ColourIs(&v[3], 255, 255, 255, 255) && EngineFx_UvAt(&v[2], 3.0f, 0), "ribbon NULL colour is white");

	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 3, 1.0f, v, 5, idx, 12, 0) == 0, "ribbon vertex capacity refused");
	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 3, 1.0f, v, 8, idx, 11, 0) == 0, "ribbon index capacity refused");
	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 3, 1.0f, v, 6, idx, 12, 0) == 6, "ribbon exact capacity accepted");
	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 3, 1.0f, v, 8, idx, 12, 65531) == 0, "ribbon baseIndex overflow refused");
	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 3, 1.0f, v, 8, idx, 12, 65530) == 6, "ribbon highest baseIndex accepted");
	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 1, 1.0f, v, 8, idx, 12, 0) == 0, "ribbon one point refused");
	EngineFx_Check(Engine_FxRibbon(NULL, right, NULL, 3, 1.0f, v, 8, idx, 12, 0) == 0, "ribbon NULL left");
	EngineFx_Check(Engine_FxRibbon(left, NULL, NULL, 3, 1.0f, v, 8, idx, 12, 0) == 0, "ribbon NULL right");
	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 3, 1.0f, NULL, 8, idx, 12, 0) == 0, "ribbon NULL vertices");
	EngineFx_Check(Engine_FxRibbon(left, right, NULL, 3, 1.0f, v, 8, NULL, 12, 0) == 0, "ribbon NULL indices");
}

int Engine_SelfTestFx(int *checks)
{
	g_engineFxChecks = 0;
	g_engineFxFailures = 0;
	EngineFx_TestBillboard();
	EngineFx_TestGroundQuad();
	EngineFx_TestRibbon();
	if (checks)
	{
		*checks += g_engineFxChecks;
	}
	return g_engineFxFailures;
}
