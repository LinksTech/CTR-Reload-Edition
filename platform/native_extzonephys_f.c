// THE ZONE PORT, PART F: ground probe, input latch, wheel-parameter blend, node
// tracking, spring-back, alignment controller, boost zone (see
// include/platform/native_extzonephys.h for the conventions). Compiles alone
// (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_f.h>
#include <platform/native_extzonephys_c.h>
#include <string.h>

// Globals of the original (absolute addresses, read in the port's memory).
#define F_G_SURF_TABLE 0x006F53E0u // pointer to the surface table (44 bytes per type)
#define F_G_WORLD 0x006FA190u      // pointer to the world; world + 0x74 = the ray query object
#define F_G_CLASS_ID 0x006F63E0u   // the class id handed to the type test
#define F_G_PHANTOMS 0x006F3B20u   // pointer to the phantom data; + 0x34 = the zone list
#define F_G_MODE_A 0x006F3568u     // pointer (0 = none) to an object with a mode index at +0x124
#define F_G_MODE_B 0x006F35B8u     // pointer to the global game object (+0x24C mode index, +0x190 tick count)
#define F_G_MODE_TAB 0x0068B650u   // byte table indexed by the mode index

// Kart fields used here.
enum
{
	F_K_RB = 0x2C,        // pointer to a pointer to the kart's rigid-body wrapper R
	F_K_OWNER = 0x38,     // the driver object (0 = none)
	F_K_STATE = 0x3E0,    // driving state
	F_K_GROUND = 0x402,   // u8: ground contact
	F_K_UP = 0x410,       // vec4: reference up vector
	F_K_SURF_TYPE = 0x420,
	F_K_SURF = 0x424,     // pointer into the surface table
	F_K_MAGLEV = 0x428,
	F_R_BODY = 0x94,      // R: pointer to the body H
	F_H_POS = 0x30,       // H: position
	F_H_VEL = 0x80,       // H: velocity
};

// The 3-term dot product of the original: mula / madda / madd, a first operand.
static Ps2F F_Dot(const Ps2F a[3], const Ps2F b[3])
{
	Ps2Acc acc = Ps2A_Mula(a[0], b[0]);
	acc = Ps2A_Madda(acc, a[1], b[1]);
	return Ps2A_Madd(acc, a[2], b[2]);
}

static void F_Rd3(ExtZpMem *m, uint32_t a, Ps2F v[3])
{
	v[0] = ZmRF(m, a);
	v[1] = ZmRF(m, a + 4u);
	v[2] = ZmRF(m, a + 8u);
}

// Normalises the xyz of v with the original's sequence (length^2 by mula/madda/madd,
// sqrt, three divides). Returns f3 (the length, or 0 when the length^2 is 0 and
// nothing was divided); *did = 1 when the divides ran.
static Ps2F F_Norm(Ps2Vec4 *v, int *did)
{
	Ps2Acc acc = Ps2A_Mula(v->v[0], v->v[0]);
	Ps2F f3;
	acc = Ps2A_Madda(acc, v->v[1], v->v[1]);
	f3 = Ps2A_Madd(acc, v->v[2], v->v[2]);
	if (Ps2F_Eq(f3, 0u))
	{
		*did = 0;
		return f3;
	}
	f3 = Ps2F_Sqrt(f3);
	v->v[0] = Ps2F_Div(v->v[0], f3);
	v->v[1] = Ps2F_Div(v->v[1], f3);
	v->v[2] = Ps2F_Div(v->v[2], f3);
	*did = 1;
	return f3;
}

// Normalises the xyz at addr in memory.
static Ps2F F_NormMem(ExtZpMem *m, uint32_t a)
{
	Ps2Vec4 v = {{ZmRF(m, a), ZmRF(m, a + 4u), ZmRF(m, a + 8u), 0u}};
	int did;
	Ps2F f3 = F_Norm(&v, &did);
	if (did)
	{
		ZmWF(m, a, v.v[0]);
		ZmWF(m, a + 4u, v.v[1]);
		ZmWF(m, a + 8u, v.v[2]);
	}
	return f3;
}

// v.xyz *= s (vmulx.xyz with s broadcast).
static void F_MulX(Ps2Vec4 *v, Ps2F s)
{
	Ps2Vec4 inv = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&inv, 0);
	Ps2V_Mul(v, v, &bc, 0xEu);
}

// d.xyz += b.xyz (vadd.xyz).
static void F_AddXyz(Ps2Vec4 *d, const Ps2Vec4 *b)
{
	Ps2V_Add(d, d, b, 0xEu);
}

// K+0x424 = surface table + type * 44 + 4 (the original's shifts and adds).
static void F_SetSurf(ExtZpMem *m, uint32_t k, uint32_t type)
{
	uint32_t tab = ZmR32(m, F_G_SURF_TABLE);
	ZmW32(m, k + F_K_SURF, tab + type * 44u + 4u);
}

// The query object's ray test (0x338700): a0 = query object, a1 = start, a2 = end.
static uint32_t F_Ray(ExtZpCtx *c, uint32_t rc, uint32_t start, uint32_t end)
{
	ExtZpQueryArgs qa;
	memset(&qa, 0, sizeof(qa));
	qa.func = 0x00338700u;
	qa.a[0] = rc;
	qa.a[1] = start;
	qa.a[2] = end;
	qa.a[3] = 0u;
	qa.a[4] = 0u;
	return ExtZp_Query(c, EXT_ZP_Q_RAY, &qa).v0;
}

// ---------------------------------------------------------------------------------------
// Ground probe / reference up vector (0x191bc0).
void ExtZp_Kart_191bc0(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;

	if (ZmR8(m, k + F_K_GROUND) != 0u)
	{
		// ground contact: normalise the up vector, point K+0x424 at the surface entry
		F_NormMem(m, k + F_K_UP);
		F_SetSurf(m, k, ZmR32(m, k + F_K_SURF_TYPE));
	}
	else
	{
		ZmW32(m, k + F_K_SURF_TYPE, 0u);
		if (ZmR8(m, k + F_K_MAGLEV) != 0u)
		{
			// maglev: ray 10 units along minus the chassis up from the body position
			const uint32_t r = ZmR32(m, ZmR32(m, k + F_K_RB));
			const uint32_t rc = ZmR32(m, ZmR32(m, F_G_WORLD) + 0x74u);
			const uint32_t h = ZmR32(m, r + F_R_BODY);
			const uint32_t endAddr = ExtZp_Push(c, 16u);
			const uint32_t startAddr = ExtZp_Push(c, 16u);
			Ps2Vec4 end = ZmRV(m, r + 0x20u);
			Ps2Vec4 pos;
			uint32_t hit;

			F_MulX(&end, 0xC1200000u); // -10 (the original's constant)
			pos = ZmRV(m, h + F_H_POS);
			F_AddXyz(&end, &pos);
			ZmWV(m, endAddr, end);
			ZmWV(m, k + F_K_UP, ZmRV(m, r + 0x20u));
			ZmWV(m, startAddr, ZmRV(m, h + F_H_POS));
			hit = F_Ray(c, rc, startAddr, endAddr);
			if (hit != 0u)
			{
				const int32_t type = (int32_t)ZmR32(m, rc + 0x54u); // hit record: surface type
				if (!(type < 0x18) && (type < 0x1B))
				{
					ZmWV(m, k + F_K_UP, ZmRV(m, rc + 0x40u)); // hit record: normal
				}
			}
			ExtZp_Pop(c, 32u);
		}
		else
		{
			// maglev off: up = (0, 0, 1, 1); ray along minus the chassis up for K+0x50
			ZmWV(m, k + F_K_UP, (Ps2Vec4){{0u, 0u, PS2F_ONE, PS2F_ONE}});
			if (ZmR32(m, k + F_K_STATE) != 4u && Ps2F_Le(ZmRF(m, k + 0x490u), 0u))
			{
				const uint32_t r = ZmR32(m, ZmR32(m, k + F_K_RB));
				const uint32_t rc = ZmR32(m, ZmR32(m, F_G_WORLD) + 0x74u);
				const uint32_t h = ZmR32(m, r + F_R_BODY);
				const uint32_t endAddr = ExtZp_Push(c, 16u);
				const uint32_t startAddr = ExtZp_Push(c, 16u);
				Ps2Vec4 end = ZmRV(m, r + 0x20u);
				Ps2Vec4 pos;
				uint32_t hit;

				F_MulX(&end, Ps2F_Neg(ZmRF(m, k + 0x50u)));
				pos = ZmRV(m, h + F_H_POS);
				F_AddXyz(&end, &pos);
				ZmWV(m, endAddr, end);
				ZmWV(m, startAddr, ZmRV(m, h + F_H_POS));
				hit = F_Ray(c, rc, startAddr, endAddr);
				if (hit != 0u)
				{
					const int32_t type = (int32_t)ZmR32(m, rc + 0x54u);
					if (type > 0 && type < 0x19)
					{
						ZmW8(m, k + F_K_GROUND, 1u);
						ZmWV(m, k + F_K_UP, ZmRV(m, rc + 0x40u));
						ZmW32(m, k + F_K_SURF_TYPE, ZmR32(m, rc + 0x54u));
						F_SetSurf(m, k, ZmR32(m, k + F_K_SURF_TYPE));
					}
				}
				ExtZp_Pop(c, 32u);
			}
		}
	}

	// the owner's effect bits can force surface type 0x16
	if (ZmR32(m, k + F_K_OWNER) != 0u)
	{
		if (ExtZp_Kart_1c1ff0(c, ZmR32(m, ZmR32(m, k + F_K_OWNER) + 0x88u), 0xFu) != 0u)
		{
			ZmW32(m, k + F_K_SURF_TYPE, 0x16u);
			F_SetSurf(m, k, ZmR32(m, k + F_K_SURF_TYPE));
		}
		if (ExtZp_Kart_1c1ff0(c, ZmR32(m, ZmR32(m, k + F_K_OWNER) + 0x88u), 0xAu) != 0u)
		{
			if (ExtZp_Kart_1c2010(c, ZmR32(m, ZmR32(m, k + F_K_OWNER) + 0x88u), 0xAu) != 0u)
			{
				ZmW32(m, k + F_K_SURF_TYPE, 0x16u);
				F_SetSurf(m, k, ZmR32(m, k + F_K_SURF_TYPE));
			}
		}
	}
}

// ---------------------------------------------------------------------------------------
// Input latch (0x191f60).
void ExtZp_Kart_191f60(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;
	uint32_t a1;
	uint32_t v1;
	Ps2F f0, f1;

	if (ZmR32(m, k + F_K_OWNER) != 0u)
	{
		a1 = ZmR32(m, k + F_K_OWNER) + 0x5Cu;      // the owner's input block
	}
	else
	{
		a1 = ZmR32(m, k + 0x3Cu) + 0x898u;         // the default input block
	}
	ZmWF(m, k + 0x3E4u, ZmRF(m, a1));
	ZmWF(m, k + 0x3E8u, ZmRF(m, a1 + 4u));
	ZmWF(m, k + 0x3ECu, ZmRF(m, a1 + 8u));
	ZmWF(m, k + 0x3F4u, ZmRF(m, a1 + 0x10u));

	if (ZmR32(m, k + F_K_STATE) == 3u || !Ps2F_Le(ZmRF(m, k + 0x54Cu), ZmRF(m, k + 0x394u)))
	{
		f1 = ZmRF(m, a1 + 0x10u);
		f0 = ZmRF(m, a1 + 0xCu);
		if (Ps2F_Le(f0, f1))
		{
			f0 = f1;
		}
		ZmWF(m, k + 0x3F0u, f0);
		ZmW32(m, k + 0x3F4u, 0u);
	}
	else
	{
		ZmWF(m, k + 0x3F0u, ZmRF(m, a1 + 0xCu));
	}

	// the two button channels: edge flag (just pressed) and held flag
	if (ZmR8(m, a1 + 0x14u) != 0u)
	{
		uint32_t a0 = ZmR8(m, k + 0x3F8u);
		a0 = (a0 == 0u) ? 1u : 0u;
		ZmW8(m, k + 0x3F9u, (uint8_t)a0);
		ZmW8(m, k + 0x3F8u, 1u);
	}
	else
	{
		ZmW8(m, k + 0x3F9u, 0u);
		ZmW8(m, k + 0x3F8u, 0u);
	}
	if (ZmR8(m, a1 + 0x15u) != 0u)
	{
		uint32_t a0 = ZmR8(m, k + 0x3FAu);
		a0 = (a0 == 0u) ? 1u : 0u;
		ZmW8(m, k + 0x3FBu, (uint8_t)a0);
		ZmW8(m, k + 0x3FAu, 1u);
	}
	else
	{
		ZmW8(m, k + 0x3FBu, 0u);
		ZmW8(m, k + 0x3FAu, 0u);
	}

	ZmW8(m, k + 0x400u, ZmR8(m, k + 0x3FEu));
	ZmW8(m, k + 0x401u, ZmR8(m, k + 0x3FFu));
	f1 = ZmRF(m, k + 0x3ECu);
	v1 = Ps2F_Le(f1, 0u) ? 0u : 1u;
	ZmW8(m, k + 0x3FEu, (uint8_t)v1);
	f1 = ZmRF(m, k + 0x3F4u);
	v1 = Ps2F_Le(f1, 0u) ? 0u : 1u;
	ZmW8(m, k + 0x3FFu, (uint8_t)v1);

	{
		uint32_t a0 = (ZmR8(m, k + 0x400u) == 0u) ? 1u : 0u;
		v1 = ZmR8(m, k + 0x3FEu) & a0;
		ZmW8(m, k + 0x3FCu, (uint8_t)(v1 != 0u));
		a0 = (ZmR8(m, k + 0x401u) == 0u) ? 1u : 0u;
		v1 = ZmR8(m, k + 0x3FFu) & a0;
		ZmW8(m, k + 0x3FDu, (uint8_t)(v1 != 0u));
	}

	// owner's effect bits: clear or mirror the two steering values
	if (ZmR32(m, k + F_K_OWNER) != 0u)
	{
		uint32_t own = ZmR32(m, k + F_K_OWNER);
		if (ExtZp_Kart_1c1ff0(c, ZmR32(m, own + 0x88u), 0xDu) != 0u)
		{
			ZmW32(m, k + 0x3E4u, 0u);
			ZmW32(m, k + 0x3E8u, 0u);
		}
		else
		{
			own = ZmR32(m, k + F_K_OWNER);
			if ((ZmR32(m, own + 0x38u) & 2u) != 0u)
			{
				if (ExtZp_Kart_1c1ff0(c, ZmR32(m, own + 0x88u), 0xBu) != 0u)
				{
					if (ExtZp_Kart_1c2010(c, ZmR32(m, ZmR32(m, k + F_K_OWNER) + 0x88u), 0xBu) != 0u)
					{
						ZmWF(m, k + 0x3E4u, Ps2F_Neg(ZmRF(m, k + 0x3E4u)));
						ZmWF(m, k + 0x3E8u, Ps2F_Neg(ZmRF(m, k + 0x3E8u)));
					}
				}
			}
		}
	}

	// idle in the "wait" mode: reset the whole latch
	if (ZmR32(m, k + F_K_STATE) != 1u)
	{
		uint32_t p = ZmR32(m, F_G_MODE_A);
		uint32_t b;
		if (p != 0u)
		{
			b = ZmR8(m, F_G_MODE_TAB + ZmR32(m, p + 0x124u));
		}
		else
		{
			b = ZmR8(m, F_G_MODE_TAB + ZmR32(m, ZmR32(m, F_G_MODE_B) + 0x24Cu));
		}
		if (b == 0u)
		{
			ZmW32(m, k + 0x3E4u, 0u);
			ZmW32(m, k + 0x3E8u, 0u);
			ZmW32(m, k + 0x3F0u, 0u);
			ZmW32(m, k + 0x3ECu, 0u);
			ZmW32(m, k + 0x3F4u, PS2F_ONE);
			ZmW8(m, k + 0x3F9u, 0u);
			ZmW8(m, k + 0x3F8u, 0u);
			ZmW8(m, k + 0x3FBu, 0u);
			ZmW8(m, k + 0x3FAu, 0u);
			ZmW8(m, k + 0x3FCu, 0u);
			ZmW8(m, k + 0x3FDu, 0u);
			ZmW8(m, k + 0x3FEu, 0u);
			ZmW8(m, k + 0x3FFu, 0u);
			ZmW8(m, k + 0x400u, 0u);
			ZmW8(m, k + 0x401u, 0u);
		}
	}
}

// ---------------------------------------------------------------------------------------
// Wheel-parameter blend (0x192220).
// One blended value: lerp(K[a], K[b], t) = K[a] + t * (K[b] - K[a]) into K[dst].
static void F_Lerp(ExtZpMem *m, uint32_t k, uint32_t a, uint32_t b, uint32_t dst, Ps2F t)
{
	Ps2F f1 = ZmRF(m, k + a);
	Ps2F f0 = ZmRF(m, k + b);
	f0 = Ps2F_Sub(f0, f1);
	f0 = Ps2F_Mul(t, f0);
	f0 = Ps2F_Add(f1, f0);
	ZmWF(m, k + dst, f0);
}
// K[dst] = max(K[dst], K[a] * K[b]) (the clamp from below).
static void F_ClampLow(ExtZpMem *m, uint32_t k, uint32_t dst, uint32_t a, uint32_t b)
{
	Ps2F f2 = ZmRF(m, k + a);
	Ps2F f1 = ZmRF(m, k + b);
	Ps2F f0 = ZmRF(m, k + dst);
	f1 = Ps2F_Mul(f2, f1);
	if (Ps2F_Le(f0, f1))
	{
		f0 = f1;
	}
	ZmWF(m, k + dst, f0);
}
// The four values times three factors (the last factor is used twice).
static void F_Scale4(ExtZpMem *m, uint32_t k, uint32_t fa, uint32_t fb, uint32_t fc)
{
	ZmWF(m, k + 0x470u, Ps2F_Mul(ZmRF(m, k + 0x470u), ZmRF(m, k + fa)));
	ZmWF(m, k + 0x474u, Ps2F_Mul(ZmRF(m, k + 0x474u), ZmRF(m, k + fb)));
	ZmWF(m, k + 0x478u, Ps2F_Mul(ZmRF(m, k + 0x478u), ZmRF(m, k + fc)));
	ZmWF(m, k + 0x47Cu, Ps2F_Mul(ZmRF(m, k + 0x47Cu), ZmRF(m, k + fc)));
}

void ExtZp_Kart_192220(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;
	Ps2F f0, f1, f2;
	uint32_t g;

	f0 = ZmRF(m, k + 0x430u);
	g = ZmR32(m, F_G_MODE_B);
	f2 = Ps2F_FromInt((int32_t)f0);
	f0 = ZmRF(m, g + 0x190u);
	f1 = PS2F_ONE;
	f0 = Ps2F_FromInt((int32_t)f0);
	f2 = Ps2F_Div(f2, f0);
	if (Ps2F_Lt(f2, f1))
	{
		f1 = f2;
	}
	f0 = 0u;
	if (Ps2F_Le(f1, f0))
	{
		f2 = f0;
	}
	else
	{
		f0 = PS2F_ONE;
		if (!Ps2F_Lt(f2, f0))
		{
			f2 = f0;
		}
	}
	F_Lerp(m, k, 0x70u, 0x74u, 0x470u, f2);
	F_Lerp(m, k, 0x7Cu, 0x80u, 0x474u, f2);
	F_Lerp(m, k, 0xBCu, 0xC0u, 0x478u, f2);
	F_Lerp(m, k, 0xF4u, 0xF8u, 0x47Cu, f2);

	if (ZmR32(m, k + F_K_OWNER) != 0u)
	{
		if (ExtZp_Kart_1c1ff0(c, ZmR32(m, ZmR32(m, k + F_K_OWNER) + 0x88u), 2u) != 0u)
		{
			F_ClampLow(m, k, 0x470u, 0x70u, 0x31Cu);
			F_ClampLow(m, k, 0x474u, 0x7Cu, 0x320u);
			F_ClampLow(m, k, 0x478u, 0xBCu, 0x324u);
			F_ClampLow(m, k, 0x47Cu, 0xF4u, 0x324u);
		}
	}

	{
		uint32_t v1 = ZmR32(m, k + F_K_SURF);
		ZmWF(m, k + 0x470u, Ps2F_Mul(ZmRF(m, k + 0x470u), ZmRF(m, v1 + 0x24u)));
		v1 = ZmR32(m, k + F_K_SURF);
		ZmWF(m, k + 0x474u, Ps2F_Mul(ZmRF(m, k + 0x474u), ZmRF(m, v1 + 0x24u)));
	}

	if (ZmR32(m, k + F_K_OWNER) == 0u)
	{
		return;
	}
	if ((int32_t)ZmR32(m, ZmR32(m, k + F_K_OWNER) + 0xA0u) > 0)
	{
		F_Scale4(m, k, 0x328u, 0x32Cu, 0x330u);
	}
	if (ExtZp_Kart_15d3b0(c, ZmR32(m, k + F_K_OWNER)) != 0u)
	{
		F_Scale4(m, k, 0x334u, 0x338u, 0x33Cu);
	}
	{
		const uint32_t s1 = ZmR32(m, ZmR32(m, k + F_K_OWNER) + 0x88u);
		if (ExtZp_Kart_1c1ff0(c, s1, 0xBu) != 0u)
		{
			F_Scale4(m, k, 0x340u, 0x344u, 0x348u);
		}
		if (ExtZp_Kart_1c1ff0(c, s1, 0xCu) != 0u)
		{
			F_Scale4(m, k, 0x358u, 0x35Cu, 0x360u);
		}
		if (ExtZp_Kart_1c1ff0(c, s1, 0xEu) != 0u)
		{
			F_Scale4(m, k, 0x364u, 0x368u, 0x36Cu);
		}
		if (ExtZp_Kart_1c1ff0(c, s1, 3u) == 0u)
		{
			return;
		}
		if (ZmR8(m, s1 + 0x68u) != 0u)
		{
			return;
		}
		F_Scale4(m, k, 0x370u, 0x374u, 0x378u);
	}
}

// ---------------------------------------------------------------------------------------
// Node tracking (0x192650).
// The nearest-segment search of a spline object is worker D's memory form ExtZp_Kart_28b3f0.
#define F_SplineLocal(c, s, i, p) ExtZp_Kart_28b3f0((c), (s), (i), (p))

// The type test of the original (0x512ee0): is the object of the class? (not a path function)
// The source's type test (0x512EE0), ported in part c.
static uint32_t F_IsKindOf(ExtZpCtx *c, uint32_t obj, uint32_t cls)
{
	return ExtZp_Kart_512ee0(c, obj, cls);
}

static void F_TrackOne(ExtZpCtx *c, uint32_t k, uint32_t holderOff, uint32_t nodeOff, uint32_t buf)
{
	ExtZpMem *m = c->m;
	uint32_t s1 = ZmR32(m, ZmR32(m, ZmR32(m, k + holderOff) + 8u) + 0x10u) ;
	// list element: ((holder[8])[0x10])[index * 4]
	s1 = ZmR32(m, s1 + ZmR32(m, k + 0x464u) * 4u);
	if (s1 != 0u)
	{
		if (F_IsKindOf(c, s1, ZmR32(m, F_G_CLASS_ID)) == 0u)
		{
			s1 = 0u;
		}
	}
	ZmW32(m, k + nodeOff, F_SplineLocal(c, s1, ZmR32(m, k + nodeOff), buf));
}

void ExtZp_Kart_192650(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;
	uint32_t buf;
	Ps2F x, y, z;

	if (ZmR32(m, k + 0xA8u) == 0u)
	{
		return;
	}
	if (ZmR32(m, k + 0x464u) == 0xFFFFFFFFu)
	{
		return;
	}
	{
		// the body position, as the point of the search (the third vector the original
		// keeps on its stack at +0x40 is never read and is not kept)
		const uint32_t h = ZmR32(m, ZmR32(m, ZmR32(m, k + F_K_RB)) + F_R_BODY);
		Ps2Vec4 pos = ZmRV(m, h + F_H_POS);
		x = pos.v[0];
		y = pos.v[1];
		z = pos.v[2];
	}
	buf = ExtZp_Push(c, 16u);
	ZmWF(m, buf, x);
	ZmWF(m, buf + 4u, y);
	ZmWF(m, buf + 8u, z);
	F_TrackOne(c, k, 0xA8u, 0x468u, buf);
	if (ZmR32(m, k + 0x46Cu) != 0xFFFFFFFFu)
	{
		F_TrackOne(c, k, 0xACu, 0x46Cu, buf);
	}
	ExtZp_Pop(c, 16u);
}

// ---------------------------------------------------------------------------------------
// Timer (0x1927a0).
void ExtZp_Kart_1927a0(ExtZpCtx *c, uint32_t kart, Ps2F dt)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;
	uint32_t state;

	if (Ps2F_Le(ZmRF(m, k + 0x550u), dt))
	{
		ZmW32(m, k + 0x550u, 0u);
		return;
	}
	ZmWF(m, k + 0x550u, Ps2F_Sub(ZmRF(m, k + 0x550u), dt));
	if (Ps2F_Le(ZmRF(m, k + 0x4C8u), 0u))
	{
		ZmW32(m, k + 0x550u, 0u);
		return;
	}
	state = ZmR32(m, k + F_K_STATE);
	if (state == 7u || state == 9u)
	{
		ZmW32(m, k + 0x550u, 0u);
		return;
	}
	if (state == 4u && !Ps2F_Le(ZmRF(m, k + 0x490u), 0u))
	{
		ZmW32(m, k + 0x550u, 0u);
	}
}

// ---------------------------------------------------------------------------------------
// Spring-back along the surface (0x1963d0).
// Adds to the body's velocity the vector L scaled by -(excess) * stiffness / dt.
static void F_Push(ExtZpMem *m, uint32_t h, Ps2Vec4 *l, Ps2F f0)
{
	Ps2Vec4 vel;
	F_MulX(l, f0);
	vel = ZmRV(m, h + F_H_VEL);
	F_AddXyz(&vel, l);
	ZmWV(m, h + F_H_VEL, vel);
}

void ExtZp_Kart_1963d0(ExtZpCtx *c, uint32_t kart, Ps2F dt)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;
	Ps2Vec4 l = {{0u, 0u, 0u, PS2F_ONE}};
	Ps2Vec4 mm = {{0u, 0u, 0u, PS2F_ONE}};
	Ps2F r40[3], f0, f1, f3;
	uint32_t r, h;

	r = ZmR32(m, ZmR32(m, k + F_K_RB));
	if (ZmR8(m, k + F_K_MAGLEV) != 0u)
	{
		l = ZmRV(m, r + 0x20u);
	}
	else
	{
		l = (Ps2Vec4){{0u, 0u, PS2F_ONE, PS2F_ONE}};
	}
	F_Rd3(m, r + 0x40u, r40);
	{
		Ps2F lv[3] = {l.v[0], l.v[1], l.v[2]};
		f3 = F_Dot(r40, lv);
	}
	f1 = ZmRF(m, k + 0x58u);
	if (!Ps2F_Le(f3, f1))
	{
		h = ZmR32(m, r + F_R_BODY);
		f0 = Ps2F_Sub(f3, f1);
		f0 = Ps2F_Neg(f0);
		f1 = ZmRF(m, h + 0x1B0u);
		f0 = Ps2F_Mul(f0, f1);
		f0 = Ps2F_Div(f0, dt);
		F_Push(m, h, &l, f0);
	}
	else
	{
		f0 = Ps2F_Neg(f1);
		if (Ps2F_Lt(f3, f0))
		{
			h = ZmR32(m, r + F_R_BODY);
			f0 = Ps2F_Add(f3, f1);
			f0 = Ps2F_Neg(f0);
			f1 = ZmRF(m, h + 0x1B0u);
			f0 = Ps2F_Mul(f0, f1);
			f0 = Ps2F_Div(f0, dt);
			F_Push(m, h, &l, f0);
		}
	}
	f1 = ZmRF(m, k + 0x42Cu);
	f0 = ZmRF(m, k + 0x54u);
	if (Ps2F_Le(f1, f0))
	{
		return;
	}
	if (ZmR8(m, k + F_K_MAGLEV) != 0u)
	{
		Ps2F r20[3], e;
		Ps2Vec4 a;
		F_Rd3(m, r + 0x20u, r20);
		e = F_Dot(r40, r20);
		mm = ZmRV(m, r + 0x20u);
		F_MulX(&mm, Ps2F_Neg(e));
		a = ZmRV(m, r + 0x40u);
		F_AddXyz(&mm, &a);
	}
	else
	{
		mm = ZmRV(m, r + 0x40u);
		mm.v[2] = 0u;
	}
	{
		int did;
		f3 = F_Norm(&mm, &did);
	}
	if (Ps2F_Le(f3, 0x38D1B717u)) // 9.99999975e-05 (the original's constant)
	{
		return;
	}
	h = ZmR32(m, r + F_R_BODY);
	f1 = ZmRF(m, k + 0x54u);
	f0 = ZmRF(m, k + 0x42Cu);
	{
		Ps2F f2 = ZmRF(m, h + 0x1B0u);
		f0 = Ps2F_Sub(f0, f1);
		f0 = Ps2F_Neg(f0);
		f0 = Ps2F_Mul(f0, f2);
		f0 = Ps2F_Div(f0, dt);
		F_Push(m, h, &mm, f0);
	}
}

// ---------------------------------------------------------------------------------------
// Alignment controller (0x197510).
// f = clamp(x, -1, 1) as the original does it (the lower test on the min(x, 1) copy).
static Ps2F F_Clamp1(Ps2F x)
{
	const Ps2F f21 = 0xBF800000u; // -1
	Ps2F f0 = PS2F_ONE;
	if (Ps2F_Lt(x, PS2F_ONE))
	{
		f0 = x;
	}
	if (Ps2F_Le(f0, f21))
	{
		return f21;
	}
	if (!Ps2F_Lt(x, PS2F_ONE))
	{
		return PS2F_ONE;
	}
	return x;
}

void ExtZp_Kart_197510(ExtZpCtx *c, uint32_t kart, uint32_t want, Ps2F f12, Ps2F f13)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;
	const uint32_t s0 = want;
	const Ps2F one = PS2F_ONE;
	Ps2Vec4 v50 = {{0u, 0u, 0u, one}};
	Ps2Vec4 v40 = {{0u, 0u, 0u, one}};
	Ps2Vec4 v60 = {{0u, 0u, 0u, one}};
	Ps2F f0, f1, f2, f20, w[3], t[3];
	uint32_t r, v60addr, v0, body;

	f0 = Ps2F_Div(f12, f13);
	f1 = Ps2F_Div(one, f12);
	f20 = Ps2F_Mul(f1, f0);
	if (!Ps2F_Lt(f20, f1))
	{
		f20 = f1;
	}
	r = ZmR32(m, ZmR32(m, k + F_K_RB));
	v50 = ZmRV(m, r + 0x10u);
	{
		Ps2Vec4 vf1 = v50, vf2 = ZmRV(m, s0), res;
		Ps2VAcc acc;
		Ps2V_OpMulaAcc(&acc, &vf1, &vf2);
		res = vf1;
		Ps2V_OpMsubAcc(&res, &acc, &vf2, &vf1);
		v50 = res;
	}
	{
		int did;
		F_Norm(&v50, &did);
	}
	F_Rd3(m, s0, w);
	r = ZmR32(m, ZmR32(m, k + F_K_RB));
	F_Rd3(m, r + 0x20u, t);
	f12 = F_Dot(w, t);
	f12 = F_Clamp1(f12);
	f0 = ExtZp_Math_43bc40(c, f12);

	r = ZmR32(m, ZmR32(m, k + F_K_RB));
	F_Rd3(m, r + 0x10u, t);
	F_Rd3(m, s0, w);
	f2 = F_Dot(w, t);
	if (!Ps2F_Le(f2, 0u))
	{
		f0 = Ps2F_Neg(f0);
	}
	v40 = ZmRV(m, r + 0x30u);
	f0 = Ps2F_Mul(f0, f20);
	F_MulX(&v40, f0);
	v60 = v40;

	r = ZmR32(m, ZmR32(m, k + F_K_RB));
	F_Rd3(m, r + 0x30u, t);
	{
		Ps2F a[3] = {v50.v[0], v50.v[1], v50.v[2]};
		f12 = F_Dot(a, t);
	}
	f12 = F_Clamp1(f12);
	f0 = ExtZp_Math_43bc40(c, f12);

	r = ZmR32(m, ZmR32(m, k + F_K_RB));
	F_Rd3(m, r + 0x30u, t);
	F_Rd3(m, s0, w);
	f2 = F_Dot(w, t);
	if (Ps2F_Lt(f2, 0u))
	{
		f0 = Ps2F_Neg(f0);
	}
	v40 = ZmRV(m, r + 0x10u);
	f0 = Ps2F_Mul(f0, f20);
	F_MulX(&v40, f0);
	F_AddXyz(&v60, &v40);
	r = ZmR32(m, ZmR32(m, k + F_K_RB));
	{
		Ps2Vec4 a = ZmRV(m, r + 0x70u);
		F_AddXyz(&v60, &a);
	}
	v60addr = ExtZp_Push(c, 16u);
	ZmWV(m, v60addr, v60);
	body = ZmR32(m, ZmR32(m, k + F_K_RB));
	v0 = ExtZp_Kart_136160(c, v60addr);
	ExtZp_Kart_190350(c, ZmR32(m, body + F_R_BODY), v0);
	ExtZp_Glue_190280(c, body);
	ExtZp_Pop(c, 16u);
}

// ---------------------------------------------------------------------------------------
// Boost zone (0x1979d0).
void ExtZp_Kart_1979d0(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	const uint32_t k = kart;
	const uint32_t h = ZmR32(m, ZmR32(m, ZmR32(m, k + F_K_RB)) + F_R_BODY);
	const Ps2Vec4 pos = ZmRV(m, h + F_H_POS);
	uint32_t pt, phantoms;

	pt = ExtZp_Push(c, 16u);
	ZmWF(m, pt, pos.v[0]);
	ZmWF(m, pt + 4u, pos.v[1]);
	ZmWF(m, pt + 8u, pos.v[2]);
	phantoms = ZmR32(m, F_G_PHANTOMS);
	if (ZmR32(m, k + 0x564u) != 0u)
	{
		if (ExtZp_Kart_23cf10(c, ZmR32(m, k + 0x564u), pt) == 0u)
		{
			ZmW32(m, k + 0x564u, 0u);
		}
		else
		{
			const uint32_t a1 = ZmR32(m, k + 0x564u);
			const uint32_t t1 = ZmR32(m, a1 + 0x40u);
			const uint32_t state = ZmR32(m, k + F_K_STATE);
			if (state != 4u || ZmR8(m, t1 + 0x3Cu) != 0u)
			{
				Ps2Vec4 d = {{ZmRF(m, t1 + 0x30u), ZmRF(m, t1 + 0x34u), ZmRF(m, t1 + 0x38u), PS2F_ONE}};
				Ps2Vec4 f;
				F_MulX(&d, ZmRF(m, t1 + 0x2Cu));
				f = ZmRV(m, k + 0x3B0u);
				F_AddXyz(&f, &d);
				ZmWV(m, k + 0x3B0u, f);
			}
		}
	}
	else
	{
		const uint32_t out = ExtZp_Push(c, 16u);
		ZmW32(m, out, 0u);
		if (ExtZp_Kart_23d110(c, ZmR32(m, phantoms + 0x34u), out, pt) != 0u)
		{
			ZmW32(m, k + 0x564u, ZmR32(m, ZmR32(m, out)));
		}
		ExtZp_Pop(c, 16u);
	}
	ExtZp_Pop(c, 16u);
}
