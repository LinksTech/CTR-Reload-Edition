#include <platform/engine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// EXTERNAL TRACK - see include/platform/engine.h, EXTERNAL TRACK (round of
// 2026-10-19).
//
// WHAT. --ext-track <dir> puts a track from a folder of its own into the slot
// of the level of --level (the donor). The folder holds exttrack.json (one
// section per part: units, counts, render, sky, uv_anims, collision, race,
// nav, crates, music) and the files the sections name. This file holds the
// switch's state, the JSON reader every part uses, and the DISPATCHER: the
// game's hooks call ExtTrack_On*, and those call the parts' fixed entry points
// (platform/native_extcoll.c, native_exttrack_race.c, native_exttrack_nav.c,
// native_extcrates.c, native_extmusic.c).
//
// WITHOUT THE SWITCH every hook returns after ExtTrack_Active() (one compare of
// a string's first byte): nothing is read, nothing written, nothing logged.
//
// RECOGNITION: never by the level number. The slot is the level --level jumps
// to; ExtTrack_ActiveForLevel answers 1 only for that slot while the switch is
// on. Every part asks it with gGT->levelID.

char g_cfg_extTrackDir[ENGINE_EXTTRACK_PATH_MAX] = "";
int g_cfg_extTrackSlot = -1;
int g_cfg_extRespawnFrame = -1;

#define EXT_TRACK_JSON_NAME "exttrack.json"

global_variable struct
{
	int jsonTried;
	int jsonOk;
	ExtJsonDoc json;
	int levelLoads;
	int instancesLogged;
} s_extTrack;

// --------------------------------------------------------------------------
// The switch's state

int ExtTrack_Active(void)
{
	return g_cfg_extTrackDir[0] != '\0';
}

int ExtTrack_ActiveForLevel(int levelID)
{
	return ExtTrack_Active() && (g_cfg_extTrackSlot >= 0) && (levelID == g_cfg_extTrackSlot) && (levelID != MAIN_MENU_LEVEL);
}

const char *ExtTrack_Dir(void)
{
	return g_cfg_extTrackDir;
}

int ExtTrack_FilePath(const char *name, char *out, int size)
{
	size_t dirLength = strlen(g_cfg_extTrackDir);
	const int separator = (dirLength > 0) && (g_cfg_extTrackDir[dirLength - 1] != '/') && (g_cfg_extTrackDir[dirLength - 1] != '\\');
	int written;

	if ((name == NULL) || (out == NULL) || (size <= 0))
	{
		return 0;
	}
	written = snprintf(out, (size_t)size, "%s%s%s", g_cfg_extTrackDir, separator ? "/" : "", name);
	if ((written < 0) || (written >= size))
	{
		out[0] = '\0';
		return 0;
	}
	return 1;
}

const ExtJsonDoc *ExtTrack_Json(void)
{
	return s_extTrack.jsonOk ? &s_extTrack.json : NULL;
}

int ExtTrack_Section(const char *name)
{
	return s_extTrack.jsonOk ? ExtJson_Get(&s_extTrack.json, 0, name) : -1;
}

// --------------------------------------------------------------------------
// THE JSON READER
//
// One pass over a copy of the text: strings and keys are unescaped in place in
// the copy (an unescaped string is never longer than its source), so every
// key/text pointer stays valid for the life of the document. Numbers through
// strtod on a checked span (the program never changes the C locale). Strict
// JSON (RFC 8259): no comments, no trailing commas, no leading zeros, one root.

typedef struct
{
	ExtJsonDoc *doc;
	char *s;      // the copy (doc->strings)
	size_t n;     // its length (without the NUL)
	size_t at;
	char *why;
	int whySize;
	int failed;
} ExtJsonParser;

internal void ExtJson_Fail(ExtJsonParser *p, const char *what)
{
	size_t i;
	int line = 1;
	int column = 1;

	if (p->failed)
	{
		return;
	}
	p->failed = 1;
	for (i = 0; (i < p->at) && (i < p->n); i++)
	{
		if (p->s[i] == '\n')
		{
			line++;
			column = 1;
		}
		else
		{
			column++;
		}
	}
	if ((p->why != NULL) && (p->whySize > 0))
	{
		snprintf(p->why, (size_t)p->whySize, "%s at line %d column %d", what, line, column);
	}
}

internal int ExtJson_NewNode(ExtJsonParser *p, u8 type)
{
	ExtJsonDoc *d = p->doc;
	ExtJsonNode *node;

	if (d->nodeCount >= d->nodeCap)
	{
		const int cap = (d->nodeCap > 0) ? (d->nodeCap * 2) : 256;
		ExtJsonNode *grown;

		if (cap <= d->nodeCap)
		{
			ExtJson_Fail(p, "too many values");
			return -1;
		}
		grown = (ExtJsonNode *)realloc(d->nodes, (size_t)cap * sizeof(ExtJsonNode));
		if (grown == NULL)
		{
			ExtJson_Fail(p, "out of memory");
			return -1;
		}
		d->nodes = grown;
		d->nodeCap = cap;
	}
	node = &d->nodes[d->nodeCount];
	memset(node, 0, sizeof(*node));
	node->type = type;
	node->first = -1;
	node->last = -1;
	node->next = -1;
	return d->nodeCount++;
}

internal void ExtJson_AddChild(ExtJsonDoc *d, int parent, int child)
{
	ExtJsonNode *pn = &d->nodes[parent];

	if (pn->last >= 0)
	{
		d->nodes[pn->last].next = child;
	}
	else
	{
		pn->first = child;
	}
	pn->last = child;
	pn->count++;
}

internal void ExtJson_SkipSpace(ExtJsonParser *p)
{
	while (p->at < p->n)
	{
		const char c = p->s[p->at];

		if ((c != ' ') && (c != '\t') && (c != '\n') && (c != '\r'))
		{
			break;
		}
		p->at++;
	}
}

internal int ExtJson_Hex4(ExtJsonParser *p, u32 *out)
{
	u32 v = 0;
	int k;

	if (p->at + 4 > p->n)
	{
		return 0;
	}
	for (k = 0; k < 4; k++)
	{
		const char c = p->s[p->at + (size_t)k];

		v <<= 4;
		if ((c >= '0') && (c <= '9'))
		{
			v |= (u32)(c - '0');
		}
		else if ((c >= 'a') && (c <= 'f'))
		{
			v |= (u32)(c - 'a' + 10);
		}
		else if ((c >= 'A') && (c <= 'F'))
		{
			v |= (u32)(c - 'A' + 10);
		}
		else
		{
			return 0;
		}
	}
	p->at += 4;
	*out = v;
	return 1;
}

// A string at p->at (on the opening quote): unescaped in place, NUL-terminated.
// Returns its start in the copy, NULL on an error.
internal const char *ExtJson_ParseString(ExtJsonParser *p)
{
	char *start;
	size_t w;

	p->at++; // the quote
	start = &p->s[p->at];
	w = p->at;
	while (1)
	{
		char c;

		if (p->at >= p->n)
		{
			ExtJson_Fail(p, "unterminated string");
			return NULL;
		}
		c = p->s[p->at];
		if (c == '"')
		{
			p->s[w] = '\0';
			p->at++;
			return start;
		}
		if ((unsigned char)c < 0x20)
		{
			ExtJson_Fail(p, "control character in a string");
			return NULL;
		}
		if (c != '\\')
		{
			p->s[w++] = c;
			p->at++;
			continue;
		}
		p->at++;
		if (p->at >= p->n)
		{
			ExtJson_Fail(p, "unterminated string");
			return NULL;
		}
		c = p->s[p->at++];
		switch (c)
		{
		case '"':
		case '\\':
		case '/':
			p->s[w++] = c;
			break;
		case 'b':
			p->s[w++] = '\b';
			break;
		case 'f':
			p->s[w++] = '\f';
			break;
		case 'n':
			p->s[w++] = '\n';
			break;
		case 'r':
			p->s[w++] = '\r';
			break;
		case 't':
			p->s[w++] = '\t';
			break;
		case 'u':
		{
			u32 cp;

			if (!ExtJson_Hex4(p, &cp))
			{
				ExtJson_Fail(p, "bad \\u escape");
				return NULL;
			}
			if ((cp >= 0xD800u) && (cp <= 0xDBFFu))
			{
				u32 low;

				if ((p->at + 2 > p->n) || (p->s[p->at] != '\\') || (p->s[p->at + 1] != 'u'))
				{
					ExtJson_Fail(p, "lone high surrogate");
					return NULL;
				}
				p->at += 2;
				if (!ExtJson_Hex4(p, &low) || (low < 0xDC00u) || (low > 0xDFFFu))
				{
					ExtJson_Fail(p, "bad low surrogate");
					return NULL;
				}
				cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
			}
			else if ((cp >= 0xDC00u) && (cp <= 0xDFFFu))
			{
				ExtJson_Fail(p, "lone low surrogate");
				return NULL;
			}
			// UTF-8: at most 4 bytes for the 6 or 12 of the escape.
			if (cp < 0x80u)
			{
				p->s[w++] = (char)cp;
			}
			else if (cp < 0x800u)
			{
				p->s[w++] = (char)(0xC0u | (cp >> 6));
				p->s[w++] = (char)(0x80u | (cp & 0x3Fu));
			}
			else if (cp < 0x10000u)
			{
				p->s[w++] = (char)(0xE0u | (cp >> 12));
				p->s[w++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
				p->s[w++] = (char)(0x80u | (cp & 0x3Fu));
			}
			else
			{
				p->s[w++] = (char)(0xF0u | (cp >> 18));
				p->s[w++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
				p->s[w++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
				p->s[w++] = (char)(0x80u | (cp & 0x3Fu));
			}
			break;
		}
		default:
			ExtJson_Fail(p, "bad escape");
			return NULL;
		}
	}
}

internal int ExtJson_IsDigit(char c)
{
	return (c >= '0') && (c <= '9');
}

internal int ExtJson_NumberAt(ExtJsonParser *p, double *out)
{
	const size_t from = p->at;
	char buffer[64];
	size_t length;

	if ((p->at < p->n) && (p->s[p->at] == '-'))
	{
		p->at++;
	}
	if ((p->at >= p->n) || !ExtJson_IsDigit(p->s[p->at]))
	{
		ExtJson_Fail(p, "bad number");
		return 0;
	}
	if (p->s[p->at] == '0')
	{
		p->at++;
		if ((p->at < p->n) && ExtJson_IsDigit(p->s[p->at]))
		{
			ExtJson_Fail(p, "leading zero");
			return 0;
		}
	}
	else
	{
		while ((p->at < p->n) && ExtJson_IsDigit(p->s[p->at]))
		{
			p->at++;
		}
	}
	if ((p->at < p->n) && (p->s[p->at] == '.'))
	{
		p->at++;
		if ((p->at >= p->n) || !ExtJson_IsDigit(p->s[p->at]))
		{
			ExtJson_Fail(p, "bad fraction");
			return 0;
		}
		while ((p->at < p->n) && ExtJson_IsDigit(p->s[p->at]))
		{
			p->at++;
		}
	}
	if ((p->at < p->n) && ((p->s[p->at] == 'e') || (p->s[p->at] == 'E')))
	{
		p->at++;
		if ((p->at < p->n) && ((p->s[p->at] == '+') || (p->s[p->at] == '-')))
		{
			p->at++;
		}
		if ((p->at >= p->n) || !ExtJson_IsDigit(p->s[p->at]))
		{
			ExtJson_Fail(p, "bad exponent");
			return 0;
		}
		while ((p->at < p->n) && ExtJson_IsDigit(p->s[p->at]))
		{
			p->at++;
		}
	}
	length = p->at - from;
	if (length >= sizeof(buffer))
	{
		ExtJson_Fail(p, "number too long");
		return 0;
	}
	memcpy(buffer, &p->s[from], length);
	buffer[length] = '\0';
	*out = strtod(buffer, NULL);
	return 1;
}

internal int ExtJson_Literal(ExtJsonParser *p, const char *word)
{
	const size_t length = strlen(word);

	if ((p->at + length <= p->n) && (memcmp(&p->s[p->at], word, length) == 0))
	{
		p->at += length;
		return 1;
	}
	ExtJson_Fail(p, "unknown word");
	return 0;
}

// One value at p->at; returns its node, -1 on an error.
internal int ExtJson_Value(ExtJsonParser *p, int depth)
{
	char c;
	int node;

	ExtJson_SkipSpace(p);
	if (p->at >= p->n)
	{
		ExtJson_Fail(p, "value expected");
		return -1;
	}
	if (depth > EXT_JSON_DEPTH_MAX)
	{
		ExtJson_Fail(p, "nested too deep");
		return -1;
	}
	c = p->s[p->at];
	if (c == '{')
	{
		node = ExtJson_NewNode(p, EXT_JSON_OBJECT);
		if (node < 0)
		{
			return -1;
		}
		p->at++;
		ExtJson_SkipSpace(p);
		if ((p->at < p->n) && (p->s[p->at] == '}'))
		{
			p->at++;
			return node;
		}
		while (1)
		{
			const char *key;
			int child;

			ExtJson_SkipSpace(p);
			if ((p->at >= p->n) || (p->s[p->at] != '"'))
			{
				ExtJson_Fail(p, "key expected");
				return -1;
			}
			key = ExtJson_ParseString(p);
			if (key == NULL)
			{
				return -1;
			}
			ExtJson_SkipSpace(p);
			if ((p->at >= p->n) || (p->s[p->at] != ':'))
			{
				ExtJson_Fail(p, "':' expected");
				return -1;
			}
			p->at++;
			child = ExtJson_Value(p, depth + 1);
			if (child < 0)
			{
				return -1;
			}
			p->doc->nodes[child].key = key;
			ExtJson_AddChild(p->doc, node, child);
			ExtJson_SkipSpace(p);
			if ((p->at < p->n) && (p->s[p->at] == ','))
			{
				p->at++;
				continue;
			}
			if ((p->at < p->n) && (p->s[p->at] == '}'))
			{
				p->at++;
				return node;
			}
			ExtJson_Fail(p, "',' or '}' expected");
			return -1;
		}
	}
	if (c == '[')
	{
		node = ExtJson_NewNode(p, EXT_JSON_ARRAY);
		if (node < 0)
		{
			return -1;
		}
		p->at++;
		ExtJson_SkipSpace(p);
		if ((p->at < p->n) && (p->s[p->at] == ']'))
		{
			p->at++;
			return node;
		}
		while (1)
		{
			const int child = ExtJson_Value(p, depth + 1);

			if (child < 0)
			{
				return -1;
			}
			ExtJson_AddChild(p->doc, node, child);
			ExtJson_SkipSpace(p);
			if ((p->at < p->n) && (p->s[p->at] == ','))
			{
				p->at++;
				continue;
			}
			if ((p->at < p->n) && (p->s[p->at] == ']'))
			{
				p->at++;
				return node;
			}
			ExtJson_Fail(p, "',' or ']' expected");
			return -1;
		}
	}
	if (c == '"')
	{
		const char *text;

		node = ExtJson_NewNode(p, EXT_JSON_STRING);
		if (node < 0)
		{
			return -1;
		}
		text = ExtJson_ParseString(p);
		if (text == NULL)
		{
			return -1;
		}
		p->doc->nodes[node].text = text;
		return node;
	}
	if ((c == '-') || ExtJson_IsDigit(c))
	{
		double number = 0.0;

		node = ExtJson_NewNode(p, EXT_JSON_NUMBER);
		if ((node < 0) || !ExtJson_NumberAt(p, &number))
		{
			return -1;
		}
		p->doc->nodes[node].number = number;
		return node;
	}
	if (c == 't')
	{
		node = ExtJson_NewNode(p, EXT_JSON_TRUE);
		return ((node >= 0) && ExtJson_Literal(p, "true")) ? node : -1;
	}
	if (c == 'f')
	{
		node = ExtJson_NewNode(p, EXT_JSON_FALSE);
		return ((node >= 0) && ExtJson_Literal(p, "false")) ? node : -1;
	}
	if (c == 'n')
	{
		node = ExtJson_NewNode(p, EXT_JSON_NULL);
		return ((node >= 0) && ExtJson_Literal(p, "null")) ? node : -1;
	}
	ExtJson_Fail(p, "unexpected character");
	return -1;
}

void ExtJson_Free(ExtJsonDoc *doc)
{
	if (doc == NULL)
	{
		return;
	}
	free(doc->nodes);
	free(doc->strings);
	memset(doc, 0, sizeof(*doc));
}

int ExtJson_Parse(ExtJsonDoc *doc, const char *text, size_t length, char *why, int whySize)
{
	ExtJsonParser p;
	int root;

	if (why != NULL && whySize > 0)
	{
		why[0] = '\0';
	}
	if (doc == NULL)
	{
		return 0;
	}
	memset(doc, 0, sizeof(*doc));
	memset(&p, 0, sizeof(p));
	p.doc = doc;
	p.why = why;
	p.whySize = whySize;
	if ((text == NULL) || (length > EXT_JSON_TEXT_MAX))
	{
		ExtJson_Fail(&p, (text == NULL) ? "no text" : "text too large");
		return 0;
	}
	doc->strings = (char *)malloc(length + 1u);
	if (doc->strings == NULL)
	{
		ExtJson_Fail(&p, "out of memory");
		return 0;
	}
	memcpy(doc->strings, text, length);
	doc->strings[length] = '\0';
	p.s = doc->strings;
	p.n = length;
	root = ExtJson_Value(&p, 0);
	if (!p.failed)
	{
		ExtJson_SkipSpace(&p);
		if (p.at != p.n)
		{
			ExtJson_Fail(&p, "text after the value");
		}
	}
	if (p.failed || (root != 0))
	{
		if (!p.failed)
		{
			ExtJson_Fail(&p, "no root");
		}
		ExtJson_Free(doc);
		return 0;
	}
	return 1;
}

int ExtJson_ParseFile(ExtJsonDoc *doc, const char *path, char *why, int whySize)
{
	FILE *f;
	long size;
	char *text;
	int ok;

	if (why != NULL && whySize > 0)
	{
		why[0] = '\0';
	}
	if (doc != NULL)
	{
		memset(doc, 0, sizeof(*doc));
	}
	f = (path != NULL) ? fopen(path, "rb") : NULL;
	if (f == NULL)
	{
		if (why != NULL && whySize > 0)
		{
			snprintf(why, (size_t)whySize, "the file does not open");
		}
		return 0;
	}
	if ((fseek(f, 0, SEEK_END) != 0) || ((size = ftell(f)) < 0) || ((unsigned long)size > EXT_JSON_TEXT_MAX) || (fseek(f, 0, SEEK_SET) != 0))
	{
		fclose(f);
		if (why != NULL && whySize > 0)
		{
			snprintf(why, (size_t)whySize, "the file cannot be sized or is larger than %u bytes", (unsigned)EXT_JSON_TEXT_MAX);
		}
		return 0;
	}
	text = (char *)malloc((size_t)size + 1u);
	if (text == NULL)
	{
		fclose(f);
		if (why != NULL && whySize > 0)
		{
			snprintf(why, (size_t)whySize, "out of memory (%ld bytes)", size);
		}
		return 0;
	}
	if (fread(text, 1, (size_t)size, f) != (size_t)size)
	{
		fclose(f);
		free(text);
		if (why != NULL && whySize > 0)
		{
			snprintf(why, (size_t)whySize, "the file cannot be read");
		}
		return 0;
	}
	fclose(f);
	ok = ExtJson_Parse(doc, text, (size_t)size, why, whySize);
	free(text);
	return ok;
}

internal const ExtJsonNode *ExtJson_Node(const ExtJsonDoc *doc, int node)
{
	if ((doc == NULL) || (doc->nodes == NULL) || (node < 0) || (node >= doc->nodeCount))
	{
		return NULL;
	}
	return &doc->nodes[node];
}

int ExtJson_Type(const ExtJsonDoc *doc, int node)
{
	const ExtJsonNode *n = ExtJson_Node(doc, node);

	return (n != NULL) ? (int)n->type : EXT_JSON_NULL;
}

int ExtJson_Get(const ExtJsonDoc *doc, int object, const char *key)
{
	const ExtJsonNode *n = ExtJson_Node(doc, object);
	int child;

	if ((n == NULL) || (n->type != EXT_JSON_OBJECT) || (key == NULL))
	{
		return -1;
	}
	for (child = n->first; child >= 0; child = doc->nodes[child].next)
	{
		if ((doc->nodes[child].key != NULL) && (strcmp(doc->nodes[child].key, key) == 0))
		{
			return child;
		}
	}
	return -1;
}

int ExtJson_Count(const ExtJsonDoc *doc, int node)
{
	const ExtJsonNode *n = ExtJson_Node(doc, node);

	return ((n != NULL) && ((n->type == EXT_JSON_ARRAY) || (n->type == EXT_JSON_OBJECT))) ? n->count : 0;
}

int ExtJson_First(const ExtJsonDoc *doc, int node)
{
	const ExtJsonNode *n = ExtJson_Node(doc, node);

	return ((n != NULL) && ((n->type == EXT_JSON_ARRAY) || (n->type == EXT_JSON_OBJECT))) ? n->first : -1;
}

int ExtJson_Next(const ExtJsonDoc *doc, int node)
{
	const ExtJsonNode *n = ExtJson_Node(doc, node);

	return (n != NULL) ? n->next : -1;
}

int ExtJson_At(const ExtJsonDoc *doc, int array, int index)
{
	int child = ExtJson_First(doc, array);

	if (index < 0)
	{
		return -1;
	}
	while ((child >= 0) && (index > 0))
	{
		child = doc->nodes[child].next;
		index--;
	}
	return child;
}

double ExtJson_Number(const ExtJsonDoc *doc, int node, double fallback)
{
	const ExtJsonNode *n = ExtJson_Node(doc, node);

	return ((n != NULL) && (n->type == EXT_JSON_NUMBER)) ? n->number : fallback;
}

const char *ExtJson_String(const ExtJsonDoc *doc, int node, const char *fallback)
{
	const ExtJsonNode *n = ExtJson_Node(doc, node);

	return ((n != NULL) && (n->type == EXT_JSON_STRING)) ? n->text : fallback;
}

int ExtJson_Bool(const ExtJsonDoc *doc, int node, int fallback)
{
	const ExtJsonNode *n = ExtJson_Node(doc, node);

	if (n == NULL)
	{
		return fallback;
	}
	if (n->type == EXT_JSON_TRUE)
	{
		return 1;
	}
	if (n->type == EXT_JSON_FALSE)
	{
		return 0;
	}
	return fallback;
}

int ExtJson_Numbers(const ExtJsonDoc *doc, int array, double *out, int count)
{
	int child = ExtJson_First(doc, array);
	int got = 0;

	if ((ExtJson_Type(doc, array) != EXT_JSON_ARRAY) || (out == NULL))
	{
		return 0;
	}
	while ((child >= 0) && (got < count))
	{
		if (doc->nodes[child].type != EXT_JSON_NUMBER)
		{
			break;
		}
		out[got++] = doc->nodes[child].number;
		child = doc->nodes[child].next;
	}
	return got;
}

// --------------------------------------------------------------------------
// THE DISPATCHER

// exttrack.json, once per run (at the first level load of the track). One
// line either way: the sections found, or why it did not parse.
internal void ExtTrack_LoadJsonOnce(void)
{
	char path[ENGINE_EXTTRACK_PATH_MAX + 32];
	char why[160];
	char sections[256];
	size_t used = 0;
	int child;

	if (s_extTrack.jsonTried)
	{
		return;
	}
	s_extTrack.jsonTried = 1;
	if (!ExtTrack_FilePath(EXT_TRACK_JSON_NAME, path, (int)sizeof(path)))
	{
		Platform_Log("[CTR ExtTrack] the path of %s in '%s' is too long - no data for the parts\n", EXT_TRACK_JSON_NAME, g_cfg_extTrackDir);
		return;
	}
	if (!ExtJson_ParseFile(&s_extTrack.json, path, why, (int)sizeof(why)))
	{
		Platform_Log("[CTR ExtTrack] '%s' refused: %s - no data for the parts\n", path, why);
		return;
	}
	if (ExtJson_Type(&s_extTrack.json, 0) != EXT_JSON_OBJECT)
	{
		Platform_Log("[CTR ExtTrack] '%s' refused: the root is not an object - no data for the parts\n", path);
		ExtJson_Free(&s_extTrack.json);
		return;
	}
	s_extTrack.jsonOk = 1;
	sections[0] = '\0';
	for (child = ExtJson_First(&s_extTrack.json, 0); child >= 0; child = ExtJson_Next(&s_extTrack.json, child))
	{
		const char *key = s_extTrack.json.nodes[child].key;
		const int written = snprintf(&sections[used], sizeof(sections) - used, "%s%s", (used > 0) ? "," : "", (key != NULL) ? key : "?");

		if ((written < 0) || ((size_t)written >= (sizeof(sections) - used)))
		{
			break;
		}
		used += (size_t)written;
	}
	Platform_Log("[CTR ExtTrack] '%s': %d values, sections %s\n", path, s_extTrack.json.nodeCount, sections);
}

// The level of the running game, -1 without a game tracker.
internal int ExtTrack_CurrentLevel(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	return (gGT != NULL) ? (int)gGT->levelID : -1;
}

void ExtTrack_OnLevelLoaded(struct Level *lev)
{
	if (!ExtTrack_Active())
	{
		return;
	}
	if ((lev == NULL) || !ExtTrack_ActiveForLevel(ExtTrack_CurrentLevel()))
	{
		return;
	}
	s_extTrack.levelLoads++;
	ExtTrack_LoadJsonOnce();
	Platform_Log("[CTR ExtTrack] level %d: the external track '%s' takes the slot (load %d, data %s)\n", ExtTrack_CurrentLevel(), g_cfg_extTrackDir,
	             s_extTrack.levelLoads, s_extTrack.jsonOk ? "ok" : "missing");
	ExtColl_OnLevelLoaded(lev);
	ExtRace_OnLevelLoaded(lev);
	ExtNav_OnLevelLoaded(lev);
	ExtMusic_OnLevelLoaded(lev);
}

int ExtTrack_LevInstanceCount(int levelID, int count)
{
	if (!ExtTrack_Active() || !ExtTrack_ActiveForLevel(levelID))
	{
		return count;
	}
	if (!s_extTrack.instancesLogged)
	{
		s_extTrack.instancesLogged = 1;
		Platform_Log("[CTR ExtTrack] level %d: the donor's %d level instances are not made\n", levelID, count);
	}
	return 0;
}

void ExtTrack_OnRaceStart(void)
{
	if (!ExtTrack_Active() || !ExtTrack_ActiveForLevel(ExtTrack_CurrentLevel()))
	{
		return;
	}
	ExtCrates_OnRaceStart();
}

void ExtTrack_OnRestart(void)
{
	if (!ExtTrack_Active() || !ExtTrack_ActiveForLevel(ExtTrack_CurrentLevel()))
	{
		return;
	}
	ExtCrates_OnRestart();
}

void ExtTrack_OnLogicFrame(void)
{
	if (!ExtTrack_Active() || !ExtTrack_ActiveForLevel(ExtTrack_CurrentLevel()))
	{
		return;
	}
	ExtRace_OnLogicFrame();
	ExtCrates_OnLogicFrame();
	ExtMusic_OnLogicFrame();
}

// --------------------------------------------------------------------------
// Selftests

int ExtTrack_Selftest(const char *name)
{
	if (name == NULL)
	{
		return -1;
	}
	if (strcmp(name, "json") == 0)
	{
		return ExtJson_Selftest();
	}
	if (strcmp(name, "coll") == 0)
	{
		return ExtColl_Selftest();
	}
	if (strcmp(name, "race") == 0)
	{
		return ExtRace_Selftest();
	}
	if (strcmp(name, "nav") == 0)
	{
		return ExtNav_Selftest();
	}
	if (strcmp(name, "crates") == 0)
	{
		return ExtCrates_Selftest();
	}
	if (strcmp(name, "music") == 0)
	{
		return ExtMusic_Selftest();
	}
	return -1;
}

internal void ExtJson_Check(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("ext selftest json: %s\n", what);
	}
}

// A text that must be refused, with the word its reason must contain.
internal void ExtJson_CheckRefused(const char *text, const char *reason, int *n, int *failures)
{
	ExtJsonDoc doc;
	char why[160];
	char what[256];
	const int ok = ExtJson_Parse(&doc, text, strlen(text), why, (int)sizeof(why));

	snprintf(what, sizeof(what), "'%s' must be refused for '%s' (got %s: '%s')", text, reason, ok ? "accepted" : "refused", why);
	ExtJson_Check(!ok && (strstr(why, reason) != NULL) && (doc.nodes == NULL) && (doc.strings == NULL), what, n, failures);
	if (ok)
	{
		ExtJson_Free(&doc);
	}
}

int ExtJson_Selftest(void)
{
	static const char sample[] = "{\n"
	                             "  \"units\": {\"scale\": 64, \"origin\": [-7.9, -183.1, 0.0]},\n"
	                             "  \"name\": \"a \\\"quoted\\\" \\\\ word\\n\\u00e9\\ud83d\\ude00\\/\",\n"
	                             "  \"flags\": [true, false, null],\n"
	                             "  \"numbers\": [0, -1.5e3, 12345678901, 2.5E-2, -0],\n"
	                             "  \"empty_object\": {}, \"empty_array\": [],\n"
	                             "  \"twice\": 1, \"twice\": 2,\n"
	                             "  \"nested\": [[[{\"deep\": \"yes\"}]]]\n"
	                             "}\n";
	ExtJsonDoc doc;
	char why[160];
	int n = 0;
	int failures = 0;

	ExtJson_Check(ExtJson_Parse(&doc, sample, strlen(sample), why, (int)sizeof(why)) == 1, why, &n, &failures);
	if (doc.nodes != NULL)
	{
		const int units = ExtJson_Get(&doc, 0, "units");
		const int origin = ExtJson_Get(&doc, units, "origin");
		const int numbers = ExtJson_Get(&doc, 0, "numbers");
		const int flags = ExtJson_Get(&doc, 0, "flags");
		const int nested = ExtJson_Get(&doc, 0, "nested");
		double o[4] = {0.0, 0.0, 0.0, 0.0};
		double v[8];
		static const char expectName[] = "a \"quoted\" \\ word\n\xC3\xA9\xF0\x9F\x98\x80/";

		ExtJson_Check(ExtJson_Type(&doc, 0) == EXT_JSON_OBJECT, "root is an object", &n, &failures);
		ExtJson_Check(ExtJson_Count(&doc, 0) == 9, "root has 9 members (duplicates kept)", &n, &failures);
		ExtJson_Check(ExtJson_Number(&doc, ExtJson_Get(&doc, units, "scale"), 0.0) == 64.0, "units.scale = 64", &n, &failures);
		ExtJson_Check(ExtJson_Numbers(&doc, origin, o, 4) == 3, "origin has 3 numbers", &n, &failures);
		ExtJson_Check((o[0] == -7.9) && (o[1] == -183.1) && (o[2] == 0.0), "origin values", &n, &failures);
		ExtJson_Check(strcmp(ExtJson_String(&doc, ExtJson_Get(&doc, 0, "name"), ""), expectName) == 0, "string escapes, \\u and a surrogate pair", &n, &failures);
		ExtJson_Check(ExtJson_Bool(&doc, ExtJson_At(&doc, flags, 0), -1) == 1, "true", &n, &failures);
		ExtJson_Check(ExtJson_Bool(&doc, ExtJson_At(&doc, flags, 1), -1) == 0, "false", &n, &failures);
		ExtJson_Check(ExtJson_Type(&doc, ExtJson_At(&doc, flags, 2)) == EXT_JSON_NULL, "null", &n, &failures);
		ExtJson_Check(ExtJson_Bool(&doc, ExtJson_At(&doc, flags, 2), 7) == 7, "null is no bool", &n, &failures);
		ExtJson_Check(ExtJson_Numbers(&doc, numbers, v, 8) == 5, "5 numbers", &n, &failures);
		ExtJson_Check((v[0] == 0.0) && (v[1] == -1500.0) && (v[2] == 12345678901.0) && (v[3] == 0.025) && (v[4] == 0.0), "number values", &n, &failures);
		ExtJson_Check(ExtJson_Numbers(&doc, numbers, v, 2) == 2, "count limit of Numbers", &n, &failures);
		ExtJson_Check(ExtJson_Count(&doc, ExtJson_Get(&doc, 0, "empty_object")) == 0, "empty object", &n, &failures);
		ExtJson_Check((ExtJson_Type(&doc, ExtJson_Get(&doc, 0, "empty_array")) == EXT_JSON_ARRAY) &&
		                  (ExtJson_First(&doc, ExtJson_Get(&doc, 0, "empty_array")) == -1),
		              "empty array", &n, &failures);
		ExtJson_Check(ExtJson_Number(&doc, ExtJson_Get(&doc, 0, "twice"), 0.0) == 1.0, "a duplicate key answers with the first", &n, &failures);
		ExtJson_Check(strcmp(ExtJson_String(&doc, ExtJson_Get(&doc, ExtJson_At(&doc, ExtJson_At(&doc, ExtJson_At(&doc, nested, 0), 0), 0), "deep"), ""), "yes") == 0,
		              "nested arrays and objects", &n, &failures);
		// Fallbacks: a missing node (-1) and the wrong type.
		ExtJson_Check(ExtJson_Get(&doc, 0, "missing") == -1, "missing key", &n, &failures);
		ExtJson_Check(ExtJson_Get(&doc, -1, "units") == -1, "lookup on -1", &n, &failures);
		ExtJson_Check(ExtJson_Number(&doc, -1, 3.5) == 3.5, "number fallback on -1", &n, &failures);
		ExtJson_Check(ExtJson_Number(&doc, ExtJson_Get(&doc, 0, "name"), 4.5) == 4.5, "number fallback on a string", &n, &failures);
		ExtJson_Check(strcmp(ExtJson_String(&doc, units, "fb"), "fb") == 0, "string fallback on an object", &n, &failures);
		ExtJson_Check(ExtJson_At(&doc, numbers, 5) == -1, "index past the end", &n, &failures);
		ExtJson_Check(ExtJson_At(&doc, numbers, -1) == -1, "negative index", &n, &failures);
		ExtJson_Check(ExtJson_Count(&doc, ExtJson_Get(&doc, units, "scale")) == 0, "count of a number", &n, &failures);
		ExtJson_Check(ExtJson_Type(&doc, 100000) == EXT_JSON_NULL, "type of a node out of range", &n, &failures);
		ExtJson_Free(&doc);
		ExtJson_Check((doc.nodes == NULL) && (doc.strings == NULL) && (doc.nodeCount == 0), "free clears", &n, &failures);
	}

	// A long array (the keys of a UV animation): every element in order.
	{
		const int count = 5000;
		char *text = (char *)malloc((size_t)count * 12u + 16u);

		ExtJson_Check(text != NULL, "out of memory (long array)", &n, &failures);
		if (text != NULL)
		{
			size_t used = 0;
			int i;
			int ok;

			text[used++] = '[';
			for (i = 0; i < count; i++)
			{
				used += (size_t)snprintf(&text[used], 12u, "%s%d", (i > 0) ? "," : "", i * 3);
			}
			text[used++] = ']';
			ok = ExtJson_Parse(&doc, text, used, why, (int)sizeof(why));
			ExtJson_Check(ok && (ExtJson_Count(&doc, 0) == count), "long array count", &n, &failures);
			if (ok)
			{
				int child;
				int i2 = 0;
				int inOrder = 1;

				for (child = ExtJson_First(&doc, 0); child >= 0; child = ExtJson_Next(&doc, child))
				{
					inOrder = inOrder && (ExtJson_Number(&doc, child, -1.0) == (double)(i2 * 3));
					i2++;
				}
				ExtJson_Check(inOrder && (i2 == count), "long array order", &n, &failures);
				ExtJson_Check(ExtJson_Number(&doc, ExtJson_At(&doc, 0, count - 1), -1.0) == (double)((count - 1) * 3), "long array last", &n, &failures);
				ExtJson_Free(&doc);
			}
			free(text);
		}
	}

	// Depth: EXT_JSON_DEPTH_MAX nested arrays pass, one more is refused.
	{
		char text[2 * (EXT_JSON_DEPTH_MAX + 2) + 1];
		int depth;

		for (depth = EXT_JSON_DEPTH_MAX + 1; depth <= EXT_JSON_DEPTH_MAX + 2; depth++)
		{
			int i;
			int ok;

			for (i = 0; i < depth; i++)
			{
				text[i] = '[';
				text[2 * depth - 1 - i] = ']';
			}
			text[2 * depth] = '\0';
			ok = ExtJson_Parse(&doc, text, strlen(text), why, (int)sizeof(why));
			ExtJson_Check(ok == ((depth == EXT_JSON_DEPTH_MAX + 1) ? 1 : 0), (depth == EXT_JSON_DEPTH_MAX + 1) ? "depth at the limit" : "depth past the limit", &n,
			              &failures);
			if (ok)
			{
				ExtJson_Free(&doc);
			}
		}
	}

	// Refusals, each with its reason.
	ExtJson_CheckRefused("", "value expected", &n, &failures);
	ExtJson_CheckRefused("   ", "value expected", &n, &failures);
	ExtJson_CheckRefused("[1,2,]", "unexpected character", &n, &failures);
	ExtJson_CheckRefused("{\"a\":1,}", "key expected", &n, &failures);
	ExtJson_CheckRefused("{\"a\" 1}", "':' expected", &n, &failures);
	ExtJson_CheckRefused("{\"a\":1 \"b\":2}", "',' or '}' expected", &n, &failures);
	ExtJson_CheckRefused("[1 2]", "',' or ']' expected", &n, &failures);
	ExtJson_CheckRefused("\"open", "unterminated string", &n, &failures);
	ExtJson_CheckRefused("\"tab\there\"", "control character", &n, &failures);
	ExtJson_CheckRefused("\"\\x\"", "bad escape", &n, &failures);
	ExtJson_CheckRefused("\"\\u12g4\"", "bad \\u escape", &n, &failures);
	ExtJson_CheckRefused("\"\\ud83d\"", "lone high surrogate", &n, &failures);
	ExtJson_CheckRefused("\"\\ude00\"", "lone low surrogate", &n, &failures);
	ExtJson_CheckRefused("01", "leading zero", &n, &failures);
	ExtJson_CheckRefused("1.", "bad fraction", &n, &failures);
	ExtJson_CheckRefused("1e", "bad exponent", &n, &failures);
	ExtJson_CheckRefused("-", "bad number", &n, &failures);
	ExtJson_CheckRefused("tru", "unknown word", &n, &failures);
	ExtJson_CheckRefused("{} {}", "text after the value", &n, &failures);
	ExtJson_CheckRefused("// comment\n{}", "unexpected character", &n, &failures);
	ExtJson_CheckRefused("{\"a\":\n  [1,\n   x]}", "line 3 column 4", &n, &failures);

	// A text without a NUL at its end: only length bytes are read.
	{
		const char text[3] = {'[', '7', ']'};
		const int ok = ExtJson_Parse(&doc, text, sizeof(text), why, (int)sizeof(why));

		ExtJson_Check(ok && (ExtJson_Number(&doc, ExtJson_At(&doc, 0, 0), 0.0) == 7.0), "a text without a NUL", &n, &failures);
		if (ok)
		{
			ExtJson_Free(&doc);
		}
	}

	// The file reader: a missing file is refused with its reason.
	ExtJson_Check(ExtJson_ParseFile(&doc, "this file does not exist.json", why, (int)sizeof(why)) == 0 && (strstr(why, "does not open") != NULL),
	              "a missing file", &n, &failures);

	// The switch's state without the switch: nothing is active.
	{
		char savedDir[ENGINE_EXTTRACK_PATH_MAX];
		const int savedSlot = g_cfg_extTrackSlot;
		char path[64];

		memcpy(savedDir, g_cfg_extTrackDir, sizeof(savedDir));
		g_cfg_extTrackDir[0] = '\0';
		g_cfg_extTrackSlot = 14;
		ExtJson_Check(!ExtTrack_Active() && !ExtTrack_ActiveForLevel(14), "off without the switch", &n, &failures);
		ExtJson_Check(ExtTrack_LevInstanceCount(14, 37) == 37, "the donor's instances stay without the switch", &n, &failures);
		snprintf(g_cfg_extTrackDir, sizeof(g_cfg_extTrackDir), "C:\\dir");
		ExtJson_Check(ExtTrack_Active() && ExtTrack_ActiveForLevel(14) && !ExtTrack_ActiveForLevel(13) && !ExtTrack_ActiveForLevel(MAIN_MENU_LEVEL),
		              "on for its slot only", &n, &failures);
		ExtJson_Check(ExtTrack_FilePath("exttrack.json", path, (int)sizeof(path)) && (strcmp(path, "C:\\dir/exttrack.json") == 0), "file path joined", &n,
		              &failures);
		ExtJson_Check(!ExtTrack_FilePath("a-name-that-is-far-too-long-for-the-small-buffer-of-this-check.json", path, 32) && (path[0] == '\0'),
		              "file path too long", &n, &failures);
		g_cfg_extTrackSlot = -1;
		ExtJson_Check(!ExtTrack_ActiveForLevel(14), "no slot without --level", &n, &failures);
		memcpy(g_cfg_extTrackDir, savedDir, sizeof(savedDir));
		g_cfg_extTrackSlot = savedSlot;
	}
	ExtJson_Check(ExtTrack_Selftest("nothing") == -1, "an unknown selftest name", &n, &failures);

	if (failures == 0)
	{
		printf("ext selftest json passed: %d checks, 0 failures\n", n);
		return 0;
	}
	printf("ext selftest json FAILED: %d checks, %d failures\n", n, failures);
	return 1;
}
