#include <platform/engine.h>

#include <math.h>
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
	ExtPhys_OnLevelLoaded();
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
	ExtColl_OnLogicFrame();
	ExtPhys_OnLogicFrame();
	ExtRace_OnLogicFrame();
	ExtCrates_OnLogicFrame();
	ExtMusic_OnLogicFrame();
}

// The game's audio pause (MainFrame_TogglePauseAudio): kept for the parts, read
// from any thread (the music mixer polls it).
global_variable volatile int s_extTrackAudioPaused;

void ExtTrack_OnAudioPause(int paused)
{
	if (!ExtTrack_Active())
	{
		return;
	}
	s_extTrackAudioPaused = (paused != 0) ? 1 : 0;
}

int ExtTrack_AudioPaused(void)
{
	return ExtTrack_Active() ? s_extTrackAudioPaused : 0;
}

// --------------------------------------------------------------------------
// THE DRAWING (round of 2026-10-19)
//
// TWO FILES from the render and sky sections of exttrack.json:
//   track.glb  world units and axes (as --engine-static-model). Its nodes whose
//              names begin with EXT_UV_PREFIX belong to an entry of uv_anims;
//              every other node is static.
//   sky.glb    every node drawn on its own, in the order of sky.order (a node
//              not named there is not drawn). sky.follow "xyz": the positions
//              are relative to the eye (the camera's rotation only); anything
//              else ("none", the default): world units like the track.
// LOAD, ONCE PER RUN (the first stage 8 of the slot):
//   textures   one per (image, wrap) of each file, as the static model makes
//              them (Engine_TextureCreate, full resolution, mip chain);
//   static     the static nodes of track.glb as pieces (EngineStatic_BuildPieces
//              with THE NODE FILTER NOT_PREFIX), one GPU vertex buffer each, one
//              index buffer; each piece keeps its ranges split in two lists:
//              opaque/mask and blend;
//   nodes      every uv node of track.glb and every node of sky.glb as one piece
//              of its own (filter EXACT), kept on the CPU (u16 indices, one
//              material): drawn as dynamic geometry, the texture coordinates
//              moved by their animation each frame (no shader of its own).
// PER LEVEL (each stage 8 of the slot): the materials are registered (OPAQUE,
// MASK with the file's cutoff, BLEND as ENGINE_BLEND_ALPHA without depth write;
// the section's "materials" rules can change the mode, draw ONE/ONE ("add") and
// set the depth write - see THE MATERIAL RULES)
// and each piece becomes up to two external meshes.
// DRAW (Engine_FrameCollect, view 0 of the one-player race of the slot):
//   sky        cell 0x3ff, linked BEFORE Engine_SkyCollect so the cell draws it
//              after the engine sky's backdrop (the donor's sky is hidden by the
//              switch, the backdrop is its key colour: any hole shows); the
//              camera of the view (or its rotation only, sky.follow "xyz"), no
//              depth test, no depth write;
//   static     opaque ranges in cell 0x3fe (the track's opaque cell), blend
//              ranges in cell 0x3fc (after every opaque cell of the track), each
//              piece with the view test of the static model;
//   uv nodes   as dynamic geometry in the cell of their material.
// Time of the animations: the frame's game time (Engine_FrameTime, the logic
// timer / 30), so a pause stops them and two runs are the same.
// DRAW TIME ONLY: nothing of the game is written.

#define EXT_UV_PREFIX "uv"
#define EXT_CELL_SKY 0x3ff
#define EXT_CELL_OPAQUE 0x3fe
#define EXT_CELL_BLEND 0x3fc
#define EXT_TEXTURES_MAX 256
#define EXT_RANGES_MAX 1024 // per piece
#define EXT_NODES_MAX 64    // uv nodes of the track plus sky nodes
#define EXT_NODE_NAME_MAX 96
#define EXT_BLEND_CUTOFF (1.0f / 255.0f) // a texel with alpha 0 is not drawn in a blend

typedef struct
{
	int image;
	int wrapU;
	int wrapV;
	u32 texture; // 0xffffffff = missing
} ExtDrawTexture;

// The materials and textures of one file.
typedef struct
{
	int materialCount;
	EngineStaticMaterial *materials;
	int *materialTexture; // index into textures, -1 none
	int *levelMaterial;   // per level: Engine_MaterialRegister index
	int textureCount;
	ExtDrawTexture textures[EXT_TEXTURES_MAX];
	u32 texturesMade;
	u32 texturesMissing;
	u32 counts[3]; // opaque, mask, blend (an "add" material counts as blend)
	// The section's "materials" rules (ExtDraw_MaterialRules): per material
	// EXT_MAT_* (EXT_MAT_KEEP without a rule) and its depth write (-1 keep).
	u8 *ruleKind;
	s8 *ruleDepthWrite;
	u32 rulesApplied;  // materials of the file a rule changed
	u32 rulesAdd;      // of those drawn additively
	u32 rulesUnknown;  // rules naming no material of the file
	u32 rulesRefused;  // rules that did not read
} ExtDrawFile;

// One node drawn as dynamic geometry (a uv node of the track, a sky node).
typedef struct
{
	char name[EXT_NODE_NAME_MAX];
	int sky;      // 1 = of sky.glb
	int material; // of its file
	EngineVertex *base;
	EngineVertex *work;
	u32 vertexCount;
	u16 *indices;
	u32 indexCount;
	float boxMin[3];
	float boxMax[3];
	// Its animation (uv_anims), keyCount 0 = none.
	float *times;
	float *offset; // 2 per key
	int keyCount;
	float period;
} ExtDrawNode;

global_variable struct
{
	int tried;
	int ok;
	int reportOn;
	ExtDrawFile file[2]; // 0 track, 1 sky
	// The static pieces of track.glb.
	int pieceCount;
	u32 vb[ENGINE_STATIC_PIECES_MAX];
	u32 ib;
	u32 pieceVertices[ENGINE_STATIC_PIECES_MAX];
	EngineRange *ranges[ENGINE_STATIC_PIECES_MAX]; // opaque ranges first, then the blend ranges
	u32 opaqueRanges[ENGINE_STATIC_PIECES_MAX];
	u32 blendRanges[ENGINE_STATIC_PIECES_MAX];
	u32 pieceTriangles[ENGINE_STATIC_PIECES_MAX];
	float boxMin[ENGINE_STATIC_PIECES_MAX][3];
	float boxMax[ENGINE_STATIC_PIECES_MAX][3];
	u32 staticTriangles;
	u32 staticPrimitives;
	// The nodes drawn as dynamic geometry, sky nodes in their drawing order.
	int nodeCount;
	ExtDrawNode nodes[EXT_NODES_MAX];
	int uvNodes;
	int skyNodes;
	int skyEyeRelative; // sky.follow "xyz": the camera's rotation only
	int uvAnimsNamed; // nodes named by uv_anims
	int uvAnimsFound;
	// Per level
	int levelReady;
	int levelId;
	const void *level1;
	int meshOpaque[ENGINE_STATIC_PIECES_MAX];
	int meshBlend[ENGINE_STATIC_PIECES_MAX];
	// Counters (the exit line)
	u64 frames;
	u64 skyDraws;
	u64 staticDraws;
	u64 nodeDraws;
	u64 culled;
	u64 refused;
	// THE INSTANCE TINT (render.instance_tint true): samples of the static
	// track's baked light (vertex position, brightness 0..255), see
	// ExtDraw_TintInstance.
	int tintOn;
	s16 (*tintPos)[3];
	u8 *tintLight;
	u32 tintCount;
	u32 tintCap;
	u32 tinted;
} s_extDraw;

// --------------------------------------------------------------------------
// Pure helpers

void ExtTrack_UvOffsetAt(const float *times, const float *offset, int count, float period, float t, float out[2])
{
	int k;

	out[0] = 0.0f;
	out[1] = 0.0f;
	if ((times == NULL) || (offset == NULL) || (count <= 0))
	{
		return;
	}
	if (period > 0.0f)
	{
		t = fmodf(t, period);
		t = (t < 0.0f) ? (t + period) : t;
	}
	if ((count == 1) || (t <= times[0]))
	{
		out[0] = offset[0];
		out[1] = offset[1];
		return;
	}
	for (k = 0; k + 1 < count; k++)
	{
		if (t < times[k + 1])
		{
			const float span = times[k + 1] - times[k];
			const float f = (span > 0.0f) ? ((t - times[k]) / span) : 0.0f;

			out[0] = offset[2 * k] + (offset[2 * (k + 1)] - offset[2 * k]) * f;
			out[1] = offset[2 * k + 1] + (offset[2 * (k + 1) + 1] - offset[2 * k + 1]) * f;
			return;
		}
	}
	out[0] = offset[2 * (count - 1)];
	out[1] = offset[2 * (count - 1) + 1];
}

internal double ExtDraw_Ms(u64 from)
{
	return (double)(SDL_GetPerformanceCounter() - from) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

// --------------------------------------------------------------------------
// Load: materials and textures of one file

internal int ExtDraw_TextureSlot(ExtDrawFile *f, int image, int wrapU, int wrapV)
{
	int i;

	if (image < 0)
	{
		return -1;
	}
	for (i = 0; i < f->textureCount; i++)
	{
		if ((f->textures[i].image == image) && (f->textures[i].wrapU == wrapU) && (f->textures[i].wrapV == wrapV))
		{
			return i;
		}
	}
	if (f->textureCount >= EXT_TEXTURES_MAX)
	{
		return -1;
	}
	i = f->textureCount++;
	f->textures[i].image = image;
	f->textures[i].wrapU = wrapU;
	f->textures[i].wrapV = wrapV;
	f->textures[i].texture = 0xffffffffu;
	return i;
}

// THE MATERIAL RULES (round of 2026-10-20): "materials" in the render section
// (track.glb) and in the sky section (sky.glb), an object by material name:
//   {"blend": "opaque" | "mask" | "alpha" | "add", "depth_write": true | false,
//    "cutoff": 0..1 (mask)}
// They carry the source's render states the glTF cannot say: "add" draws ONE/ONE
// (ENGINE_BLEND_PSX_ADD) in the blend cell, without depth write unless the rule
// says otherwise (a source with SRC_ALPHA/ONE comes with its texture
// premultiplied by the converter); "alpha" is the glTF BLEND, "opaque" and
// "mask" the glTF modes. A material without a rule, and every material when the
// section has no "materials", keeps its glTF mode exactly as before.
#define EXT_MAT_KEEP 0
#define EXT_MAT_OPAQUE 1
#define EXT_MAT_MASK 2
#define EXT_MAT_ALPHA 3
#define EXT_MAT_ADD 4

typedef struct
{
	int kind;       // EXT_MAT_*
	int depthWrite; // 1, 0, -1 keep
	float cutoff;   // mask; < 0 keep
} ExtMatRule;

// One rule object. 1 = read (out set); 0 = not an object, an unknown blend
// word, a depth_write that is no boolean or a cutoff outside 0..1.
internal int ExtTrack_MaterialRule(const ExtJsonDoc *doc, int node, ExtMatRule *out)
{
	static const char *const words[] = {"opaque", "mask", "alpha", "add"};
	const int blend = ExtJson_Get(doc, node, "blend");
	const int depth = ExtJson_Get(doc, node, "depth_write");
	const int cutoff = ExtJson_Get(doc, node, "cutoff");
	int k;

	out->kind = EXT_MAT_KEEP;
	out->depthWrite = -1;
	out->cutoff = -1.0f;
	if (ExtJson_Type(doc, node) != EXT_JSON_OBJECT)
	{
		return 0;
	}
	if (blend >= 0)
	{
		const char *word = ExtJson_String(doc, blend, "");

		for (k = 0; k < 4; k++)
		{
			if (strcmp(word, words[k]) == 0)
			{
				out->kind = EXT_MAT_OPAQUE + k;
			}
		}
		if (out->kind == EXT_MAT_KEEP)
		{
			return 0;
		}
	}
	if (depth >= 0)
	{
		if ((ExtJson_Type(doc, depth) != EXT_JSON_TRUE) && (ExtJson_Type(doc, depth) != EXT_JSON_FALSE))
		{
			return 0;
		}
		out->depthWrite = (ExtJson_Type(doc, depth) == EXT_JSON_TRUE) ? 1 : 0;
	}
	if (cutoff >= 0)
	{
		const double c = ExtJson_Number(doc, cutoff, -1.0);

		if ((ExtJson_Type(doc, cutoff) != EXT_JSON_NUMBER) || (c < 0.0) || (c > 1.0))
		{
			return 0;
		}
		out->cutoff = (float)c;
	}
	return 1;
}

// The material names come from the model's parsed JSON. model->gltf is the
// loader's EngStaticLoadGltf (engine_static_load.c), whose data member is the
// cgltf_data - not the cgltf_data itself (the first build read it as such: no
// name matched, and a sky file with rules crashed).
internal const cgltf_data *ExtDraw_ModelGltf(const EngineStaticModel *model)
{
	const EngStaticLoadGltf *g = (model != NULL) ? (const EngStaticLoadGltf *)model->gltf : NULL;

	return (g != NULL) ? g->data : NULL;
}

// The rules object `rules` of doc (-1: none) for the open model: each rule of a
// material name the file has is applied to the copy of its material (mode,
// cutoff) and kept per material (add, depth write). 1 = done (also without rules).
internal int ExtDraw_ApplyMaterialRules(ExtDrawFile *f, const EngineStaticModel *model, const ExtJsonDoc *doc, int rules, const char *label)
{
	const cgltf_data *gdata = ExtDraw_ModelGltf(model);
	int node;
	int i;

	f->ruleKind = (u8 *)calloc((size_t)f->materialCount, sizeof(u8));
	f->ruleDepthWrite = (s8 *)malloc((size_t)f->materialCount * sizeof(s8));
	if ((f->ruleKind == NULL) || (f->ruleDepthWrite == NULL))
	{
		Platform_Log("[CTR ExtTrack] draw: %s: out of memory (material rules)\n", label);
		return 0;
	}
	memset(f->ruleDepthWrite, -1, (size_t)f->materialCount * sizeof(s8));
	if ((doc == NULL) || (rules < 0) || (gdata == NULL) || ((gdata->materials == NULL) && (gdata->materials_count > 0)))
	{
		return 1;
	}
	if (ExtJson_Type(doc, rules) != EXT_JSON_OBJECT)
	{
		f->rulesRefused++;
		Platform_Log("[CTR ExtTrack] draw: %s: \"materials\" is not an object - no rules\n", label);
		return 1;
	}
	for (node = ExtJson_First(doc, rules); node >= 0; node = ExtJson_Next(doc, node))
	{
		const char *name = doc->nodes[node].key;
		ExtMatRule rule;
		int found = 0;

		if (!ExtTrack_MaterialRule(doc, node, &rule))
		{
			f->rulesRefused++;
			Platform_Log("[CTR ExtTrack] draw: %s: the rule of material '%s' does not read - not applied\n", label, (name != NULL) ? name : "?");
			continue;
		}
		// The last material of the model is the default one (no glTF material).
		for (i = 0; (i < f->materialCount - 1) && (i < (int)gdata->materials_count); i++)
		{
			EngineStaticMaterial *m = &f->materials[i];

			if ((name == NULL) || (gdata->materials[i].name == NULL) || (strcmp(gdata->materials[i].name, name) != 0))
			{
				continue;
			}
			found = 1;
			f->ruleKind[i] = (u8)rule.kind;
			f->ruleDepthWrite[i] = (s8)rule.depthWrite;
			if (rule.kind == EXT_MAT_OPAQUE)
			{
				m->alphaMode = ENGINE_STATIC_ALPHA_OPAQUE;
			}
			else if (rule.kind == EXT_MAT_MASK)
			{
				m->alphaMode = ENGINE_STATIC_ALPHA_MASK;
			}
			else if ((rule.kind == EXT_MAT_ALPHA) || (rule.kind == EXT_MAT_ADD))
			{
				// Both draw in the blend list and the blend cell.
				m->alphaMode = ENGINE_STATIC_ALPHA_BLEND;
			}
			if ((rule.cutoff >= 0.0f) && (m->alphaMode == ENGINE_STATIC_ALPHA_MASK))
			{
				m->alphaCutoff = rule.cutoff;
			}
			f->rulesApplied++;
			f->rulesAdd += (rule.kind == EXT_MAT_ADD) ? 1u : 0u;
		}
		if (!found)
		{
			f->rulesUnknown++;
			Platform_Log("[CTR ExtTrack] draw: %s: rule for material '%s', which the file does not have\n", label, (name != NULL) ? name : "?");
		}
	}
	Platform_Log("[CTR ExtTrack] draw: %s: material rules applied to %u material(s) (add %u), unknown names %u, refused %u\n", label, f->rulesApplied,
	             f->rulesAdd, f->rulesUnknown, f->rulesRefused);
	return 1;
}

// The rules of the section ("render" or "sky") of exttrack.json for the open model.
internal int ExtDraw_MaterialRules(ExtDrawFile *f, const EngineStaticModel *model, int section, const char *label)
{
	const ExtJsonDoc *doc = ExtTrack_Json();

	return ExtDraw_ApplyMaterialRules(f, model, doc, (doc != NULL) ? ExtJson_Get(doc, section, "materials") : -1, label);
}

// The materials of the open model and their textures. 1 = done.
internal int ExtDraw_FileMaterials(ExtDrawFile *f, const EngineStaticModel *model, int section, const char *label)
{
	int i;

	f->materialCount = model->materialCount;
	f->materials = (EngineStaticMaterial *)malloc((size_t)model->materialCount * sizeof(EngineStaticMaterial));
	f->materialTexture = (int *)malloc((size_t)model->materialCount * sizeof(int));
	f->levelMaterial = (int *)malloc((size_t)model->materialCount * sizeof(int));
	if ((model->materialCount < 1) || (f->materials == NULL) || (f->materialTexture == NULL) || (f->levelMaterial == NULL))
	{
		Platform_Log("[CTR ExtTrack] draw: %s: out of memory (material tables)\n", label);
		return 0;
	}
	memcpy(f->materials, model->materials, (size_t)model->materialCount * sizeof(EngineStaticMaterial));
	if (!ExtDraw_MaterialRules(f, model, section, label))
	{
		return 0;
	}
	for (i = 0; i < model->materialCount; i++)
	{
		const EngineStaticMaterial *m = &f->materials[i];

		f->materialTexture[i] = ExtDraw_TextureSlot(f, m->image, m->wrapU, m->wrapV);
		f->levelMaterial[i] = -1;
		f->counts[((m->alphaMode >= 0) && (m->alphaMode <= 2)) ? m->alphaMode : 0]++;
	}
	for (i = 0; i < f->textureCount; i++)
	{
		ExtDrawTexture *t = &f->textures[i];
		EngineStaticImage img;
		EngineTextureDesc desc;
		char imgWhy[96];
		char name[40];

		memset(&img, 0, sizeof(img));
		imgWhy[0] = 0;
		if (!EngineStatic_ReadImage(model, t->image, &img, imgWhy, (int)sizeof(imgWhy)))
		{
			f->texturesMissing++;
			Platform_Log("[CTR ExtTrack] draw: %s: image %d missing: %s\n", label, t->image, imgWhy);
			continue;
		}
		memset(&desc, 0, sizeof(desc));
		snprintf(name, sizeof(name), "ext %s image %d", label, t->image);
		desc.width = img.width;
		desc.height = img.height;
		desc.rgba = img.rgba;
		desc.filter = ENGINE_TEXTURE_FILTERED;
		desc.wrapU = t->wrapU;
		desc.wrapV = t->wrapV;
		desc.name = name;
		t->texture = Engine_TextureCreate(&desc);
		if (t->texture == 0xffffffffu)
		{
			f->texturesMissing++;
			Platform_Log("[CTR ExtTrack] draw: %s: image %d (%dx%d) refused by the texture manager\n", label, t->image, img.width, img.height);
		}
		else
		{
			f->texturesMade++;
		}
		EngineStatic_FreeImage(&img);
	}
	return 1;
}

// --------------------------------------------------------------------------
// Load: the static pieces of track.glb

typedef struct
{
	u32 *indices;
	u32 indexCap;
	u32 indexCount;
	const ExtDrawFile *file;
	char why[96];
} ExtDrawBuild;

// THE INSTANCE TINT (round of 2026-10-20; only with render.instance_tint true).
// The light of the source is baked into the track's vertex colours; the game's
// instances (the crates) carry their own and stay bright in a dark cave. With
// the field the samples of the static pieces are kept at load (position and
// brightness = the largest channel of the vertex colour), and
// ExtDraw_TintInstance gives an instance the brightness of the floor around it
// through the PS1 draw's own fade (RenderBucket_LoadPrimColors: the colour moves
// towards the far colour inst->colorRGBA by idpp->alphaScale / 4096, the far
// colour set by the default setup RB_RETAIL_INST_SETUP_COLOR): far colour black,
// alphaScale = the darkness. Without the field nothing is kept or changed.
#define EXT_TINT_RADIUS 320.0f  // floor samples around the instance (x/z)
#define EXT_TINT_BELOW 260      // ... from this far below its origin
#define EXT_TINT_ABOVE 160      // ... to this far above
#define EXT_TINT_FULL 170.0f    // this bright or more: no tint (first test track: beach 190-255, caves 40-140)
#define EXT_TINT_MAX 0.75f      // at most this much towards black

internal void ExtDraw_TintSamples(const EngineStaticPiece *piece)
{
	u32 i;

	if (!s_extDraw.tintOn || (piece == NULL))
	{
		return;
	}
	if (s_extDraw.tintCount + piece->vertexCount > s_extDraw.tintCap)
	{
		const u32 cap = (s_extDraw.tintCount + piece->vertexCount) * 2u;
		s16(*pos)[3] = (s16(*)[3])realloc(s_extDraw.tintPos, (size_t)cap * sizeof(*pos));
		u8 *light;

		if (pos == NULL)
		{
			return;
		}
		s_extDraw.tintPos = pos;
		light = (u8 *)realloc(s_extDraw.tintLight, (size_t)cap);
		if (light == NULL)
		{
			return;
		}
		s_extDraw.tintLight = light;
		s_extDraw.tintCap = cap;
	}
	for (i = 0; i < piece->vertexCount; i++)
	{
		const EngineVertex *v = &piece->vertices[i];
		const float *p = v->position;
		u8 l = v->color[0];

		if ((p[0] < -32767.0f) || (p[0] > 32767.0f) || (p[1] < -32767.0f) || (p[1] > 32767.0f) || (p[2] < -32767.0f) || (p[2] > 32767.0f))
		{
			continue;
		}
		l = (v->color[1] > l) ? v->color[1] : l;
		l = (v->color[2] > l) ? v->color[2] : l;
		s_extDraw.tintPos[s_extDraw.tintCount][0] = (s16)p[0];
		s_extDraw.tintPos[s_extDraw.tintCount][1] = (s16)p[1];
		s_extDraw.tintPos[s_extDraw.tintCount][2] = (s16)p[2];
		s_extDraw.tintLight[s_extDraw.tintCount] = l;
		s_extDraw.tintCount++;
	}
}

// The darkness (0 = as made, EXT_TINT_MAX at most) for the brightness l.
float ExtTrack_TintFor(float l)
{
	float k = 1.0f - l / EXT_TINT_FULL;

	return (k < 0.0f) ? 0.0f : ((k > EXT_TINT_MAX) ? EXT_TINT_MAX : k);
}

// Gives the instance the light of the track around it (render.instance_tint).
// 1 = tinted; 0 = off, no samples near, or bright enough.
int ExtDraw_TintInstance(struct Instance *inst)
{
	double sum = 0.0;
	u32 n = 0;
	u32 i;
	float k;

	if (!ExtTrack_Active() || !s_extDraw.tintOn || (s_extDraw.tintCount == 0u) || (inst == NULL))
	{
		return 0;
	}
	for (i = 0; i < s_extDraw.tintCount; i++)
	{
		const float dx = (float)s_extDraw.tintPos[i][0] - (float)inst->matrix.t[0];
		const float dz = (float)s_extDraw.tintPos[i][2] - (float)inst->matrix.t[2];
		const int dy = (int)s_extDraw.tintPos[i][1] - inst->matrix.t[1];

		if ((dy >= -EXT_TINT_BELOW) && (dy <= EXT_TINT_ABOVE) && (dx * dx + dz * dz <= EXT_TINT_RADIUS * EXT_TINT_RADIUS))
		{
			sum += s_extDraw.tintLight[i];
			n++;
		}
	}
	if (n == 0u)
	{
		return 0;
	}
	k = ExtTrack_TintFor((float)(sum / (double)n));
	if (k <= 0.0f)
	{
		return 0;
	}
	inst->colorRGBA = 0;
	inst->alphaScale = (s16)(k * 4096.0f);
	s_extDraw.tinted++;
	if (s_extDraw.tinted <= 64u)
	{
		Platform_Log("[CTR ExtTrack] tint: instance at %d %d %d: floor light %.0f of %u sample(s) -> towards black %.2f\n", inst->matrix.t[0], inst->matrix.t[1],
		             inst->matrix.t[2], sum / (double)n, n, k);
	}
	return 1;
}

internal int ExtDraw_OnStaticPiece(void *user, const EngineStaticPiece *piece)
{
	ExtDrawBuild *b = (ExtDrawBuild *)user;
	const int p = s_extDraw.pieceCount;
	u32 r;
	u32 opaque = 0;
	u32 at = 0;

	if ((p >= ENGINE_STATIC_PIECES_MAX) || (piece->indexCount > (b->indexCap - b->indexCount)) || (piece->rangeCount == 0u) ||
	    (piece->rangeCount > EXT_RANGES_MAX))
	{
		snprintf(b->why, sizeof(b->why), "piece %d does not fit the tables (%u ranges)", p, piece->rangeCount);
		return 0;
	}
	s_extDraw.ranges[p] = (EngineRange *)malloc((size_t)piece->rangeCount * sizeof(EngineRange));
	if (s_extDraw.ranges[p] == NULL)
	{
		snprintf(b->why, sizeof(b->why), "out of memory (ranges of piece %d)", p);
		return 0;
	}
	s_extDraw.vb[p] = Engine_RenderStaticVertexBuffer(piece->vertices, piece->vertexCount);
	if (s_extDraw.vb[p] == 0xffffffffu)
	{
		free(s_extDraw.ranges[p]);
		s_extDraw.ranges[p] = NULL;
		snprintf(b->why, sizeof(b->why), "the vertex buffer of piece %d (%u vertices) was refused", p, piece->vertexCount);
		return 0;
	}
	memcpy(&b->indices[b->indexCount], piece->indices, (size_t)piece->indexCount * sizeof(u32));
	ExtDraw_TintSamples(piece);
	// Opaque and mask ranges first, then the blend ranges (the file's order inside each).
	for (r = 0; r < piece->rangeCount; r++)
	{
		const int m = (int)piece->ranges[r].material;

		if ((m < b->file->materialCount) && (b->file->materials[m].alphaMode != ENGINE_STATIC_ALPHA_BLEND))
		{
			s_extDraw.ranges[p][at] = piece->ranges[r];
			s_extDraw.ranges[p][at++].firstIndex += b->indexCount;
			opaque++;
		}
	}
	for (r = 0; r < piece->rangeCount; r++)
	{
		const int m = (int)piece->ranges[r].material;

		if ((m < b->file->materialCount) && (b->file->materials[m].alphaMode == ENGINE_STATIC_ALPHA_BLEND))
		{
			s_extDraw.ranges[p][at] = piece->ranges[r];
			s_extDraw.ranges[p][at++].firstIndex += b->indexCount;
		}
	}
	s_extDraw.opaqueRanges[p] = opaque;
	s_extDraw.blendRanges[p] = at - opaque;
	s_extDraw.pieceVertices[p] = piece->vertexCount;
	s_extDraw.pieceTriangles[p] = piece->indexCount / 3u;
	memcpy(s_extDraw.boxMin[p], piece->boxMin, sizeof(piece->boxMin));
	memcpy(s_extDraw.boxMax[p], piece->boxMax, sizeof(piece->boxMax));
	b->indexCount += piece->indexCount;
	s_extDraw.pieceCount = p + 1;
	return 1;
}

// The static nodes of track.glb (every node not beginning with EXT_UV_PREFIX). 1 = done
// (also with no static node at all).
internal int ExtDraw_LoadStatic(const char *path)
{
	EngineStaticSource src;
	EngineStaticModel model;
	ExtDrawBuild build;
	char why[160];
	int pieces;

	memset(&model, 0, sizeof(model));
	memset(&build, 0, sizeof(build));
	why[0] = 0;
	if (!EngineStatic_SourceFile(path, &src))
	{
		Platform_Log("[CTR ExtTrack] draw: '%s' does not open\n", path);
		return 0;
	}
	src.nodeName = EXT_UV_PREFIX;
	src.nodeMatch = ENGINE_STATIC_NODES_NOT_PREFIX;
	if (!EngineStatic_Open(&src, &model, why, (int)sizeof(why)))
	{
		// No static node is no error (a track made of uv nodes only).
		Platform_Log("[CTR ExtTrack] draw: '%s': no static nodes (%s)\n", path, why);
		EngineStatic_SourceFileClose(&src);
		return 1;
	}
	build.file = &s_extDraw.file[0];
	build.indexCap = model.triangles * 3u;
	build.indices = (model.triangles <= (0xffffffffu / 3u)) ? (u32 *)malloc((size_t)build.indexCap * sizeof(u32)) : NULL;
	if (build.indices == NULL)
	{
		Platform_Log("[CTR ExtTrack] draw: '%s': out of memory (%u triangles)\n", path, model.triangles);
		EngineStatic_Close(&model);
		EngineStatic_SourceFileClose(&src);
		return 0;
	}
	pieces = EngineStatic_BuildPieces(&model, ENGINE_STATIC_PIECE_TRIANGLES, 0u, ExtDraw_OnStaticPiece, &build, why, (int)sizeof(why));
	if ((pieces < 0) || (pieces != s_extDraw.pieceCount) || (build.indexCount != build.indexCap))
	{
		Platform_Log("[CTR ExtTrack] draw: '%s': the pieces were refused: %s\n", path, (build.why[0] != 0) ? build.why : why);
		free(build.indices);
		EngineStatic_Close(&model);
		EngineStatic_SourceFileClose(&src);
		return 0;
	}
	s_extDraw.ib = Engine_RenderStaticIndexBuffer(build.indices, build.indexCount);
	free(build.indices);
	s_extDraw.staticTriangles = model.triangles;
	s_extDraw.staticPrimitives = model.primitives;
	EngineStatic_Close(&model);
	EngineStatic_SourceFileClose(&src);
	if (s_extDraw.ib == 0xffffffffu)
	{
		Platform_Log("[CTR ExtTrack] draw: '%s': the index buffer was refused\n", path);
		return 0;
	}
	return 1;
}

// --------------------------------------------------------------------------
// Load: one node as dynamic geometry

typedef struct
{
	ExtDrawNode *node;
	int pieces;
	char why[96];
} ExtDrawNodeBuild;

internal int ExtDraw_OnNodePiece(void *user, const EngineStaticPiece *piece)
{
	ExtDrawNodeBuild *b = (ExtDrawNodeBuild *)user;
	ExtDrawNode *n = b->node;
	u32 i;

	b->pieces++;
	if ((b->pieces > 1) || (piece->rangeCount != 1u) || (piece->vertexCount > 0xffffu))
	{
		snprintf(b->why, sizeof(b->why), "%s (%u vertices, %u materials)", (piece->vertexCount > 0xffffu) ? "too many vertices for u16" : "more than one material",
		         piece->vertexCount, piece->rangeCount);
		return 0;
	}
	n->base = (EngineVertex *)malloc((size_t)piece->vertexCount * sizeof(EngineVertex));
	n->work = (EngineVertex *)malloc((size_t)piece->vertexCount * sizeof(EngineVertex));
	n->indices = (u16 *)malloc((size_t)piece->indexCount * sizeof(u16));
	if ((n->base == NULL) || (n->work == NULL) || (n->indices == NULL))
	{
		snprintf(b->why, sizeof(b->why), "out of memory");
		return 0;
	}
	memcpy(n->base, piece->vertices, (size_t)piece->vertexCount * sizeof(EngineVertex));
	memcpy(n->work, piece->vertices, (size_t)piece->vertexCount * sizeof(EngineVertex));
	for (i = 0; i < piece->indexCount; i++)
	{
		n->indices[i] = (u16)piece->indices[i];
	}
	n->vertexCount = piece->vertexCount;
	n->indexCount = piece->indexCount;
	n->material = (int)piece->ranges[0].material;
	memcpy(n->boxMin, piece->boxMin, sizeof(piece->boxMin));
	memcpy(n->boxMax, piece->boxMax, sizeof(piece->boxMax));
	return 1;
}

internal void ExtDraw_FreeNode(ExtDrawNode *n)
{
	free(n->base);
	free(n->work);
	free(n->indices);
	free(n->times);
	free(n->offset);
	memset(n, 0, sizeof(*n));
}

// The node `name` of the file at path into the next node slot. 1 = done.
internal int ExtDraw_LoadNode(const char *path, const char *name, int sky)
{
	EngineStaticSource src;
	EngineStaticModel model;
	ExtDrawNodeBuild build;
	ExtDrawNode *n;
	char why[160];
	int pieces;

	if (s_extDraw.nodeCount >= EXT_NODES_MAX)
	{
		Platform_Log("[CTR ExtTrack] draw: node '%s' left out: more than %d nodes\n", name, EXT_NODES_MAX);
		return 0;
	}
	n = &s_extDraw.nodes[s_extDraw.nodeCount];
	memset(n, 0, sizeof(*n));
	memset(&build, 0, sizeof(build));
	why[0] = 0;
	if (!EngineStatic_SourceFile(path, &src))
	{
		return 0;
	}
	src.nodeName = name;
	src.nodeMatch = ENGINE_STATIC_NODES_EXACT;
	if (!EngineStatic_Open(&src, &model, why, (int)sizeof(why)))
	{
		EngineStatic_SourceFileClose(&src);
		return 0;
	}
	build.node = n;
	pieces = EngineStatic_BuildPieces(&model, ENGINE_STATIC_PIECE_TRIANGLES, 0u, ExtDraw_OnNodePiece, &build, why, (int)sizeof(why));
	EngineStatic_Close(&model);
	EngineStatic_SourceFileClose(&src);
	if (pieces != 1)
	{
		Platform_Log("[CTR ExtTrack] draw: node '%s' refused: %s\n", name, (build.why[0] != 0) ? build.why : why);
		ExtDraw_FreeNode(n);
		return 0;
	}
	snprintf(n->name, sizeof(n->name), "%s", name);
	n->sky = sky;
	s_extDraw.nodeCount++;
	return 1;
}

// The node by name, NULL none.
internal ExtDrawNode *ExtDraw_FindNode(const char *name)
{
	int i;

	for (i = 0; i < s_extDraw.nodeCount; i++)
	{
		if (strcmp(s_extDraw.nodes[i].name, name) == 0)
		{
			return &s_extDraw.nodes[i];
		}
	}
	return NULL;
}

// The animation of uv_anims entry `entry` onto node n. 1 = done.
internal int ExtDraw_TakeAnimation(ExtDrawNode *n, const ExtJsonDoc *doc, int entry)
{
	const int times = ExtJson_Get(doc, entry, "times");
	const int offset = ExtJson_Get(doc, entry, "offset");
	const int count = ExtJson_Count(doc, times);
	int child;
	int k;

	if ((count < 1) || (ExtJson_Count(doc, offset) != count))
	{
		return 0;
	}
	n->times = (float *)malloc((size_t)count * sizeof(float));
	n->offset = (float *)malloc((size_t)count * 2u * sizeof(float));
	if ((n->times == NULL) || (n->offset == NULL))
	{
		free(n->times);
		free(n->offset);
		n->times = NULL;
		n->offset = NULL;
		return 0;
	}
	for (k = 0, child = ExtJson_First(doc, times); (child >= 0) && (k < count); child = ExtJson_Next(doc, child), k++)
	{
		n->times[k] = (float)ExtJson_Number(doc, child, 0.0);
	}
	for (k = 0, child = ExtJson_First(doc, offset); (child >= 0) && (k < count); child = ExtJson_Next(doc, child), k++)
	{
		double uv[2] = {0.0, 0.0};

		(void)ExtJson_Numbers(doc, child, uv, 2);
		n->offset[2 * k] = (float)uv[0];
		n->offset[2 * k + 1] = (float)uv[1];
	}
	n->keyCount = count;
	n->period = (float)ExtJson_Number(doc, ExtJson_Get(doc, entry, "period"), 0.0);
	return 1;
}

internal void ExtDraw_Report(void)
{
	Platform_Log("[CTR ExtTrack] draw at exit: frames %llu; sky draws %llu, static draws %llu, node draws %llu, left out by the view test %llu, refused %llu\n",
	             (unsigned long long)s_extDraw.frames, (unsigned long long)s_extDraw.skyDraws, (unsigned long long)s_extDraw.staticDraws,
	             (unsigned long long)s_extDraw.nodeDraws, (unsigned long long)s_extDraw.culled, (unsigned long long)s_extDraw.refused);
}

// The two files, once per run. Sets s_extDraw.ok; one line either way.
internal void ExtDraw_ReadOnce(void)
{
	const ExtJsonDoc *doc = ExtTrack_Json();
	const int render = ExtTrack_Section("render");
	const int sky = ExtTrack_Section("sky");
	const int uvAnims = ExtTrack_Section("uv_anims");
	const u64 started = SDL_GetPerformanceCounter();
	const u32 stagingBefore = NativeGfx_StagingBytes();
	char trackPath[ENGINE_EXTTRACK_PATH_MAX + 64];
	char skyPath[ENGINE_EXTTRACK_PATH_MAX + 64];
	const char *trackName;
	const char *skyName;
	int haveSky;
	int entry;
	int f;

	s_extDraw.tried = 1;
	if (doc == NULL)
	{
		Platform_Log("[CTR ExtTrack] draw: no exttrack.json - nothing drawn\n");
		return;
	}
	trackName = ExtJson_String(doc, ExtJson_Get(doc, render, "glb"), NULL);
	skyName = ExtJson_String(doc, ExtJson_Get(doc, sky, "glb"), NULL);
	if ((trackName == NULL) || !ExtTrack_FilePath(trackName, trackPath, (int)sizeof(trackPath)))
	{
		Platform_Log("[CTR ExtTrack] draw: render.glb missing or too long - nothing drawn\n");
		return;
	}
	haveSky = (skyName != NULL) && ExtTrack_FilePath(skyName, skyPath, (int)sizeof(skyPath));
	s_extDraw.skyEyeRelative = (strcmp(ExtJson_String(doc, ExtJson_Get(doc, sky, "follow"), "none"), "xyz") == 0) ? 1 : 0;

	// The materials and textures of each file (all nodes: materials are the file's).
	for (f = 0; f < 2; f++)
	{
		EngineStaticSource src;
		EngineStaticModel model;
		char why[160];
		const char *path = (f == 0) ? trackPath : skyPath;

		if ((f == 1) && !haveSky)
		{
			continue;
		}
		why[0] = 0;
		if (!EngineStatic_SourceFile(path, &src))
		{
			Platform_Log("[CTR ExtTrack] draw: '%s' does not open%s\n", path, (f == 0) ? " - nothing drawn" : " - no sky");
			if (f == 0)
			{
				return;
			}
			haveSky = 0;
			continue;
		}
		if (!EngineStatic_Open(&src, &model, why, (int)sizeof(why)))
		{
			Platform_Log("[CTR ExtTrack] draw: '%s' refused: %s%s\n", path, why, (f == 0) ? " - nothing drawn" : " - no sky");
			EngineStatic_SourceFileClose(&src);
			if (f == 0)
			{
				return;
			}
			haveSky = 0;
			continue;
		}
		if (!ExtDraw_FileMaterials(&s_extDraw.file[f], &model, (f == 0) ? render : sky, (f == 0) ? "track" : "sky"))
		{
			EngineStatic_Close(&model);
			EngineStatic_SourceFileClose(&src);
			return;
		}
		EngineStatic_Close(&model);
		EngineStatic_SourceFileClose(&src);
	}

	// The static pieces of the track (and the samples of the instance tint).
	s_extDraw.tintOn = ExtJson_Bool(doc, ExtJson_Get(doc, render, "instance_tint"), 0);
	if (!ExtDraw_LoadStatic(trackPath))
	{
		return;
	}
	if (s_extDraw.tintOn)
	{
		Platform_Log("[CTR ExtTrack] draw: instance tint on (render.instance_tint): %u light sample(s) of the static track\n", s_extDraw.tintCount);
	}

	// The sky's nodes, in the order of sky.order, then the rest of the file's
	// nodes is not looked for: a node not named there is not drawn (counted).
	if (haveSky)
	{
		const int order = ExtJson_Get(doc, sky, "order");
		int child;

		for (child = ExtJson_First(doc, order); child >= 0; child = ExtJson_Next(doc, child))
		{
			const char *name = ExtJson_String(doc, child, NULL);

			if ((name != NULL) && ExtDraw_LoadNode(skyPath, name, 1))
			{
				s_extDraw.skyNodes++;
			}
			else
			{
				Platform_Log("[CTR ExtTrack] draw: sky node '%s' not found in '%s'\n", (name != NULL) ? name : "?", skyPath);
			}
		}
	}

	// The uv nodes: each entry's nodes, from the track (or a sky node already loaded).
	for (entry = ExtJson_First(doc, uvAnims); entry >= 0; entry = ExtJson_Next(doc, entry))
	{
		const int nodes = ExtJson_Get(doc, entry, "nodes");
		int child;

		for (child = ExtJson_First(doc, nodes); child >= 0; child = ExtJson_Next(doc, child))
		{
			const char *name = ExtJson_String(doc, child, NULL);
			ExtDrawNode *n;

			if (name == NULL)
			{
				continue;
			}
			s_extDraw.uvAnimsNamed++;
			n = ExtDraw_FindNode(name);
			if ((n == NULL) && ExtDraw_LoadNode(trackPath, name, 0))
			{
				n = &s_extDraw.nodes[s_extDraw.nodeCount - 1];
				s_extDraw.uvNodes++;
			}
			if ((n == NULL) || !ExtDraw_TakeAnimation(n, doc, entry))
			{
				Platform_Log("[CTR ExtTrack] draw: uv node '%s' not found or its keys unusable - not animated\n", name);
				continue;
			}
			s_extDraw.uvAnimsFound++;
		}
	}

	s_extDraw.ok = 1;
	{
		const u32 given = NativeGfx_ShrinkStaging(stagingBefore);
		const ExtDrawFile *t = &s_extDraw.file[0];
		const ExtDrawFile *k = &s_extDraw.file[1];
		u32 nodeTriangles = 0;
		int i;

		for (i = 0; i < s_extDraw.nodeCount; i++)
		{
			nodeTriangles += s_extDraw.nodes[i].indexCount / 3u;
		}
		Platform_Log("[CTR ExtTrack] draw: track '%s': static %u triangles of %u nodes in %d pieces; uv nodes %d; materials %d (opaque %u, mask %u, blend %u), "
		             "textures made %u (missing %u); sky '%s' (%s): nodes %d, materials %d, textures made %u (missing %u); node triangles %u; "
		             "uv animations %d of %d named nodes; staging given back %u KB; load %.0f ms\n",
		             trackPath, s_extDraw.staticTriangles, s_extDraw.staticPrimitives, s_extDraw.pieceCount, s_extDraw.uvNodes, t->materialCount - 1, t->counts[0],
		             t->counts[1], t->counts[2], t->texturesMade, t->texturesMissing, haveSky ? skyPath : "-",
		             s_extDraw.skyEyeRelative ? "relative to the eye" : "world", s_extDraw.skyNodes,
		             (k->materialCount > 0) ? (k->materialCount - 1) : 0, k->texturesMade, k->texturesMissing, nodeTriangles, s_extDraw.uvAnimsFound,
		             s_extDraw.uvAnimsNamed, given / 1024u, ExtDraw_Ms(started));
	}
	{
		const int counts = ExtTrack_Section("counts");

		Platform_Log("[CTR ExtTrack] draw: the data's counts: meshes track %d, sky %d, triangles track %d, sky %d, uv_anims %d\n",
		             (int)ExtJson_Number(doc, ExtJson_Get(doc, counts, "meshes_track"), -1.0), (int)ExtJson_Number(doc, ExtJson_Get(doc, counts, "meshes_sky"), -1.0),
		             (int)ExtJson_Number(doc, ExtJson_Get(doc, counts, "triangles_track"), -1.0),
		             (int)ExtJson_Number(doc, ExtJson_Get(doc, counts, "triangles_sky"), -1.0), (int)ExtJson_Number(doc, ExtJson_Get(doc, counts, "uv_anims"), -1.0));
	}
}

// The materials of one file for this level. 1 = done.
internal int ExtDraw_RegisterFile(ExtDrawFile *f)
{
	int i;

	for (i = 0; i < f->materialCount; i++)
	{
		const EngineStaticMaterial *m = &f->materials[i];
		const int slot = f->materialTexture[i];
		EngineMaterial em;

		memset(&em, 0, sizeof(em));
		if (m->alphaMode == ENGINE_STATIC_ALPHA_BLEND)
		{
			// A material rule "add" (ExtDraw_MaterialRules): ONE/ONE.
			em.blend = ((f->ruleKind != NULL) && (f->ruleKind[i] == EXT_MAT_ADD)) ? ENGINE_BLEND_PSX_ADD : ENGINE_BLEND_ALPHA;
			em.alphaCutoff = EXT_BLEND_CUTOFF;
			em.flags = ENGINE_MATERIAL_NO_DEPTH_WRITE;
		}
		else
		{
			em.blend = (m->alphaMode == ENGINE_STATIC_ALPHA_MASK) ? ENGINE_BLEND_MASK : ENGINE_BLEND_OPAQUE;
			em.alphaCutoff = (m->alphaMode == ENGINE_STATIC_ALPHA_MASK) ? m->alphaCutoff : 0.0f;
		}
		// The rule's depth write, if it says one.
		if ((f->ruleDepthWrite != NULL) && (f->ruleDepthWrite[i] == 1))
		{
			em.flags &= (u16)~ENGINE_MATERIAL_NO_DEPTH_WRITE;
		}
		else if ((f->ruleDepthWrite != NULL) && (f->ruleDepthWrite[i] == 0))
		{
			em.flags |= (u16)ENGINE_MATERIAL_NO_DEPTH_WRITE;
		}
		memcpy(em.tint, m->baseColor, sizeof(em.tint));
		em.flags |= (m->doubleSided != 0) ? (u16)ENGINE_MATERIAL_DOUBLE_SIDED : (u16)0u;
		em.program = ENGINE_PROGRAM_NE;
		if ((slot >= 0) && (f->textures[slot].texture != 0xffffffffu))
		{
			Engine_TextureMaterial(&em, f->textures[slot].texture);
		}
		f->levelMaterial[i] = Engine_MaterialRegister(&em);
		if (f->levelMaterial[i] < 0)
		{
			Platform_Log("[CTR ExtTrack] draw: the material table is full at material %d - not drawn in this level\n", i);
			return 0;
		}
	}
	return 1;
}

// This level's materials and the static pieces' meshes. 1 = ready.
internal int ExtDraw_RegisterLevel(void)
{
	EngineRange ranges[EXT_RANGES_MAX];
	int p;

	if (!ExtDraw_RegisterFile(&s_extDraw.file[0]) || ((s_extDraw.file[1].materialCount > 0) && !ExtDraw_RegisterFile(&s_extDraw.file[1])))
	{
		return 0;
	}
	for (p = 0; p < s_extDraw.pieceCount; p++)
	{
		int list;

		s_extDraw.meshOpaque[p] = -1;
		s_extDraw.meshBlend[p] = -1;
		for (list = 0; list < 2; list++)
		{
			const u32 first = (list == 0) ? 0u : s_extDraw.opaqueRanges[p];
			const u32 count = (list == 0) ? s_extDraw.opaqueRanges[p] : s_extDraw.blendRanges[p];
			EngineExternalMeshDesc desc;
			u32 r;
			int mesh;

			if (count == 0u)
			{
				continue;
			}
			for (r = 0; r < count; r++)
			{
				ranges[r] = s_extDraw.ranges[p][first + r];
				ranges[r].material = (u16)s_extDraw.file[0].levelMaterial[ranges[r].material];
			}
			memset(&desc, 0, sizeof(desc));
			desc.vertexBuffer = s_extDraw.vb[p];
			desc.indexBuffer = s_extDraw.ib;
			desc.verticesPerFrame = s_extDraw.pieceVertices[p];
			desc.frames = 1;
			desc.ranges = ranges;
			desc.rangeCount = count;
			desc.name = (list == 0) ? "ext track piece" : "ext track piece blend";
			mesh = Engine_MeshCreateExternal(&desc);
			if (mesh < 0)
			{
				Platform_Log("[CTR ExtTrack] draw: the mesh table refused piece %d - not drawn in this level\n", p);
				return 0;
			}
			if (list == 0)
			{
				s_extDraw.meshOpaque[p] = mesh;
			}
			else
			{
				s_extDraw.meshBlend[p] = mesh;
			}
		}
	}
	return 1;
}

void Engine_ExtTrackLoad(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!ExtTrack_Active())
	{
		return;
	}
	s_extDraw.levelReady = 0;
	if (!g_cfg_engine || (gGT == NULL) || (gGT->level1 == NULL) || ((gGT->gameMode1 & MAIN_MENU) != 0) || !ExtTrack_ActiveForLevel(gGT->levelID))
	{
		return;
	}
	// The HUD minimap of the track (platform/native_extmap.c; only with a "map" section).
	ExtMap_LevelLoad();
	if (!s_extDraw.reportOn)
	{
		s_extDraw.reportOn = 1;
		Platform_AtExitReport(ExtDraw_Report);
	}
	if (!s_extDraw.tried)
	{
		ExtDraw_ReadOnce();
	}
	if (!s_extDraw.ok || !ExtDraw_RegisterLevel())
	{
		return;
	}
	s_extDraw.levelReady = 1;
	s_extDraw.levelId = gGT->levelID;
	s_extDraw.level1 = gGT->level1;
	Platform_Log("[CTR ExtTrack] draw: level %d - %d + %d materials, %d pieces, %d nodes registered\n", gGT->levelID, s_extDraw.file[0].materialCount,
	             s_extDraw.file[1].materialCount, s_extDraw.pieceCount, s_extDraw.nodeCount);
}

// 1 = this frame draws the external track in view 0.
internal int ExtDraw_Eligible(const struct GameTracker *gGT)
{
	return ExtTrack_Active() && g_cfg_engine && s_extDraw.levelReady && (gGT != NULL) && ((gGT->gameMode1 & MAIN_MENU) == 0) &&
	       (gGT->numPlyrCurrGame == 1) && ((gGT->renderFlags & RENDER_FLAG_DRAW_LEVEL) != 0) && ((const void *)gGT->level1 == s_extDraw.level1) &&
	       ((int)gGT->levelID == s_extDraw.levelId);
}

// One node as dynamic geometry: its texture coordinates moved by its animation at time t.
internal void ExtDraw_NodeDraw(ExtDrawNode *n, float t, const EngineMat4 *clip, int cell, u16 flags, EngineDraw *draw)
{
	const ExtDrawFile *f = &s_extDraw.file[n->sky ? 1 : 0];
	float uv[2] = {0.0f, 0.0f};
	u32 i;

	if (n->keyCount > 0)
	{
		ExtTrack_UvOffsetAt(n->times, n->offset, n->keyCount, n->period, t, uv);
	}
	for (i = 0; i < n->vertexCount; i++)
	{
		n->work[i].texcoord[0] = n->base[i].texcoord[0] + uv[0];
		n->work[i].texcoord[1] = n->base[i].texcoord[1] + uv[1];
	}
	memset(draw, 0, sizeof(*draw));
	draw->mesh = -1;
	draw->clipFromModel = *clip;
	draw->tint[0] = draw->tint[1] = draw->tint[2] = draw->tint[3] = 1.0f;
	draw->materialOverride = -1;
	draw->otCell = (u32)cell;
	draw->flags = flags;
	draw->dynVertices = n->work;
	draw->dynVertexCount = n->vertexCount;
	draw->dynIndices = n->indices;
	draw->dynIndexCount = n->indexCount;
	draw->dynMaterial = ((n->material >= 0) && (n->material < f->materialCount)) ? f->levelMaterial[n->material] : -1;
}

void Engine_ExtTrackSkyCollect(struct GameTracker *gGT)
{
	EngineDraw draws[EXT_NODES_MAX];
	const EngineCamera *cam;
	EngineCamera skyCam;
	EngineMat4 clip;
	float t;
	int count = 0;
	int i;

	if (!ExtTrack_Active() || !ExtDraw_Eligible(gGT) || (s_extDraw.skyNodes == 0))
	{
		return;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		return;
	}
	// sky.follow "xyz": the camera's rotation only (as the engine sky), the sky is
	// relative to the eye; else the view's camera, the sky is in world units.
	skyCam = *cam;
	if (s_extDraw.skyEyeRelative)
	{
		skyCam.viewFromWorld.m[12] = 0.0f;
		skyCam.viewFromWorld.m[13] = 0.0f;
		skyCam.viewFromWorld.m[14] = 0.0f;
	}
	Engine_CameraScreenFromWorld(&skyCam, &clip);
	t = Engine_FrameTime(Engine_SceneBank());
	for (i = 0; i < s_extDraw.nodeCount; i++)
	{
		if (s_extDraw.nodes[i].sky)
		{
			ExtDraw_NodeDraw(&s_extDraw.nodes[i], t, &clip, EXT_CELL_SKY, ENGINE_DRAW_NO_DEPTH, &draws[count++]);
		}
	}
	// Linked in reverse: the cell draws them in the order of sky.order.
	for (i = count - 1; i >= 0; i--)
	{
		if (Engine_RenderSubmitLinked(0, &draws[i], gGT->pushBuffer[0].ptrOT, EXT_CELL_SKY))
		{
			s_extDraw.skyDraws++;
		}
		else
		{
			s_extDraw.refused++;
		}
	}
}

void Engine_ExtTrackCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	EngineMat4 screen;
	float t;
	int p;
	int i;

	if (!ExtTrack_Active() || !ExtDraw_Eligible(gGT))
	{
		return;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		return;
	}
	s_extDraw.frames++;
	ExtMap_Frame(gGT);
	Engine_CameraScreenFromWorld(cam, &screen);
	t = Engine_FrameTime(Engine_SceneBank());
	for (p = 0; p < s_extDraw.pieceCount; p++)
	{
		int list;

		if (!Engine_StaticBoxVisible(cam, s_extDraw.boxMin[p], s_extDraw.boxMax[p]))
		{
			s_extDraw.culled++;
			continue;
		}
		for (list = 0; list < 2; list++)
		{
			const int mesh = (list == 0) ? s_extDraw.meshOpaque[p] : s_extDraw.meshBlend[p];
			const int cell = (list == 0) ? EXT_CELL_OPAQUE : EXT_CELL_BLEND;
			EngineDraw draw;

			if (mesh < 0)
			{
				continue;
			}
			memset(&draw, 0, sizeof(draw));
			draw.mesh = mesh;
			draw.clipFromModel = screen; // the model is in world units
			draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
			draw.materialOverride = -1;
			draw.otCell = (u32)cell;
			draw.dynMaterial = -1;
			if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
			{
				s_extDraw.staticDraws++;
			}
			else
			{
				s_extDraw.refused++;
			}
		}
	}
	for (i = 0; i < s_extDraw.nodeCount; i++)
	{
		ExtDrawNode *n = &s_extDraw.nodes[i];
		const ExtDrawFile *f = &s_extDraw.file[0];
		const int blend = (n->material >= 0) && (n->material < f->materialCount) && (f->materials[n->material].alphaMode == ENGINE_STATIC_ALPHA_BLEND);
		const int cell = blend ? EXT_CELL_BLEND : EXT_CELL_OPAQUE;
		EngineDraw draw;

		if (n->sky)
		{
			continue;
		}
		if (!Engine_StaticBoxVisible(cam, n->boxMin, n->boxMax))
		{
			s_extDraw.culled++;
			continue;
		}
		ExtDraw_NodeDraw(n, t, &screen, cell, 0u, &draw);
		if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
		{
			s_extDraw.nodeDraws++;
		}
		else
		{
			s_extDraw.refused++;
		}
	}
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
	if (strcmp(name, "phys") == 0)
	{
		return ExtPhys_Selftest();
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

	// The UV offset of an animation: linear between keys, repeated with the period.
	{
		static const float times[3] = {0.0f, 1.0f, 3.0f};
		static const float offset[6] = {0.0f, 0.0f, 0.0f, -1.0f, 2.0f, -3.0f};
		float o[2];

		ExtTrack_UvOffsetAt(times, offset, 3, 4.0f, 0.5f, o);
		ExtJson_Check((fabsf(o[0]) < 1e-6f) && (fabsf(o[1] + 0.5f) < 1e-6f), "uv offset between keys 0 and 1", &n, &failures);
		ExtTrack_UvOffsetAt(times, offset, 3, 4.0f, 2.0f, o);
		ExtJson_Check((fabsf(o[0] - 1.0f) < 1e-6f) && (fabsf(o[1] + 2.0f) < 1e-6f), "uv offset between keys 1 and 2", &n, &failures);
		ExtTrack_UvOffsetAt(times, offset, 3, 4.0f, 3.5f, o);
		ExtJson_Check((fabsf(o[0] - 2.0f) < 1e-6f) && (fabsf(o[1] + 3.0f) < 1e-6f), "uv offset after the last key holds it", &n, &failures);
		ExtTrack_UvOffsetAt(times, offset, 3, 4.0f, 4.5f, o);
		ExtJson_Check((fabsf(o[0]) < 1e-6f) && (fabsf(o[1] + 0.5f) < 1e-6f), "uv offset repeats with the period", &n, &failures);
		ExtTrack_UvOffsetAt(times, offset, 3, 0.0f, 100.0f, o);
		ExtJson_Check((fabsf(o[0] - 2.0f) < 1e-6f) && (fabsf(o[1] + 3.0f) < 1e-6f), "uv offset without a period holds the last key", &n, &failures);
		ExtTrack_UvOffsetAt(NULL, offset, 3, 4.0f, 1.0f, o);
		ExtJson_Check((o[0] == 0.0f) && (o[1] == 0.0f), "uv offset without keys is zero", &n, &failures);
	}
	// The material rules (render.materials / sky.materials).
	{
		static const char rules[] = "{\"a\": {\"blend\": \"add\", \"depth_write\": false}, \"b\": {\"blend\": \"alpha\", \"depth_write\": true},"
		                            " \"c\": {\"blend\": \"mask\", \"cutoff\": 0.25}, \"d\": {\"blend\": \"opaque\"}, \"e\": {},"
		                            " \"f\": {\"blend\": \"glow\"}, \"g\": {\"depth_write\": 1}, \"h\": {\"cutoff\": 2}, \"i\": \"add\"}";
		ExtJsonDoc rd;
		ExtMatRule r;

		ExtJson_Check(ExtJson_Parse(&rd, rules, strlen(rules), why, (int)sizeof(why)) == 1, "material rules parse", &n, &failures);
		if (rd.nodes != NULL)
		{
			ExtJson_Check(ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "a"), &r) && (r.kind == EXT_MAT_ADD) && (r.depthWrite == 0) && (r.cutoff < 0.0f),
			              "rule add without depth write", &n, &failures);
			ExtJson_Check(ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "b"), &r) && (r.kind == EXT_MAT_ALPHA) && (r.depthWrite == 1), "rule alpha with depth write",
			              &n, &failures);
			ExtJson_Check(ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "c"), &r) && (r.kind == EXT_MAT_MASK) && (r.depthWrite == -1) && (r.cutoff == 0.25f),
			              "rule mask with cutoff", &n, &failures);
			ExtJson_Check(ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "d"), &r) && (r.kind == EXT_MAT_OPAQUE), "rule opaque", &n, &failures);
			ExtJson_Check(ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "e"), &r) && (r.kind == EXT_MAT_KEEP) && (r.depthWrite == -1), "empty rule keeps", &n,
			              &failures);
			ExtJson_Check(!ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "f"), &r), "unknown blend word refused", &n, &failures);
			ExtJson_Check(!ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "g"), &r), "depth_write must be a boolean", &n, &failures);
			ExtJson_Check(!ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "h"), &r), "cutoff outside 0..1 refused", &n, &failures);
			ExtJson_Check(!ExtTrack_MaterialRule(&rd, ExtJson_Get(&rd, 0, "i"), &r), "a rule that is no object refused", &n, &failures);
			ExtJson_Check(!ExtTrack_MaterialRule(&rd, -1, &r) && (r.kind == EXT_MAT_KEEP), "no rule", &n, &failures);
			ExtJson_Free(&rd);
		}
	}
	// The rules applied to a model opened by the static loader - the data form of the converter: glTF
	// materials named, the rules keyed by those names (the A4 run of 2026-10-20 crashed here on the sky file).
	{
		static const char glbJson[] =
		    "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"scene\":0,\"nodes\":[{\"mesh\":0}],"
		    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],\"buffers\":[{\"byteLength\":36}],"
		    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
		    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
		    "\"materials\":[{\"name\":\"glow\"},{\"name\":\"glass\",\"alphaMode\":\"BLEND\"},{\"name\":\"rock\"},{}]}";
		static const char rulesJson[] = "{\"render\": {\"materials\": {\"glow\": {\"blend\": \"add\", \"depth_write\": false},"
		                                " \"glass\": {\"depth_write\": true}, \"missing\": {\"blend\": \"opaque\"}, \"bad\": {\"blend\": \"x\"}}},"
		                                " \"sky\": {\"materials\": [1, 2]}}";
		const float tri[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
		u32 size = 0;
		u8 *glb = EngStaticLoadTest_Glb(glbJson, (const u8 *)tri, sizeof(tri), &size);
		EngStaticLoadMem mem;
		EngineStaticSource src;
		EngineStaticModel model;
		ExtJsonDoc rd;
		int pass;

		memset(&src, 0, sizeof(src));
		memset(&model, 0, sizeof(model));
		mem.data = glb;
		mem.size = size;
		src.read = EngStaticLoadTest_MemRead;
		src.user = &mem;
		src.size = size;
		ExtJson_Check((glb != NULL) && EngineStatic_Open(&src, &model, why, (int)sizeof(why)) && (model.materialCount == 5), "rule model opens", &n, &failures);
		ExtJson_Check(ExtJson_Parse(&rd, rulesJson, strlen(rulesJson), why, (int)sizeof(why)) == 1, "rule document parses", &n, &failures);
		for (pass = 0; (pass < 2) && (model.materials != NULL) && (rd.nodes != NULL); pass++)
		{
			ExtDrawFile f;
			const int section = ExtJson_Get(&rd, 0, pass == 0 ? "render" : "sky");

			memset(&f, 0, sizeof(f));
			f.materialCount = model.materialCount;
			f.materials = (EngineStaticMaterial *)malloc((size_t)model.materialCount * sizeof(EngineStaticMaterial));
			if (f.materials == NULL)
			{
				break;
			}
			memcpy(f.materials, model.materials, (size_t)model.materialCount * sizeof(EngineStaticMaterial));
			ExtJson_Check(ExtDraw_ApplyMaterialRules(&f, &model, &rd, ExtJson_Get(&rd, section, "materials"), "selftest") == 1, "rules applied", &n,
			              &failures);
			if (pass == 0)
			{
				ExtJson_Check((f.rulesApplied == 2) && (f.rulesAdd == 1) && (f.rulesUnknown == 1) && (f.rulesRefused == 1),
				              "rules by name: 2 applied, 1 add, 1 unknown, 1 refused", &n, &failures);
				ExtJson_Check((f.ruleKind[0] == EXT_MAT_ADD) && (f.materials[0].alphaMode == ENGINE_STATIC_ALPHA_BLEND) && (f.ruleDepthWrite[0] == 0),
				              "the named material becomes add without depth write", &n, &failures);
				ExtJson_Check((f.ruleKind[1] == EXT_MAT_KEEP) && (f.materials[1].alphaMode == ENGINE_STATIC_ALPHA_BLEND) && (f.ruleDepthWrite[1] == 1),
				              "a rule with depth_write only keeps the glTF mode", &n, &failures);
				ExtJson_Check((f.ruleKind[2] == EXT_MAT_KEEP) && (f.ruleKind[3] == EXT_MAT_KEEP) && (f.ruleKind[4] == EXT_MAT_KEEP) &&
				                  (f.materials[2].alphaMode == model.materials[2].alphaMode),
				              "materials without a rule (one without a name, the default) unchanged", &n, &failures);
			}
			else
			{
				ExtJson_Check((f.rulesApplied == 0) && (f.rulesRefused == 1), "\"materials\" that is no object: no rule, no crash", &n, &failures);
			}
			free(f.ruleKind);
			free(f.ruleDepthWrite);
			free(f.materials);
		}
		{
			ExtDrawFile f;

			memset(&f, 0, sizeof(f));
			f.materialCount = 1;
			f.materials = (EngineStaticMaterial *)calloc(1, sizeof(EngineStaticMaterial));
			ExtJson_Check((f.materials != NULL) && ExtDraw_ApplyMaterialRules(&f, NULL, &rd, 0, "selftest") && (f.rulesApplied == 0),
			              "rules without a model: none applied", &n, &failures);
			free(f.ruleKind);
			free(f.ruleDepthWrite);
			free(f.materials);
		}
		if (rd.nodes != NULL)
		{
			ExtJson_Free(&rd);
		}
		EngineStatic_Close(&model);
		free(glb);
	}
	// The instance tint's darkness for a floor brightness.
	ExtJson_Check((ExtTrack_TintFor(255.0f) == 0.0f) && (ExtTrack_TintFor(170.0f) == 0.0f) && (fabsf(ExtTrack_TintFor(85.0f) - 0.5f) < 1e-6f) &&
	                  (fabsf(ExtTrack_TintFor(0.0f) - 0.75f) < 1e-6f),
	              "instance tint: bright floor none, half light 0.5, dark at most 0.75", &n, &failures);
	ExtJson_Check(ExtDraw_TintInstance(NULL) == 0, "instance tint off without the field", &n, &failures);
	// The HUD minimap's table, raster and texel packing (platform/native_extmap.c).
	ExtMap_Selftest(&n, &failures);
	// The zone camera's roll against the game's camera matrix (platform/native_extcam.c).
	ExtCam_Selftest(&n, &failures);
	ExtJson_Check(ExtTrack_AudioPaused() == 0, "audio pause is 0 without the switch", &n, &failures);

	if (failures == 0)
	{
		printf("ext selftest json passed: %d checks, 0 failures\n", n);
		return 0;
	}
	printf("ext selftest json FAILED: %d checks, %d failures\n", n, failures);
	return 1;
}
