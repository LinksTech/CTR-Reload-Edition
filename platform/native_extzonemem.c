// THE MEMORY OF THE ZONE PORT (see include/platform/native_extzonemem.h).

#include <platform/native_extzonemem.h>

#include <stdlib.h>
#include <string.h>

#define EXT_ZM_PAGE_BITS 12u
#define EXT_ZM_PAGE (1u << EXT_ZM_PAGE_BITS)
#define EXT_ZM_PAGES (1u << (32u - EXT_ZM_PAGE_BITS))

struct ExtZpMem
{
	uint8_t **pages; // EXT_ZM_PAGES entries, NULL = absent
	uint32_t *present; // the indices of the present pages, in the order they came
	uint32_t count, cap;
	uint32_t faults;
	uint32_t lastFault; // the address of the last read of an absent page
	// the write journal (ExtZpMem_JournalBegin/End): every write, adjacent ones joined
	int journal;
	uint8_t *jbuf;      // entries {u32 addr, u32 len, len bytes} without padding
	uint32_t jlen, jcap, jcount;
	uint32_t jLastEnd;  // the address after the last entry's bytes
	uint32_t jLastHead; // the offset of the last entry in jbuf
};

static void ExtZpMem_JournalPut(ExtZpMem *m, const void *src, uint32_t n)
{
	if (m->jlen + n > m->jcap)
	{
		uint32_t cap = m->jcap ? m->jcap : 65536u;
		uint8_t *grown;
		while (cap < m->jlen + n)
			cap *= 2u;
		grown = (uint8_t *)realloc(m->jbuf, cap);
		if (grown == NULL)
		{
			m->journal = 0; // out of memory: the journal stops (its caller sees fewer writes)
			return;
		}
		m->jbuf = grown;
		m->jcap = cap;
	}
	memcpy(m->jbuf + m->jlen, src, n);
	m->jlen += n;
}

static void ExtZpMem_JournalAdd(ExtZpMem *m, uint32_t addr, const void *src, uint32_t n)
{
	if ((m->jcount > 0u) && (addr == m->jLastEnd))
	{
		uint32_t len;
		memcpy(&len, m->jbuf + m->jLastHead + 4u, 4);
		len += n;
		memcpy(m->jbuf + m->jLastHead + 4u, &len, 4);
		ExtZpMem_JournalPut(m, src, n);
	}
	else
	{
		m->jLastHead = m->jlen;
		ExtZpMem_JournalPut(m, &addr, 4);
		ExtZpMem_JournalPut(m, &n, 4);
		ExtZpMem_JournalPut(m, src, n);
		m->jcount++;
	}
	m->jLastEnd = addr + n;
}

void ExtZpMem_JournalBegin(ExtZpMem *m)
{
	m->journal = 1;
	m->jlen = 0;
	m->jcount = 0;
	m->jLastEnd = 0;
	m->jLastHead = 0;
}

const uint8_t *ExtZpMem_JournalEnd(ExtZpMem *m, uint32_t *bytes, uint32_t *count)
{
	m->journal = 0;
	*bytes = m->jlen;
	*count = m->jcount;
	return m->jbuf;
}

static int ExtZpMem_CmpIndex(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : (x > y ? 1 : 0);
}

ExtZpMem *ExtZpMem_Create(void)
{
	ExtZpMem *m = (ExtZpMem *)calloc(1, sizeof(ExtZpMem));
	if (m != NULL)
	{
		m->pages = (uint8_t **)calloc(EXT_ZM_PAGES, sizeof(uint8_t *));
		if (m->pages == NULL)
		{
			free(m);
			return NULL;
		}
	}
	return m;
}

void ExtZpMem_Clear(ExtZpMem *m)
{
	for (uint32_t k = 0; k < m->count; k++)
	{
		free(m->pages[m->present[k]]);
		m->pages[m->present[k]] = NULL;
	}
	m->count = 0;
	m->faults = 0;
	m->lastFault = 0;
}

void ExtZpMem_Destroy(ExtZpMem *m)
{
	if (m == NULL)
		return;
	ExtZpMem_Clear(m);
	free(m->jbuf);
	free(m->pages);
	free(m->present);
	free(m);
}

uint32_t ExtZpMem_LastFault(const ExtZpMem *m)
{
	return m->lastFault;
}

uint32_t ExtZpMem_Faults(const ExtZpMem *m)
{
	return m->faults;
}

uint8_t *ExtZpMem_Page(ExtZpMem *m, uint32_t addr, int create)
{
	uint32_t idx = addr >> EXT_ZM_PAGE_BITS;
	uint8_t **p = &m->pages[idx];
	if (*p == NULL && create)
	{
		if (m->count == m->cap)
		{
			uint32_t cap = m->cap ? m->cap * 2u : 64u;
			uint32_t *grown = (uint32_t *)realloc(m->present, cap * sizeof(uint32_t));
			if (grown == NULL)
				return NULL;
			m->present = grown;
			m->cap = cap;
		}
		*p = (uint8_t *)calloc(1, EXT_ZM_PAGE);
		if (*p != NULL)
			m->present[m->count++] = idx;
	}
	return *p;
}

void ExtZpMem_ForEachPage(ExtZpMem *m, void (*fn)(uint32_t pageAddr, uint8_t *bytes, void *user), void *user)
{
	qsort(m->present, m->count, sizeof(uint32_t), ExtZpMem_CmpIndex);
	for (uint32_t k = 0; k < m->count; k++)
		fn(m->present[k] << EXT_ZM_PAGE_BITS, m->pages[m->present[k]], user);
}

void ExtZpMem_Write(ExtZpMem *m, uint32_t addr, const void *src, uint32_t n)
{
	const uint8_t *s = (const uint8_t *)src;
	if (m->journal && (n > 0u))
	{
		ExtZpMem_JournalAdd(m, addr, src, n);
	}
	while (n > 0)
	{
		uint32_t off = addr & (EXT_ZM_PAGE - 1u), k = EXT_ZM_PAGE - off;
		if (k > n)
			k = n;
		memcpy(ExtZpMem_Page(m, addr, 1) + off, s, k);
		addr += k;
		s += k;
		n -= k;
	}
}

void ExtZpMem_Read(ExtZpMem *m, uint32_t addr, void *dst, uint32_t n)
{
	uint8_t *d = (uint8_t *)dst;
	while (n > 0)
	{
		uint32_t off = addr & (EXT_ZM_PAGE - 1u), k = EXT_ZM_PAGE - off;
		const uint8_t *p = ExtZpMem_Page(m, addr, 0);
		if (k > n)
			k = n;
		if (p != NULL)
			memcpy(d, p + off, k);
		else
		{
			memset(d, 0, k);
			m->faults++;
			m->lastFault = addr;
		}
		addr += k;
		d += k;
		n -= k;
	}
}

uint8_t ZmR8(ExtZpMem *m, uint32_t a)
{
	uint8_t v;
	ExtZpMem_Read(m, a, &v, 1);
	return v;
}
uint16_t ZmR16(ExtZpMem *m, uint32_t a)
{
	uint16_t v;
	ExtZpMem_Read(m, a, &v, 2);
	return v;
}
uint32_t ZmR32(ExtZpMem *m, uint32_t a)
{
	uint32_t v;
	ExtZpMem_Read(m, a, &v, 4);
	return v;
}
uint64_t ZmR64(ExtZpMem *m, uint32_t a)
{
	uint64_t v;
	ExtZpMem_Read(m, a, &v, 8);
	return v;
}
void ZmW8(ExtZpMem *m, uint32_t a, uint8_t v) { ExtZpMem_Write(m, a, &v, 1); }
void ZmW16(ExtZpMem *m, uint32_t a, uint16_t v) { ExtZpMem_Write(m, a, &v, 2); }
void ZmW32(ExtZpMem *m, uint32_t a, uint32_t v) { ExtZpMem_Write(m, a, &v, 4); }
void ZmW64(ExtZpMem *m, uint32_t a, uint64_t v) { ExtZpMem_Write(m, a, &v, 8); }
Ps2Vec4 ZmRV(ExtZpMem *m, uint32_t a)
{
	Ps2Vec4 v;
	ExtZpMem_Read(m, a & ~15u, v.v, 16);
	return v;
}
void ZmWV(ExtZpMem *m, uint32_t a, Ps2Vec4 v)
{
	ExtZpMem_Write(m, a & ~15u, v.v, 16);
}

void ExtZp_CtxInit(ExtZpCtx *c, ExtZpMem *m, ExtZpQueryFn query, void *user)
{
	memset(c, 0, sizeof(*c));
	c->m = m;
	c->sp = EXT_ZP_STACK_TOP;
	c->query = query;
	c->user = user;
}

uint32_t ExtZp_Push(ExtZpCtx *c, uint32_t bytes)
{
	static const uint8_t zero[256] = {0};
	uint32_t n = (bytes + 15u) & ~15u;
	c->sp -= n;
	for (uint32_t done = 0; done < n; done += (uint32_t)sizeof(zero))
		ExtZpMem_Write(c->m, c->sp + done, zero, (n - done) < (uint32_t)sizeof(zero) ? (n - done) : (uint32_t)sizeof(zero));
	return c->sp;
}

void ExtZp_Pop(ExtZpCtx *c, uint32_t bytes)
{
	c->sp += (bytes + 15u) & ~15u;
}

ExtZpQueryResult ExtZp_Query(ExtZpCtx *c, int kind, const ExtZpQueryArgs *in)
{
	ExtZpQueryResult r;
	memset(&r, 0, sizeof(r));
	if (c->query != NULL)
		c->query(c, kind, in, &r);
	return r;
}

void ExtZp_Leave(ExtZpCtx *c, uint32_t what)
{
	c->leaves++;
	c->leaveWhat = what;
}
