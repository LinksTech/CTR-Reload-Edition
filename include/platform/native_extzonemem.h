#ifndef NATIVE_EXTZONEMEM_H
#define NATIVE_EXTZONEMEM_H

#include <stdint.h>
#include <platform/native_ps2float.h>

// THE MEMORY OF THE ZONE PORT (platform/native_extzonemem.c).
//
// The ported functions (native_extzonephys*.c, native_extchasecam*.c) keep the
// source game's own data layout: they read and write their objects (the kart,
// its rigid body, the physics world, the camera) in a sparse 32-bit address
// space of 4 KB pages, at the offsets the source uses, and pass 32-bit
// addresses where the source passes pointers. So a step of the port and the
// same step of the original in the offline bench start from the same bytes and
// can be compared byte for byte. Reload's side (native_extzoneport.c) fills the
// objects from Reload's state and reads the results back.
//
// Reads of an address that was never written give 0 and count as a fault
// (ExtZpMem.faults); the port never stops on one.

typedef struct ExtZpMem ExtZpMem;

ExtZpMem *ExtZpMem_Create(void);
void ExtZpMem_Destroy(ExtZpMem *m);
void ExtZpMem_Clear(ExtZpMem *m);                 // every page away
uint32_t ExtZpMem_Faults(const ExtZpMem *m);
uint32_t ExtZpMem_LastFault(const ExtZpMem *m); // the address of the last fault (0 none)
// The page holding addr (4 KB, NULL if absent and create is 0).
uint8_t *ExtZpMem_Page(ExtZpMem *m, uint32_t addr, int create);
// Calls fn for every page present (ascending addresses).
void ExtZpMem_ForEachPage(ExtZpMem *m, void (*fn)(uint32_t pageAddr, uint8_t *bytes, void *user), void *user);
void ExtZpMem_Write(ExtZpMem *m, uint32_t addr, const void *src, uint32_t n);
void ExtZpMem_Read(ExtZpMem *m, uint32_t addr, void *dst, uint32_t n);

uint8_t ZmR8(ExtZpMem *m, uint32_t a);
uint16_t ZmR16(ExtZpMem *m, uint32_t a);
uint32_t ZmR32(ExtZpMem *m, uint32_t a);
uint64_t ZmR64(ExtZpMem *m, uint32_t a);
void ZmW8(ExtZpMem *m, uint32_t a, uint8_t v);
void ZmW16(ExtZpMem *m, uint32_t a, uint16_t v);
void ZmW32(ExtZpMem *m, uint32_t a, uint32_t v);
void ZmW64(ExtZpMem *m, uint32_t a, uint64_t v);
// 128-bit (lq/sq, lqc2/sqc2): the address with its low 4 bits cleared, as the CPU does.
Ps2Vec4 ZmRV(ExtZpMem *m, uint32_t a);
void ZmWV(ExtZpMem *m, uint32_t a, Ps2Vec4 v);
#define ZmRF(m, a) ((Ps2F)ZmR32((m), (a)))
#define ZmWF(m, a, v) ZmW32((m), (a), (uint32_t)(v))

// THE CONTEXT of a call into the port.
//   stack: the port's own stack in the same address space, for locals the
//          source passes by address (ExtZp_Push / ExtZp_Pop); its addresses
//          are never compared (the source's stack lies elsewhere).
//   query: the answer to a call the port does not compute itself (a ray test
//          against the track, the contact list of the collision, a material
//          lookup ...): the source's arguments in, its results out, written
//          into memory by the answering side. kind = EXT_ZP_Q_*.
//   leave: a branch of the source the port does not follow (a state outside
//          the anti-gravity path): the port records it and returns.
enum
{
	EXT_ZP_STACK_TOP = 0x01F00000u,
	EXT_ZP_STACK_BOTTOM = 0x01E00000u,
};
typedef struct ExtZpQueryArgs
{
	uint32_t a[8]; // a0..a3, t0..t3 (low 32 bits)
	Ps2F f[8];     // f12..f19
	uint32_t func; // the source function the query stands for (its address: the step log and the bench replay answer by it)
} ExtZpQueryArgs;
typedef struct ExtZpQueryResult
{
	uint32_t v0, v1;
	Ps2F f0;
} ExtZpQueryResult;
struct ExtZpCtx;
typedef void (*ExtZpQueryFn)(struct ExtZpCtx *c, int kind, const ExtZpQueryArgs *in, ExtZpQueryResult *out);
typedef struct ExtZpCtx
{
	ExtZpMem *m;
	uint32_t sp;
	ExtZpQueryFn query;
	void *user;
	int leaves;          // branches not followed (count)
	uint32_t leaveWhat;  // the last one (EXT_ZP_LEAVE_*)
} ExtZpCtx;

void ExtZp_CtxInit(ExtZpCtx *c, ExtZpMem *m, ExtZpQueryFn query, void *user);
uint32_t ExtZp_Push(ExtZpCtx *c, uint32_t bytes); // 16-byte aligned, zeroed
void ExtZp_Pop(ExtZpCtx *c, uint32_t bytes);
ExtZpQueryResult ExtZp_Query(ExtZpCtx *c, int kind, const ExtZpQueryArgs *in);
void ExtZp_Leave(ExtZpCtx *c, uint32_t what);

#endif
