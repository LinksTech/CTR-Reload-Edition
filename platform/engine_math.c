#include <platform/engine.h>

// ENGINE MATH - see include/platform/engine.h.
//
// Small column-major float matrices for the native engine. The layout is
// m[col * 4 + row], points are column vectors, so p' = M * p and a chain of
// transforms reads right to left: world-from-model comes last in a product.
//
// The two bridges to the PS1 side are Engine_Mat4FromGte (a GTE rotation in
// s16 4.12 plus an s32 translation, as the push buffers and the SetRotMatrix
// calls hold them) and Engine_Mat4FromGameRot (the game Euler angles, built
// in the same order as ConvertRotToMatrix: Y, then X, then Z, i.e. the product
// Ry * Rx * Rz). Both give a matrix that maps the same points as the PS1 side
// does, up to the 1/4096 steps of the fixed-point format.

#define ENGINE_MATH_ONE_OVER_4096 (1.0f / 4096.0f)
// The game multiplies its three axis matrices in 4.12 fixed point and truncates
// after each product, so a composed rotation is up to about 3/4096 off the exact
// float one (a single axis stays within 1/4096).
#define ENGINE_MATH_ROT_TOLERANCE (4.0f / 4096.0f)
#define ENGINE_MATH_TWO_PI        6.28318530717958647692f

void Engine_Mat4Identity(EngineMat4 *out)
{
	int i;

	for (i = 0; i < 16; i++)
	{
		out->m[i] = 0.0f;
	}
	out->m[0] = 1.0f;
	out->m[5] = 1.0f;
	out->m[10] = 1.0f;
	out->m[15] = 1.0f;
}

// out = a * b. The product goes through a local copy so that out may be a or b.
void Engine_Mat4Mul(const EngineMat4 *a, const EngineMat4 *b, EngineMat4 *out)
{
	EngineMat4 r;
	int col;
	int row;
	int k;

	for (col = 0; col < 4; col++)
	{
		for (row = 0; row < 4; row++)
		{
			float sum = 0.0f;

			for (k = 0; k < 4; k++)
			{
				sum += a->m[k * 4 + row] * b->m[col * 4 + k];
			}
			r.m[col * 4 + row] = sum;
		}
	}
	*out = r;
}

EngineVec3 Engine_Mat4MulPoint(const EngineMat4 *m, EngineVec3 p)
{
	EngineVec3 r;

	r.x = m->m[0] * p.x + m->m[4] * p.y + m->m[8] * p.z + m->m[12];
	r.y = m->m[1] * p.x + m->m[5] * p.y + m->m[9] * p.z + m->m[13];
	r.z = m->m[2] * p.x + m->m[6] * p.y + m->m[10] * p.z + m->m[14];
	return r;
}

void Engine_Mat4FromGte(const s16 r[3][3], const s32 t[3], EngineMat4 *out)
{
	int row;
	int col;

	Engine_Mat4Identity(out);
	for (row = 0; row < 3; row++)
	{
		for (col = 0; col < 3; col++)
		{
			out->m[col * 4 + row] = (float)r[row][col] * ENGINE_MATH_ONE_OVER_4096;
		}
		out->m[12 + row] = (float)t[row];
	}
}

// Ry * Rx * Rz with the angle in 4096ths of a turn, as ConvertRotToMatrix
// multiplies its three axis matrices (the game reads only the low 12 bits).
void Engine_Mat4FromGameRot(s16 rx, s16 ry, s16 rz, EngineMat4 *out)
{
	const float ax = (float)((int)rx & 0xfff) * (ENGINE_MATH_TWO_PI / 4096.0f);
	const float ay = (float)((int)ry & 0xfff) * (ENGINE_MATH_TWO_PI / 4096.0f);
	const float az = (float)((int)rz & 0xfff) * (ENGINE_MATH_TWO_PI / 4096.0f);
	const float sx = sinf(ax);
	const float cx = cosf(ax);
	const float sy = sinf(ay);
	const float cy = cosf(ay);
	const float sz = sinf(az);
	const float cz = cosf(az);
	EngineMat4 mx;
	EngineMat4 my;
	EngineMat4 mz;

	Engine_Mat4Identity(&mx);
	mx.m[5] = cx;
	mx.m[6] = sx;
	mx.m[9] = -sx;
	mx.m[10] = cx;

	Engine_Mat4Identity(&my);
	my.m[0] = cy;
	my.m[2] = -sy;
	my.m[8] = sy;
	my.m[10] = cy;

	Engine_Mat4Identity(&mz);
	mz.m[0] = cz;
	mz.m[1] = sz;
	mz.m[4] = -sz;
	mz.m[5] = cz;

	Engine_Mat4Mul(&my, &mx, out);
	Engine_Mat4Mul(out, &mz, out);
}

// --------------------------------------------------------------------------
// Selftest

internal void EngineMath_Check(int ok, const char *what, int *checks, int *failures)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: %s\n", what);
	}
}

internal int EngineMath_Near(float a, float b, float tolerance)
{
	const float d = a - b;

	return (d <= tolerance) && (d >= -tolerance);
}

internal int EngineMath_VecNear(EngineVec3 a, EngineVec3 b, float tolerance)
{
	return EngineMath_Near(a.x, b.x, tolerance) && EngineMath_Near(a.y, b.y, tolerance) && EngineMath_Near(a.z, b.z, tolerance);
}

internal void EngineMath_Translation(float x, float y, float z, EngineMat4 *out)
{
	Engine_Mat4Identity(out);
	out->m[12] = x;
	out->m[13] = y;
	out->m[14] = z;
}

int Engine_SelfTestMath(int *checks)
{
	int failures = 0;
	int n = 0;
	EngineMat4 a;
	EngineMat4 b;
	EngineMat4 c;
	EngineVec3 p = {1.0f, 2.0f, 3.0f};
	EngineVec3 q;
	EngineVec3 expect;
	EngineVec3 origin = {0.0f, 0.0f, 0.0f};
	int i;

	// Identity: 1 on the diagonal, 0 elsewhere, leaves a point alone.
	Engine_Mat4Identity(&a);
	for (i = 0; i < 16; i++)
	{
		const float want = ((i % 5) == 0) ? 1.0f : 0.0f;

		if (a.m[i] != want)
		{
			break;
		}
	}
	EngineMath_Check(i == 16, "Mat4Identity layout", &n, &failures);
	EngineMath_Check(EngineMath_VecNear(Engine_Mat4MulPoint(&a, p), p, 0.0f), "Mat4Identity keeps a point", &n, &failures);

	// Translation times translation adds; a point sees both.
	EngineMath_Translation(1.0f, 2.0f, 3.0f, &a);
	EngineMath_Translation(10.0f, 20.0f, 30.0f, &b);
	Engine_Mat4Mul(&a, &b, &c);
	expect = (EngineVec3){11.0f, 22.0f, 33.0f};
	EngineMath_Check(EngineMath_VecNear(Engine_Mat4MulPoint(&c, origin), expect, 0.0f), "Mat4Mul translations add", &n, &failures);

	// Scale and translation: the order matters (a * b applies b first).
	Engine_Mat4Identity(&a);
	a.m[0] = 2.0f;
	a.m[5] = 3.0f;
	a.m[10] = 4.0f;
	EngineMath_Translation(1.0f, 1.0f, 1.0f, &b);
	Engine_Mat4Mul(&a, &b, &c);
	expect = (EngineVec3){2.0f, 3.0f, 4.0f};
	EngineMath_Check(EngineMath_VecNear(Engine_Mat4MulPoint(&c, origin), expect, 0.0f), "Mat4Mul scale after translation", &n, &failures);
	Engine_Mat4Mul(&b, &a, &c);
	expect = (EngineVec3){1.0f, 1.0f, 1.0f};
	EngineMath_Check(EngineMath_VecNear(Engine_Mat4MulPoint(&c, origin), expect, 0.0f), "Mat4Mul translation after scale", &n, &failures);

	// A fully hand-computed product: a = [[1,2],[3,4]] block in x/y, b = [[5,6],[7,8]].
	Engine_Mat4Identity(&a);
	Engine_Mat4Identity(&b);
	a.m[0] = 1.0f;
	a.m[4] = 2.0f;
	a.m[1] = 3.0f;
	a.m[5] = 4.0f;
	b.m[0] = 5.0f;
	b.m[4] = 6.0f;
	b.m[1] = 7.0f;
	b.m[5] = 8.0f;
	Engine_Mat4Mul(&a, &b, &c);
	EngineMath_Check(c.m[0] == 19.0f && c.m[4] == 22.0f && c.m[1] == 43.0f && c.m[5] == 50.0f, "Mat4Mul 2x2 block product", &n, &failures);

	// Aliasing: out may be a or b.
	c = a;
	Engine_Mat4Mul(&c, &b, &c);
	EngineMath_Check(c.m[0] == 19.0f && c.m[4] == 22.0f && c.m[1] == 43.0f && c.m[5] == 50.0f, "Mat4Mul out aliases a", &n, &failures);
	c = b;
	Engine_Mat4Mul(&a, &c, &c);
	EngineMath_Check(c.m[0] == 19.0f && c.m[4] == 22.0f && c.m[1] == 43.0f && c.m[5] == 50.0f, "Mat4Mul out aliases b", &n, &failures);

	// FromGte: 90 degrees about Y (x' = z, z' = -x) and a translation.
	{
		const s16 r[3][3] = {{0, 0, 4096}, {0, 4096, 0}, {-4096, 0, 0}};
		const s32 t[3] = {100, -200, 300};

		Engine_Mat4FromGte(r, t, &a);
		q = Engine_Mat4MulPoint(&a, p);
		expect = (EngineVec3){3.0f + 100.0f, 2.0f - 200.0f, -1.0f + 300.0f};
		EngineMath_Check(EngineMath_VecNear(q, expect, 1e-5f), "Mat4FromGte 90 degrees about Y plus translation", &n, &failures);
		EngineMath_Check(a.m[3] == 0.0f && a.m[7] == 0.0f && a.m[11] == 0.0f && a.m[15] == 1.0f, "Mat4FromGte bottom row", &n, &failures);
	}

	// FromGameRot against the game ConvertRotToMatrix, 64 deterministic triples.
	{
		u32 seed = 0x2468ace1u;
		float worst = 0.0f;
		int bad = 0;

		for (i = 0; i < 64; i++)
		{
			SVec3 rot;
			MATRIX gm;
			int row;
			int col;

			seed = seed * 1664525u + 1013904223u;
			rot.x = (s16)((seed >> 8) & 0xfff);
			seed = seed * 1664525u + 1013904223u;
			rot.y = (s16)((seed >> 8) & 0xfff);
			seed = seed * 1664525u + 1013904223u;
			rot.z = (s16)((seed >> 8) & 0xfff);
			if (i < 4)
			{
				// The identity and each axis alone are part of the set.
				rot.x = (s16)((i == 1) ? 0x155 : 0);
				rot.y = (s16)((i == 2) ? 0x2aa : 0);
				rot.z = (s16)((i == 3) ? 0x3ff : 0);
			}

			ConvertRotToMatrix(&gm, &rot);
			Engine_Mat4FromGameRot(rot.x, rot.y, rot.z, &a);
			for (row = 0; row < 3; row++)
			{
				for (col = 0; col < 3; col++)
				{
					float d = a.m[col * 4 + row] - (float)gm.m[row][col] * ENGINE_MATH_ONE_OVER_4096;

					if (d < 0.0f)
					{
						d = -d;
					}
					if (d > worst)
					{
						worst = d;
					}
					if (d > ENGINE_MATH_ROT_TOLERANCE)
					{
						bad++;
					}
				}
			}
		}
		EngineMath_Check(bad == 0, "Mat4FromGameRot differs from ConvertRotToMatrix by more than 4/4096", &n, &failures);
		if (bad != 0)
		{
			printf("engine selftest info: Mat4FromGameRot worst element error %.6f (%d elements)\n", (double)worst, bad);
		}
	}

	*checks += n;
	return failures;
}
