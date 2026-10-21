// THE ZONE PORT'S START WORLD (physics.source.world; see include/platform/native_extzoneport.h).
//
// The ported physics (native_extzonephys*.c) runs on the source game's own
// objects in their own layout. Their start state comes with the track: the
// converter writes the words of a start world (the physics world, its
// subspace, rigid body system, controller list, the kart object and its body)
// and the pages of the source's own data a step reads (tables of method
// addresses, constants) into physics.source.world:
//   "world": {"version": 1, "origin": text,
//             "step": {"world": "%08x", "dt": "%08x"},       the step's world object and the step time
//             "objects": [["%08x", text], ...],             what lies where (information only)
//             "words": [["%08x", "%08x"], ...],             address, value
//             "pages": [{"addr": "%08x", "bytes": hex of 4096 bytes}, ...]}
// Without the section nothing here holds data and ExtZp_WorldLoad reports 0.
// Unity build: needs the JSON reader (include/platform/engine.h) before it.

#include <platform/native_extzoneport.h>

#include <string.h>

#define EXT_ZW_WORDS_MAX 8192
#define EXT_ZW_PAGES_MAX 128

typedef struct
{
	int present;
	uint32_t world;  // the step's world object (a0 of the world step)
	Ps2F dt;         // the step time of a Reload tick
	int wordCount;
	uint32_t words[EXT_ZW_WORDS_MAX][2];
	int pageCount;
	uint32_t pageAddr[EXT_ZW_PAGES_MAX];
	uint8_t pages[EXT_ZW_PAGES_MAX][4096];
	int bad;
} ExtZpWorld;

global_variable ExtZpWorld s_extZpWorld;

internal int ExtZw_Hex32(const char *s, uint32_t *out)
{
	uint32_t v = 0;
	if (s == NULL || strlen(s) != 8)
		return 0;
	for (int i = 0; i < 8; i++)
	{
		const char ch = s[i];
		const int d = (ch >= '0' && ch <= '9') ? ch - '0' : ((ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : ((ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1));
		if (d < 0)
			return 0;
		v = (v << 4) | (uint32_t)d;
	}
	*out = v;
	return 1;
}

internal int ExtZw_HexBytes(const char *s, uint8_t *out, size_t n)
{
	if (s == NULL || strlen(s) != 2 * n)
		return 0;
	for (size_t i = 0; i < n; i++)
	{
		uint32_t v = 0;
		for (int k = 0; k < 2; k++)
		{
			const char ch = s[2 * i + (size_t)k];
			const int d = (ch >= '0' && ch <= '9') ? ch - '0' : ((ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : ((ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1));
			if (d < 0)
				return 0;
			v = (v << 4) | (uint32_t)d;
		}
		out[i] = (uint8_t)v;
	}
	return 1;
}

void ExtZp_ClearWorld(void)
{
	s_extZpWorld.present = 0;
	s_extZpWorld.wordCount = 0;
	s_extZpWorld.pageCount = 0;
	s_extZpWorld.bad = 0;
	s_extZpWorld.world = 0;
	s_extZpWorld.dt = 0;
}

// physics.source.world (called by the source reader); a bad entry is left out and counted.
void ExtZp_ReadWorld(const ExtJsonDoc *doc, int node)
{
	ExtZpWorld *w = &s_extZpWorld;
	const int step = ExtJson_Get(doc, node, "step");
	const int words = ExtJson_Get(doc, node, "words");
	const int pages = ExtJson_Get(doc, node, "pages");

	ExtZp_ClearWorld();
	if (ExtJson_Type(doc, node) != EXT_JSON_OBJECT)
		return;
	if (!ExtZw_Hex32(ExtJson_String(doc, ExtJson_Get(doc, step, "world"), NULL), &w->world) ||
	    !ExtZw_Hex32(ExtJson_String(doc, ExtJson_Get(doc, step, "dt"), NULL), &w->dt))
	{
		w->bad++;
		return;
	}
	if (ExtJson_Type(doc, words) == EXT_JSON_ARRAY)
	{
		for (int e = ExtJson_First(doc, words); e >= 0; e = ExtJson_Next(doc, e))
		{
			uint32_t a, v;
			if (w->wordCount >= EXT_ZW_WORDS_MAX || ExtJson_Type(doc, e) != EXT_JSON_ARRAY || ExtJson_Count(doc, e) != 2 ||
			    !ExtZw_Hex32(ExtJson_String(doc, ExtJson_At(doc, e, 0), NULL), &a) || !ExtZw_Hex32(ExtJson_String(doc, ExtJson_At(doc, e, 1), NULL), &v) ||
			    (a & 3u) != 0)
			{
				w->bad++;
				continue;
			}
			w->words[w->wordCount][0] = a;
			w->words[w->wordCount][1] = v;
			w->wordCount++;
		}
	}
	if (ExtJson_Type(doc, pages) == EXT_JSON_ARRAY)
	{
		for (int e = ExtJson_First(doc, pages); e >= 0; e = ExtJson_Next(doc, e))
		{
			uint32_t a;
			if (w->pageCount >= EXT_ZW_PAGES_MAX || !ExtZw_Hex32(ExtJson_String(doc, ExtJson_Get(doc, e, "addr"), NULL), &a) || (a & 4095u) != 0 ||
			    !ExtZw_HexBytes(ExtJson_String(doc, ExtJson_Get(doc, e, "bytes"), NULL), w->pages[w->pageCount], 4096))
			{
				w->bad++;
				continue;
			}
			w->pageAddr[w->pageCount++] = a;
		}
	}
	w->present = (w->wordCount > 0 || w->pageCount > 0);
}

int ExtZp_WorldPages(void) { return s_extZpWorld.pageCount; }
int ExtZp_WorldWords(void) { return s_extZpWorld.wordCount; }
int ExtZp_WorldBad(void) { return s_extZpWorld.bad; }

// Writes the start world into the port's memory: first the source's data pages,
// then the words (a word on such a page wins). 1 = loaded; *world / *dt = the
// step's world object and step time.
int ExtZp_WorldLoad(ExtZpMem *m, uint32_t *world, Ps2F *dt)
{
	const ExtZpWorld *w = &s_extZpWorld;
	if (!w->present)
		return 0;
	for (int i = 0; i < w->pageCount; i++)
		ExtZpMem_Write(m, w->pageAddr[i], w->pages[i], 4096);
	for (int i = 0; i < w->wordCount; i++)
		ZmW32(m, w->words[i][0], w->words[i][1]);
	if (world)
		*world = w->world;
	if (dt)
		*dt = w->dt;
	return 1;
}

// The selftest part (--ext-selftest phys): a made-up world section.
void ExtZp_WorldSelftest(int *checks, int *failures, void (*log)(const char *line))
{
	static char text[16384];
	size_t used = 0;
	ExtJsonDoc doc;
	char why[128];
	ExtZpMem *m;
	uint32_t world = 0;
	Ps2F dt = 0;
	int ok;

	used += (size_t)snprintf(text + used, sizeof(text) - used,
	                         "{\"world\": {\"version\": 1, \"step\": {\"world\": \"00a60000\", \"dt\": \"3d088889\"},"
	                         " \"words\": [[\"00a60000\", \"12345678\"], [\"00a60004\", \"x\"], [\"00638010\", \"0000beef\"], [\"00a60002\", \"00000001\"]],"
	                         " \"pages\": [{\"addr\": \"00638000\", \"bytes\": \"");
	for (int i = 0; i < 4096; i++)
		used += (size_t)snprintf(text + used, sizeof(text) - used, "%02x", (unsigned)(i & 0xFF));
	used += (size_t)snprintf(text + used, sizeof(text) - used, "\"}, {\"addr\": \"00638001\", \"bytes\": \"00\"}]}}");
	memset(&doc, 0, sizeof(doc));
	(*checks)++;
	if (!ExtJson_Parse(&doc, text, used, why, (int)sizeof(why)))
	{
		(*failures)++;
		if (log)
			log("zone world FAIL made-up section parses");
		return;
	}
	ExtZp_ReadWorld(&doc, ExtJson_Get(&doc, 0, "world"));
	ExtJson_Free(&doc);
	m = ExtZpMem_Create();
	ok = m != NULL && ExtZp_WorldLoad(m, &world, &dt) && world == 0x00A60000u && dt == 0x3D088889u && ZmR32(m, 0x00A60000u) == 0x12345678u &&
	     ZmR32(m, 0x00638010u) == 0x0000BEEFu && ZmR8(m, 0x00638021u) == 0x21u && s_extZpWorld.wordCount == 2 && s_extZpWorld.pageCount == 1 &&
	     s_extZpWorld.bad == 3;
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		if (log)
			log("zone world FAIL words, pages and bad entries");
	}
	if (m)
		ExtZpMem_Destroy(m);
	ExtZp_ClearWorld();
	(*checks)++;
	if (ExtZp_WorldLoad(NULL, NULL, NULL) != 0)
	{
		(*failures)++;
		if (log)
			log("zone world FAIL nothing after clearing");
	}
}
