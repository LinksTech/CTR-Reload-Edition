// THE ZONE PORT, PART K0: small helpers of the kart's physics glue (see
// include/platform/native_extzonephys.h for the conventions). Included by
// platform/native_extzonephys.c; compiles alone too (offline tests).

#include <platform/native_extzonephys.h>

// The maglev state byte of a kart (kart + EXT_ZP_K_MAGLEV): written only when
// it changes.
void ExtZp_Kart_1967e0(ExtZpCtx *c, uint32_t kart, uint32_t on)
{
	if (ZmR8(c->m, kart + EXT_ZP_K_MAGLEV) != (on & 0xFFu))
	{
		ZmW8(c->m, kart + EXT_ZP_K_MAGLEV, (uint8_t)on);
	}
}

// A body's velocity gets an impulse: velocity.xyz += impulse.xyz * inverse mass
// (the vector unit: vmulx.xyz, then vadd.xyz; w of the velocity kept).
void ExtZp_Kart_136110(ExtZpCtx *c, uint32_t body, uint32_t impulse)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 inv = {{ZmRF(m, body + EXT_ZP_B_INV_MASS), 0u, 0u, 0u}};
	Ps2Vec4 imp = ZmRV(m, impulse);
	Ps2Vec4 bc = Ps2V_Bc(&inv, 0);
	Ps2Vec4 vel;
	Ps2V_Mul(&imp, &imp, &bc, 0xEu);
	vel = ZmRV(m, body + EXT_ZP_B_VELOCITY);
	Ps2V_Add(&vel, &vel, &imp, 0xEu);
	ZmWV(m, body + EXT_ZP_B_VELOCITY, vel);
}

// Copies a whole vector into the body's velocity / angular velocity slot.
void ExtZp_Kart_13dd40(ExtZpCtx *c, uint32_t body, uint32_t v)
{
	ZmWV(c->m, body + EXT_ZP_B_VELOCITY, ZmRV(c->m, v));
}
void ExtZp_Kart_190350(ExtZpCtx *c, uint32_t body, uint32_t v)
{
	ZmWV(c->m, body + EXT_ZP_B_ANGULAR, ZmRV(c->m, v));
}
