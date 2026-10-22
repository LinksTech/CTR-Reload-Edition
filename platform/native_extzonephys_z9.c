// THE ZONE PORT, PART Z9: the kart reset state (state 9) and the body removal helpers (see
// include/platform/native_extzonephys.h for the conventions). Compiles alone (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_z9.h>

#define Z9_G_WORLD 0x006FA190u // global: the physics world pointer
#define Z9_G_ZEROVEC 0x006A2160u // global: the vector (0, 0, 0, 1) used to clear a body's velocities

// An outside call (not ported): the source's argument registers in, v0 out.
static uint32_t Z9Query(ExtZpCtx *c, uint32_t func, uint32_t a0, uint32_t a1)
{
	ExtZpQueryArgs qa;
	uint32_t i;
	for (i = 0; i < 8u; i++)
	{
		qa.a[i] = 0u;
		qa.f[i] = 0u;
	}
	qa.a[0] = a0;
	qa.a[1] = a1;
	qa.func = func;
	return ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa).v0;
}

// Copies the vector at 0x006A2160 to a fresh stack slot (the original does the same with lq / sq) and
// returns the slot's address; the caller pops 16 bytes after use.
static uint32_t Z9ZeroVecCopy(ExtZpCtx *c)
{
	uint32_t slot = ExtZp_Push(c, 16u);
	ZmWV(c->m, slot, ZmRV(c->m, Z9_G_ZEROVEC));
	return slot;
}

// Removes the entity's body from the system: 380ef0(*(*(world + 0x64) + 0x14), *(entity + 0x94)).
void ExtZp_Integ_337480(ExtZpCtx *c, uint32_t world, uint32_t entity)
{
	ExtZpMem *m = c->m;
	uint32_t a0 = ZmR32(m, ZmR32(m, world + 0x64u) + 0x14u);
	uint32_t a1 = ZmR32(m, entity + 0x94u);
	ExtZp_Integ_380ef0(c, a0, a1);
}

// Walks the body's list (head at body + 0x288, count at body + 0x28C) and hands each entry to 36edc0.
void ExtZp_Integ_380ef0(ExtZpCtx *c, uint32_t system, uint32_t body)
{
	ExtZpMem *m = c->m;
	int32_t i = 0;
	while (i < (int32_t)ZmR32(m, body + 0x28Cu))
	{
		uint32_t p = ExtZp_Step_345a10(c, body + 0x288u);
		uint32_t listData = ZmR32(m, p);
		uint32_t entry = ZmR32(m, listData + ((uint32_t)i << 2));
		ExtZp_Integ_36edc0(c, ZmR32(m, system + 0x14u), entry);
		i++;
	}
}

// Kart reset state, entry.
uint32_t ExtZp_State_192c80(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	uint32_t ent, slot;
	ent = ZmR32(m, kart + 0x2Cu);
	ExtZp_Integ_337480(c, ZmR32(m, Z9_G_WORLD), ZmR32(m, ent));
	ent = ZmR32(m, ZmR32(m, kart + 0x2Cu));
	slot = Z9ZeroVecCopy(c);
	slot = ExtZp_Kart_136160(c, slot);
	ExtZp_Kart_13dd40(c, ZmR32(m, ent + 0x94u), slot);
	ExtZp_Glue_136050(c, ent);
	ExtZp_Pop(c, 16u);
	ent = ZmR32(m, ZmR32(m, kart + 0x2Cu));
	slot = Z9ZeroVecCopy(c);
	slot = ExtZp_Kart_136160(c, slot);
	ExtZp_Kart_190350(c, ZmR32(m, ent + 0x94u), slot);
	ExtZp_Glue_190280(c, ent);
	ExtZp_Pop(c, 16u);
	ExtZp_State_1972a0(c, kart);
	ZmW32(m, kart + 0x500u, 0u);
	return 0u;
}

// Kart reset state, update.
void ExtZp_State_194720(ExtZpCtx *c, uint32_t kart, Ps2F dt)
{
	ExtZpMem *m = c->m;
	uint32_t ent, slot;
	Ps2F f0, f1;
	ent = ZmR32(m, ZmR32(m, kart + 0x2Cu));
	slot = Z9ZeroVecCopy(c);
	slot = ExtZp_Kart_136160(c, slot);
	ExtZp_Kart_190350(c, ZmR32(m, ent + 0x94u), slot);
	ExtZp_Glue_190280(c, ent);
	ExtZp_Pop(c, 16u);
	if (ZmR8(m, kart + 0x505u) != 0u)
	{
		ent = ZmR32(m, ZmR32(m, kart + 0x2Cu));
		slot = Z9ZeroVecCopy(c);
		slot = ExtZp_Kart_136160(c, slot);
		ExtZp_Kart_13dd40(c, ZmR32(m, ent + 0x94u), slot);
		ExtZp_Glue_136050(c, ent);
		ExtZp_Pop(c, 16u);
		f0 = ZmRF(m, kart + 0x500u);
	}
	else
	{
		f1 = ZmRF(m, kart + 0x500u);
		f0 = ZmRF(m, kart + 0x308u);
		if (!Ps2F_Le(f1, f0))
		{
			Ps2F saved = ZmRF(m, kart + 0x460u);
			f0 = ZmRF(m, kart + 0x30Cu);
			ZmWF(m, kart + 0x460u, f0);
			ExtZp_State_195e50(c, kart);
			ZmWF(m, kart + 0x460u, saved);
		}
		f0 = ZmRF(m, kart + 0x500u);
	}
	f1 = Ps2F_Add(f0, dt);
	ZmWF(m, kart + 0x500u, f1);
	f0 = ZmRF(m, kart + 0x304u);
	if (!Ps2F_Le(f1, f0))
	{
		(void)Z9Query(c, 0x00337690u, ZmR32(m, Z9_G_WORLD), ZmR32(m, ZmR32(m, kart + 0x2Cu)));
		(void)Z9Query(c, 0x00190540u, kart, 0u);
	}
}

// Kart reset state, exit test.
uint32_t ExtZp_State_194f70(ExtZpCtx *c, uint32_t kart)
{
	Ps2F f1 = ZmRF(c->m, kart + 0x500u);
	Ps2F f0 = ZmRF(c->m, kart + 0x304u);
	return Ps2F_Le(f1, f0) ? 0u : 1u;
}
