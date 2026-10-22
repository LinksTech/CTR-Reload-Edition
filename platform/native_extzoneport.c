#include <platform/native_extzoneport.h>
#include <platform/native_extzonephys.h>
#include <platform/native_extcoll.h>
#include <platform/native_extphys.h>
#include <platform/native_extzonecontact.h>
#include <platform/native_extzonephys_s2.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// THE RELOAD SIDE OF THE ZONE PHYSICS AND CHASE CAMERA PORT (see
// include/platform/native_extzoneport.h). Unity build: included by main.c
// after the JSON reader (platform/engine_exttrack.c declares ExtJsonDoc in
// include/platform/engine.h) and before platform/native_extphys.c, which calls
// the reader below on the key "physics.source".

global_variable ExtZpSource s_extZpSource;
const char *g_cfg_extStepLog = NULL;

const ExtZpSource *ExtZp_Source(void)
{
	return &s_extZpSource;
}

int ExtZp_PortWanted(void)
{
	return s_extZpSource.present && s_extZpSource.port;
}

void ExtZp_ClearSource(void)
{
	memset(&s_extZpSource, 0, sizeof(s_extZpSource));
	s_extZpSource.scale = 64.0;
	ExtZp_ClearWorld(); // the start world (platform/native_extzoneworld.c)
}

const ExtZpSourceCsvRow *ExtZp_CsvRow(const char *table, const char *name)
{
	for (int t = 0; t < s_extZpSource.csvCount; t++)
	{
		if (strcmp(s_extZpSource.csv[t].name, table) != 0)
		{
			continue;
		}
		for (int r = 0; r < s_extZpSource.csv[t].rowCount; r++)
		{
			if (strcmp(s_extZpSource.csv[t].rows[r].name, name) == 0)
			{
				return &s_extZpSource.csv[t].rows[r];
			}
		}
	}
	return NULL;
}

// ------------------------------------------------------------------ reader

// "%08x" -> the pattern; 0 = not 8 hex digits.
internal int ExtZp_ReadBits(const ExtJsonDoc *doc, int node, Ps2F *out)
{
	const char *s = ExtJson_String(doc, node, NULL);
	uint32_t v = 0;

	if ((s == NULL) || (strlen(s) != 8))
	{
		return 0;
	}
	for (int i = 0; i < 8; i++)
	{
		const char c = s[i];
		const int d = ((c >= '0') && (c <= '9')) ? (c - '0') : (((c >= 'a') && (c <= 'f')) ? (c - 'a' + 10) : (((c >= 'A') && (c <= 'F')) ? (c - 'A' + 10) : -1));
		if (d < 0)
		{
			return 0;
		}
		v = (v << 4) | (uint32_t)d;
	}
	*out = v;
	return 1;
}

// An array of exactly n bit strings.
internal int ExtZp_ReadBitsArray(const ExtJsonDoc *doc, int node, Ps2F *out, int n)
{
	if ((ExtJson_Type(doc, node) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, node) != n))
	{
		return 0;
	}
	for (int i = 0; i < n; i++)
	{
		if (!ExtZp_ReadBits(doc, ExtJson_At(doc, node, i), &out[i]))
		{
			return 0;
		}
	}
	return 1;
}

internal void ExtZp_ReadSplines(const ExtJsonDoc *doc, int node, ExtZpSourceSpline *list, int *count)
{
	ExtZpSource *s = &s_extZpSource;

	*count = 0;
	if (ExtJson_Type(doc, node) != EXT_JSON_ARRAY)
	{
		return;
	}
	for (int e = ExtJson_First(doc, node); e >= 0; e = ExtJson_Next(doc, e))
	{
		const int nodes = ExtJson_Get(doc, e, "nodes");
		ExtZpSourceSpline sp;
		int ok = (*count < EXT_ZP_SPLINES_MAX) && (ExtJson_Type(doc, e) == EXT_JSON_OBJECT) && (ExtJson_Type(doc, nodes) == EXT_JSON_ARRAY);

		memset(&sp, 0, sizeof(sp));
		if (ok)
		{
			sp.type = (int)ExtJson_Number(doc, ExtJson_Get(doc, e, "type"), -1.0);
			sp.hkIndex = (int)ExtJson_Number(doc, ExtJson_Get(doc, e, "hkindex"), -1.0);
			sp.firstNode = s->nodeCount;
			for (int k = ExtJson_First(doc, nodes); ok && (k >= 0); k = ExtJson_Next(doc, k))
			{
				ok = (s->nodeCount < EXT_ZP_NODES_MAX) && ExtZp_ReadBitsArray(doc, k, s->nodes[s->nodeCount], 6);
				if (ok)
				{
					s->nodeCount++;
					sp.nodeCount++;
				}
			}
		}
		if (!ok)
		{
			// the list keeps its places: a bad entry becomes an empty one
			s->badValues++;
			s->nodeCount = sp.firstNode;
			memset(&sp, 0, sizeof(sp));
			sp.type = -1;
			sp.hkIndex = -1;
			if (*count >= EXT_ZP_SPLINES_MAX)
			{
				continue;
			}
		}
		list[(*count)++] = sp;
	}
}

internal void ExtZp_ReadCsv(const ExtJsonDoc *doc, int node)
{
	ExtZpSource *s = &s_extZpSource;

	if (ExtJson_Type(doc, node) != EXT_JSON_OBJECT)
	{
		return;
	}
	for (int t = ExtJson_First(doc, node); t >= 0; t = ExtJson_Next(doc, t))
	{
		ExtZpSourceCsv *c;

		if ((s->csvCount >= EXT_ZP_CSV_TABLES) || (ExtJson_Type(doc, t) != EXT_JSON_ARRAY) || (doc->nodes[t].key == NULL) ||
		    (strlen(doc->nodes[t].key) >= sizeof(c->name)))
		{
			s->badValues++;
			continue;
		}
		c = &s->csv[s->csvCount++];
		memset(c, 0, sizeof(*c));
		snprintf(c->name, sizeof(c->name), "%s", doc->nodes[t].key);
		for (int r = ExtJson_First(doc, t); r >= 0; r = ExtJson_Next(doc, r))
		{
			const char *name = ExtJson_String(doc, ExtJson_Get(doc, r, "name"), NULL);
			const int text = ExtJson_Get(doc, r, "text");
			const int bits = ExtJson_Get(doc, r, "bits");
			const int n = (ExtJson_Type(doc, bits) == EXT_JSON_ARRAY) ? ExtJson_Count(doc, bits) : -1;
			ExtZpSourceCsvRow *row;

			if ((c->rowCount >= EXT_ZP_CSV_ROWS_MAX) || (name == NULL) || (strlen(name) >= EXT_ZP_NAME_MAX) || (n < 1) || (n > 4) ||
			    (ExtJson_Type(doc, text) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, text) != n))
			{
				s->badValues++;
				continue;
			}
			row = &c->rows[c->rowCount];
			memset(row, 0, sizeof(*row));
			snprintf(row->name, sizeof(row->name), "%s", name);
			row->count = n;
			if (!ExtZp_ReadBitsArray(doc, bits, row->bits, n))
			{
				s->badValues++;
				continue;
			}
			for (int i = 0; i < n; i++)
			{
				snprintf(row->text[i], sizeof(row->text[i]), "%s", ExtJson_String(doc, ExtJson_At(doc, text, i), ""));
			}
			c->rowCount++;
		}
	}
}

// physics.source (from ExtPhys_Read); a section that is no object leaves the
// source absent.
void ExtZp_ReadSource(const ExtJsonDoc *doc, int node)
{
	ExtZpSource *s = &s_extZpSource;
	const int frame = ExtJson_Get(doc, node, "frame");
	const int phantoms = ExtJson_Get(doc, node, "phantoms");

	ExtZp_ClearSource();
	if (ExtJson_Type(doc, node) != EXT_JSON_OBJECT)
	{
		return;
	}
	s->port = ExtJson_Number(doc, ExtJson_Get(doc, node, "port"), 0.0) == 1.0;
	if (ExtJson_Type(doc, frame) == EXT_JSON_OBJECT)
	{
		const int origin = ExtJson_Get(doc, frame, "origin_cnk");
		if (ExtJson_Numbers(doc, origin, s->origin, 3) != 3)
		{
			s->badValues++;
		}
		s->scale = ExtJson_Number(doc, ExtJson_Get(doc, frame, "scale_per_m"), 64.0);
		s->rotDeg = ExtJson_Number(doc, ExtJson_Get(doc, frame, "rot_deg"), 0.0);
	}
	else
	{
		s->badValues++;
	}
	if (ExtJson_Type(doc, phantoms) == EXT_JSON_ARRAY)
	{
		for (int e = ExtJson_First(doc, phantoms); e >= 0; e = ExtJson_Next(doc, e))
		{
			ExtZpSourcePhantom ph;
			const char *name = ExtJson_String(doc, ExtJson_Get(doc, e, "name"), NULL);
			const char *kind = ExtJson_String(doc, ExtJson_Get(doc, e, "kind"), "");
			int ok;

			memset(&ph, 0, sizeof(ph));
			ph.kind = (strcmp(kind, "static_obb") == 0) ? EXT_ZP_KIND_STATIC_OBB
			          : (strcmp(kind, "dynamic_obb") == 0) ? EXT_ZP_KIND_DYNAMIC_OBB
			          : (strcmp(kind, "aabb") == 0)        ? EXT_ZP_KIND_AABB
			                                               : -1;
			ph.index = (int)ExtJson_Number(doc, ExtJson_Get(doc, e, "index"), -1.0);
			ok = (s->phantomCount < EXT_ZP_PHANTOMS_MAX) && (name != NULL) && (strlen(name) < EXT_ZP_NAME_MAX) && (ph.kind >= 0) && (ph.index >= 0) &&
			     ExtZp_ReadBitsArray(doc, ExtJson_Get(doc, e, "centre"), ph.centre, 3) && ExtZp_ReadBitsArray(doc, ExtJson_Get(doc, e, "size"), ph.size, 3) &&
			     ((ph.kind == EXT_ZP_KIND_AABB) || ExtZp_ReadBitsArray(doc, ExtJson_Get(doc, e, "rot"), ph.rot, 4));
			if (!ok)
			{
				s->badValues++;
				continue;
			}
			snprintf(ph.name, sizeof(ph.name), "%s", name);
			s->phantoms[s->phantomCount++] = ph;
		}
	}
	ExtZp_ReadSplines(doc, ExtJson_Get(doc, node, "maggrav_splines"), s->maggrav, &s->maggravCount);
	ExtZp_ReadSplines(doc, ExtJson_Get(doc, node, "magdisp_splines"), s->magdisp, &s->magdispCount);
	ExtZp_ReadCsv(doc, ExtJson_Get(doc, node, "csv"));
	ExtZp_ReadWorld(doc, ExtJson_Get(doc, node, "world"));
	s->present = 1;
	Platform_Log("[CTR ExtZp] physics.source: port %d, %d phantom(s), %d + %d spline(s), %d csv table(s), start world %d page(s) %d word(s), %d bad\n",
	             s->port, s->phantomCount, s->maggravCount, s->magdispCount, s->csvCount, ExtZp_WorldPages(), ExtZp_WorldWords(), s->badValues + ExtZp_WorldBad());
}

// ------------------------------------------------------------------ frame mapping

void ExtZp_PointToSource(const double r[3], double out[3])
{
	const ExtZpSource *s = &s_extZpSource;
	const double a2 = r[0] / s->scale, b2 = r[2] / s->scale;
	double a = a2, b = b2;

	if (s->rotDeg != 0.0)
	{
		const double c = cos(s->rotDeg * 3.14159265358979323846 / 180.0), n = sin(s->rotDeg * 3.14159265358979323846 / 180.0);
		a = a2 * c + b2 * n;
		b = -a2 * n + b2 * c;
	}
	out[0] = a + s->origin[0];
	out[1] = -b + s->origin[1];
	out[2] = r[1] / s->scale + s->origin[2];
}

void ExtZp_PointFromSource(const double p[3], double out[3])
{
	const ExtZpSource *s = &s_extZpSource;
	double a = p[0] - s->origin[0], b = -(p[1] - s->origin[1]);

	if (s->rotDeg != 0.0)
	{
		const double c = cos(s->rotDeg * 3.14159265358979323846 / 180.0), n = sin(s->rotDeg * 3.14159265358979323846 / 180.0);
		const double a2 = a * c - b * n, b2 = a * n + b * c;
		a = a2;
		b = b2;
	}
	out[0] = s->scale * a;
	out[1] = s->scale * (p[2] - s->origin[2]);
	out[2] = s->scale * b;
}

void ExtZp_DirToSource(const double r[3], double out[3])
{
	const ExtZpSource *s = &s_extZpSource;
	double a = r[0], b = r[2];

	if (s->rotDeg != 0.0)
	{
		const double c = cos(s->rotDeg * 3.14159265358979323846 / 180.0), n = sin(s->rotDeg * 3.14159265358979323846 / 180.0);
		const double a2 = r[0] * c + r[2] * n, b2 = -r[0] * n + r[2] * c;
		a = a2;
		b = b2;
	}
	out[0] = a;
	out[1] = -b;
	out[2] = r[1];
}

void ExtZp_DirFromSource(const double p[3], double out[3])
{
	const ExtZpSource *s = &s_extZpSource;
	double a = p[0], b = -p[1];

	if (s->rotDeg != 0.0)
	{
		const double c = cos(s->rotDeg * 3.14159265358979323846 / 180.0), n = sin(s->rotDeg * 3.14159265358979323846 / 180.0);
		const double a2 = a * c - b * n, b2 = a * n + b * c;
		a = a2;
		b = b2;
	}
	out[0] = a;
	out[1] = p[2];
	out[2] = b;
}

Ps2F ExtZp_ToPs2(double v)
{
	return Ps2F_FromHostFloat((float)v);
}

// ------------------------------------------------------------------ step log

global_variable FILE *s_extZpLog;
global_variable int s_extZpLogFailed;
global_variable unsigned char *s_extZpRec;
global_variable uint32_t s_extZpRecLen, s_extZpRecCap;
global_variable uint32_t s_extZpRecords;

internal void ExtZp_RecPut(const void *bytes, uint32_t n)
{
	if (s_extZpRecLen + n > s_extZpRecCap)
	{
		uint32_t cap = s_extZpRecCap ? s_extZpRecCap : 4096;
		unsigned char *grown;
		while (cap < s_extZpRecLen + n)
		{
			cap *= 2;
		}
		grown = (unsigned char *)realloc(s_extZpRec, cap);
		if (grown == NULL)
		{
			s_extZpLogFailed = 1;
			return;
		}
		s_extZpRec = grown;
		s_extZpRecCap = cap;
	}
	memcpy(s_extZpRec + s_extZpRecLen, bytes, n);
	s_extZpRecLen += n;
}

internal void ExtZp_LogAtExit(void)
{
	if (s_extZpLog != NULL)
	{
		fclose(s_extZpLog);
		s_extZpLog = NULL;
		Platform_Log("[CTR ExtZp] step log at exit: %u record(s) in %s\n", (unsigned)s_extZpRecords, g_cfg_extStepLog);
	}
}

int ExtZp_LogOn(void)
{
	if ((g_cfg_extStepLog == NULL) || s_extZpLogFailed)
	{
		return 0;
	}
	if (s_extZpLog == NULL)
	{
		s_extZpLog = fopen(g_cfg_extStepLog, "wb");
		if (s_extZpLog == NULL)
		{
			s_extZpLogFailed = 1;
			Platform_Log("[CTR ExtZp] step log: cannot write %s - off\n", g_cfg_extStepLog);
			return 0;
		}
		fwrite("RLZSTEP1", 1, 8, s_extZpLog);
		atexit(ExtZp_LogAtExit);
	}
	return 1;
}

// A record: the head words (record tag and length are added here), then blobs.
void ExtZp_LogBegin(uint32_t recordTag, const uint32_t *head, int headWords)
{
	const uint32_t zero = 0;

	s_extZpRecLen = 0;
	ExtZp_RecPut(&recordTag, 4);
	ExtZp_RecPut(&zero, 4); // length, set by ExtZp_LogEnd
	ExtZp_RecPut(head, (uint32_t)headWords * 4u);
}

void ExtZp_LogBlob(uint32_t tag, const void *bytes, uint32_t length)
{
	static const unsigned char pad[4] = {0, 0, 0, 0};

	ExtZp_RecPut(&tag, 4);
	ExtZp_RecPut(&length, 4);
	ExtZp_RecPut(bytes, length);
	ExtZp_RecPut(pad, (4u - (length & 3u)) & 3u);
}

void ExtZp_LogEnd(void)
{
	uint32_t body;

	if ((s_extZpRecLen < 8) || s_extZpLogFailed)
	{
		return;
	}
	body = s_extZpRecLen - 8;
	memcpy(s_extZpRec + 4, &body, 4);
	if ((s_extZpLog != NULL) && (fwrite(s_extZpRec, 1, s_extZpRecLen, s_extZpLog) == s_extZpRecLen))
	{
		s_extZpRecords++;
	}
	else
	{
		s_extZpLogFailed = 1;
	}
}

// ------------------------------------------------------------------ Reload's answers to the port's queries

// Writes of an answer: done in the port's memory and kept for the step log
// (STEPLOG_FORMAT: 'QRY ' blob, nWrites x {addr, len, bytes}).
global_variable int s_extZpCoupleContacts; // the coupling configured the contacts for its world
internal void ExtZp_AnswerWrite(ExtZpCtx *c, uint32_t addr, const void *bytes, uint32_t len)
{
	ExtZpMem_Write(c->m, addr, bytes, len); // kept by the memory's write journal for the step log
}

// The source's ray request (0x338700: a0 = the request object RC, a1 = start,
// a2 = end, both vec4 in metres): Reload's exact line query on the external
// collision; a hit writes RC+0x30 point (vec4), RC+0x40 normal (vec4), RC+0x50
// the fraction along the ray, RC+0x54 the surface type (24 = a zone record,
// the source's maglev surface; 1 = other ground; 27 = wall). v0 = 1 hit.
internal void ExtZp_AnswerRay(ExtZpCtx *c, const ExtZpQueryArgs *in, ExtZpQueryResult *out)
{
	double a[3], b[3], ra[3], rb[3], dir[3], len;
	ExtCollLineHit hit;

	for (int k = 0; k < 3; k++)
	{
		a[k] = Ps2F_ToDouble(ZmRF(c->m, in->a[1] + 4u * (uint32_t)k));
		b[k] = Ps2F_ToDouble(ZmRF(c->m, in->a[2] + 4u * (uint32_t)k));
	}
	ExtZp_PointFromSource(a, ra);
	ExtZp_PointFromSource(b, rb);
	dir[0] = rb[0] - ra[0];
	dir[1] = rb[1] - ra[1];
	dir[2] = rb[2] - ra[2];
	len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
	out->v0 = 0;
	if (len <= 0.0 || !ExtColl_LineQuery(ra, dir, 0.0, len, EXT_COLL_LINE_GROUND | EXT_COLL_LINE_WALL, 0, &hit))
	{
		return;
	}
	{
		double p[3], n[3];
		Ps2F rec[10];
		ExtZp_PointToSource(hit.pos, p);
		ExtZp_DirToSource(hit.normal, n);
		rec[0] = ExtZp_ToPs2(p[0]);
		rec[1] = ExtZp_ToPs2(p[1]);
		rec[2] = ExtZp_ToPs2(p[2]);
		rec[3] = PS2F_ONE;
		rec[4] = ExtZp_ToPs2(n[0]);
		rec[5] = ExtZp_ToPs2(n[1]);
		rec[6] = ExtZp_ToPs2(n[2]);
		rec[7] = 0;
		rec[8] = ExtZp_ToPs2(hit.t / len);
		rec[9] = (hit.zone != 0) ? 24u : ((hit.cls == EXT_COLL_LINE_WALL) ? 27u : 1u);
		ExtZp_AnswerWrite(c, in->a[0] + 0x30u, rec, sizeof(rec));
	}
	out->v0 = 1;
}

// The query callback of the port inside Reload (ExtZp_CtxInit with it). Every
// query goes to the step log when it is on.
global_variable u64 s_extZpQueryTicks[8]; // time in Reload's answers by kind (performance counter), the coupling's tick line
global_variable int s_extZpQueryDepth; // > 1: a query asked while Reload answers another (its writes are the outer one's)
void ExtZp_ReloadQuery(ExtZpCtx *c, int kind, const ExtZpQueryArgs *in, ExtZpQueryResult *out)
{
	const u64 queryStart = SDL_GetPerformanceCounter();
	const int logged = ExtZp_LogOn() && (s_extZpQueryDepth == 0);
	s_extZpQueryDepth++;
	memset(out, 0, sizeof(*out));
	if (logged)
	{
		// everything the answer writes (ray record, contact lists, the kart's contact events, the solver)
		ExtZpMem_JournalBegin(c->m);
	}
	if (kind == EXT_ZP_Q_RAY)
	{
		ExtZp_AnswerRay(c, in, out);
	}
	else if ((kind == EXT_ZP_Q_COLLIDE) && (in->func == 0x003B1960u) && s_extZpCoupleContacts)
	{
		// the subspace's contact solver (type 1): the port's own (parts s1-s3) on the detector's records
		ExtZp_Solver_3b1960(c, in->a[0]);
	}
	else if (s_extZpCoupleContacts)
	{
		// the collision detector, penetration handling, material and allocator of the source:
		// the zone port's contacts from Reload's geometry (platform/native_extzonecontact.c)
		(void)ExtZc_Answer(c, kind, in, out);
	}
	s_extZpQueryDepth--;
	if (logged)
	{
		uint32_t head[3 + 16 + 4];
		static uint32_t seq;
		uint32_t jBytes = 0, jCount = 0;
		const uint8_t *j = ExtZpMem_JournalEnd(c->m, &jBytes, &jCount);
		head[0] = seq++;
		head[1] = (uint32_t)kind;
		head[2] = in->func;
		memcpy(&head[3], in->a, 32);
		memcpy(&head[11], in->f, 32);
		head[19] = out->v0;
		head[20] = out->v1;
		head[21] = out->f0;
		head[22] = jCount;
		{
			// the blob: the head, then the writes {addr, len, bytes padded to 4} (STEPLOG_FORMAT 'QRY ')
			static unsigned char *blob;
			static uint32_t blobCap;
			const uint32_t need = (uint32_t)sizeof(head) + jBytes + 3u * jCount + 4u;
			uint32_t at = (uint32_t)sizeof(head), off = 0;
			if (need > blobCap)
			{
				unsigned char *grown = (unsigned char *)realloc(blob, need);
				if (grown != NULL)
				{
					blob = grown;
					blobCap = need;
				}
			}
			if (need <= blobCap)
			{
				memcpy(blob, head, sizeof(head));
				for (uint32_t e = 0; e < jCount; e++)
				{
					uint32_t len;
					memcpy(&len, j + off + 4u, 4);
					memcpy(blob + at, j + off, 8u + len);
					memset(blob + at + 8u + len, 0, ((len + 3u) & ~3u) - len);
					at += 8u + ((len + 3u) & ~3u);
					off += 8u + len;
				}
				ExtZp_LogBlob(EXT_ZP_TAG('Q', 'R', 'Y', ' '), blob, at);
			}
		}
	}
	s_extZpQueryTicks[(kind > 0 && kind < 7) ? kind : 7] += SDL_GetPerformanceCounter() - queryStart;
}

// ------------------------------------------------------------------ selftest

internal void ExtZp_Check(int ok, const char *what, int *checks, int *failures, void (*log)(const char *line))
{
	(*checks)++;
	if (!ok)
	{
		char line[192];
		(*failures)++;
		snprintf(line, sizeof(line), "zone port FAIL %s", what);
		if (log)
		{
			log(line);
		}
	}
}

void ExtZp_Selftest(int *checks, int *failures, void (*log)(const char *line))
{
	static const char text[] =
	    "{\"source\": {\"version\": 1, \"port\": 1, \"frame\": {\"origin_cnk\": [320.0, 25.6, 0.0], \"scale_per_m\": 64, \"rot_deg\": 0},"
	    " \"phantoms\": [{\"name\": \"on1\", \"kind\": \"aabb\", \"index\": 3, \"centre\": [\"3f800000\", \"40000000\", \"40400000\"],"
	    " \"size\": [\"3f800000\", \"3f800000\", \"3f800000\"]},"
	    " {\"name\": \"bad\", \"kind\": \"static_obb\", \"index\": 0, \"centre\": [\"3f80000\", \"0\", \"0\"], \"size\": [], \"rot\": []}],"
	    " \"maggrav_splines\": [{\"type\": 2, \"hkindex\": 7, \"nodes\": [[\"00000000\", \"00000000\", \"00000000\", \"00000000\", \"00000000\", \"3f800000\"],"
	    " [\"41200000\", \"00000000\", \"00000000\", \"00000000\", \"00000000\", \"3f800000\"]]}, {\"type\": -1, \"hkindex\": -1, \"nodes\": []}],"
	    " \"magdisp_splines\": [{\"type\": 1, \"nodes\": [[\"x\"]]}],"
	    " \"csv\": {\"kpbase\": [{\"name\": \"m_LowSpeed\", \"text\": [\"12\"], \"bits\": [\"41400000\"]},"
	    " {\"name\": \"m_Vec\", \"text\": [\"0\", \"-3.05\", \"1.2\"], \"bits\": [\"00000000\", \"c0433333\", \"3f99999a\"]}]}}}";
	ExtJsonDoc doc;
	char why[128];
	const ExtZpSource *s = &s_extZpSource;

	memset(&doc, 0, sizeof(doc));
	if (!ExtJson_Parse(&doc, text, strlen(text), why, (int)sizeof(why)))
	{
		ExtZp_Check(0, "made-up source parses", checks, failures, log);
		return;
	}
	ExtZp_ReadSource(&doc, ExtJson_Get(&doc, 0, "source"));
	ExtJson_Free(&doc);
	ExtZp_Check(s->present && s->port && ExtZp_PortWanted(), "source present, port asked", checks, failures, log);
	ExtZp_Check((s->phantomCount == 1) && (strcmp(s->phantoms[0].name, "on1") == 0) && (s->phantoms[0].kind == EXT_ZP_KIND_AABB) &&
	                (s->phantoms[0].index == 3) && (s->phantoms[0].centre[2] == 0x40400000u),
	            "phantom read, the bad one left out", checks, failures, log);
	ExtZp_Check((s->maggravCount == 2) && (s->maggrav[0].type == 2) && (s->maggrav[0].hkIndex == 7) && (s->maggrav[0].nodeCount == 2) &&
	                (s->nodes[s->maggrav[0].firstNode + 1][0] == 0x41200000u) && (s->maggrav[1].type == -1) && (s->maggrav[1].nodeCount == 0),
	            "maggrav splines with nodes and an empty entry", checks, failures, log);
	ExtZp_Check((s->magdispCount == 1) && (s->magdisp[0].type == -1) && (s->nodeCount == 2), "a bad spline keeps its place as empty", checks, failures,
	            log);
	{
		const ExtZpSourceCsvRow *r = ExtZp_CsvRow("kpbase", "m_Vec");
		ExtZp_Check((r != NULL) && (r->count == 3) && (r->bits[1] == 0xC0433333u) && (strcmp(r->text[2], "1.2") == 0) &&
		                (ExtZp_CsvRow("kpbase", "m_LowSpeed")->bits[0] == 0x41400000u) && (ExtZp_CsvRow("kabase", "m_Vec") == NULL),
		            "csv rows by table and name", checks, failures, log);
	}
	ExtZp_Check(s->badValues == 2, "two bad entries counted", checks, failures, log);
	{
		// a made-up frame: the source point (320 + 1, 25.6 - 2, 0 + 3) is Reload (64, 192, 128)
		const double p[3] = {321.0, 23.6, 3.0};
		double r[3], back[3], d[3], dback[3];
		const double up[3] = {0.0, 0.0, 1.0};
		ExtZp_PointFromSource(p, r);
		ExtZp_PointToSource(r, back);
		ExtZp_Check((fabs(r[0] - 64.0) < 1e-9) && (fabs(r[1] - 192.0) < 1e-9) && (fabs(r[2] - 128.0) < 1e-9) && (fabs(back[1] - 23.6) < 1e-9),
		            "point mapping and its inverse", checks, failures, log);
		ExtZp_DirFromSource(up, d);
		ExtZp_DirToSource(d, dback);
		ExtZp_Check((d[0] == 0.0) && (d[1] == 1.0) && (d[2] == 0.0) && (dback[2] == 1.0), "source up is Reload up", checks, failures, log);
		ExtZp_Check(ExtZp_ToPs2(0.1) == 0x3DCCCCCDu, "nearest float at the border", checks, failures, log);
	}
	{
		// record layout: tag, length, head, blob tag, blob length, data, padding
		const uint32_t head[3] = {5u, 0u, EXT_ZP_LOG_STEP_ZONE};
		const unsigned char three[3] = {1, 2, 3};
		uint32_t w[8];
		ExtZp_LogBegin(EXT_ZP_TAG('S', 'T', 'E', 'P'), head, 3);
		ExtZp_LogBlob(EXT_ZP_TAG('P', 'R', 'E', ' '), three, 3);
		memset(w, 0, sizeof(w));
		if (s_extZpRecLen >= sizeof(w))
		{
			const uint32_t body = s_extZpRecLen - 8;
			memcpy(s_extZpRec + 4, &body, 4);
			memcpy(w, s_extZpRec, sizeof(w));
		}
		ExtZp_Check((s_extZpRecLen == 32) && (w[0] == EXT_ZP_TAG('S', 'T', 'E', 'P')) && (w[1] == 24) && (w[2] == 5) && (w[4] == 1) &&
		                (w[5] == EXT_ZP_TAG('P', 'R', 'E', ' ')) && (w[6] == 3) && (w[7] == 0x00030201u),
		            "step log record layout", checks, failures, log);
		s_extZpRecLen = 0;
	}
	ExtZp_ClearSource();
}

// ------------------------------------------------------------------ THE COUPLING (--dev-ext-zone-port)
//
// On a track whose physics.source asks for the port and brings a start world,
// --dev-ext-zone-port lets the port compute seat 0's kart while Reload has it in
// an anti-gravity zone:
//   entry   the start world is loaded anew, the kart's body takes Reload's state
//           (position + 0.6 m along up, velocity, orientation; spin 0);
//   a tick  Reload's stages PHYS_LINEAR (input, weapons), PHYS_ANGULAR, AUDIO,
//           COLL_FIXED (ground search, lap and zone bookkeeping), TRANSLATE_MATRIX,
//           ANIMATE and PARTICLES run; APPLY_FORCES is replaced by the port's
//           world step (dt = 1/30 s, the original's own two substeps) and
//           COLL_MOVED, COLLIDE_DRIVERS and JUMP_FRICTION do not run; the
//           result is written back (position, velocity, the kart matrix, the
//           zone's up and the heading) and kept against COLL_FIXED;
//   exit    when Reload's zone logic has the kart out of the zone, Reload
//           continues from the last written state (no jump).
// The inputs go into the kart's input block (the source's latch reads it):
// steer (+1 = left in the source), gas, reverse, brake, button 1 (R1), button 2 (L1).

int g_cfg_extZonePort = 0;

enum
{
	EXT_ZC_G_VEHICLE = 0x006F3B10, // the source's global: -> object whose +0x28 is the player's vehicle
	EXT_ZPC_V_KART = 0x78,         // vehicle -> kart
	EXT_ZPC_K_SHELL = 0x2C,        // kart -> (-> entity)
	EXT_ZPC_K_OWNER = 0x38,        // kart -> its owner (input block at +0x5C)
	EXT_ZPC_OWNER_INPUT = 0x5C,
	EXT_ZPC_E_FORWARD = 0x10,
	EXT_ZPC_E_UP = 0x20,
	EXT_ZPC_E_RIGHT = 0x30,
	EXT_ZPC_E_VELOCITY = 0x40,
	EXT_ZPC_E_BODY = 0x94,
	EXT_ZPC_H_POS = 0x30,
	EXT_ZPC_H_QUAT_W = 0x40,
	EXT_ZPC_H_QUAT_XYZ = 0x50,
	EXT_ZPC_H_VEL = 0x60,
	EXT_ZPC_H_SPIN = 0x70,
	EXT_ZPC_K_STATE = 0x3E0,
	EXT_ZPC_K_GROUND = 0x402,
	EXT_ZPC_K_MAGLEV = 0x428,
	EXT_ZPC_K_SPEED = 0x42C,
	EXT_ZPC_K_SPLINE = 0x464,
	EXT_ZPC_STEP_FUNC = 0x00336B60,
};
#define EXT_ZPC_BODY_UP_E 38.4      // the body point lies 0.6 m (38.4 units) above posCurr along up (ZUORDNUNG 4)
#define EXT_ZPC_VEL_PER_MS 524.288  // Reload velocity units per m/s (ZUORDNUNG 2)

typedef struct
{
	int ready;        // switch on, world present, objects found
	int active;       // the port has seat 0 now
	ExtZpMem *m;
	ExtZpCtx c;
	uint32_t world, vehicle, kart, entity, body, input;
	Ps2F dt;
	uint32_t steps, entries;
	uint32_t enteredFrame;
	uint32_t airTicks;          // port ticks without ground contact in a row
	int blocked;                // the port gave up this stay: no entry until Reload left the zone or set the kart down
	s32 keepPos[3], keepVel[3]; // the written state, kept against COLL_FIXED
	double up[3], fwd[3];       // the written orientation (Reload axes, unit)
} ExtZpCouple;

global_variable ExtZpCouple s_extZpc;

const ExtZpMem *ExtZp_PortMemory(uint32_t *kart, uint32_t *body, uint32_t *entity, uint32_t *world)
{
	if (!s_extZpc.ready || !s_extZpc.active)
	{
		return NULL;
	}
	if (kart)
		*kart = s_extZpc.kart;
	if (body)
		*body = s_extZpc.body;
	if (entity)
		*entity = s_extZpc.entity;
	if (world)
		*world = s_extZpc.world;
	return s_extZpc.m;
}

int ExtZp_PortActive(void)
{
	return s_extZpc.ready && s_extZpc.active;
}

// The contacts (platform/native_extzonecontact.c) for the loaded world: the
// track's static shell (body B of the kart's records) and the record class are
// taken from the detector's record list in the start world (world -> +0x64 ->
// +0x08 -> +0x1C -> subspace -> +0x70 detector -> +0x14 -> list +0xF8): the
// first record of seat 0's body, else the first record; an arena of free port
// memory below the port's stack holds the records the detector writes.
#define EXT_ZPC_CONTACT_ARENA 0x01C00000u
internal void ExtZpc_ConfigureContacts(void)
{
	ExtZpMem *m = s_extZpc.m;
	const uint32_t env = ZmR32(m, ZmR32(m, s_extZpc.world + 0x64u) + 0x08u);
	const uint32_t sub = ZmR32(m, ZmR32(m, env + 0x1Cu));
	const uint32_t P = ZmR32(m, ZmR32(m, sub + EXT_ZC_SUB_DETECTOR) + EXT_ZC_DET_INNER);
	const uint32_t count = ZmR32(m, P + EXT_ZC_P_CONTACTS + EXT_ZC_VEC_COUNT);
	const uint32_t recs = ZmR32(m, P + EXT_ZC_P_CONTACTS + EXT_ZC_VEC_DATA);
	uint32_t pick = 0;
	ExtZcConfig cfg;
	static const uint8_t zero[256] = {0};

	s_extZpCoupleContacts = 0;
	if ((count == 0u) || (recs == 0u))
	{
		Platform_Log("[CTR ExtZp] contacts: the start world has no detector records - no contacts\n");
		return;
	}
	for (uint32_t i = 0; i < count; i++)
	{
		const uint32_t shellA = ZmR32(m, recs + EXT_ZC_REC_SIZE * i + EXT_ZC_REC_SHELL_A);
		if (ZmR32(m, shellA + 0x18u) == s_extZpc.body)
		{
			pick = i;
			break;
		}
	}
	for (uint32_t a = 0; a < (uint32_t)EXT_ZC_ARENA_BYTES; a += (uint32_t)sizeof(zero))
	{
		ExtZpMem_Write(m, EXT_ZPC_CONTACT_ARENA + a, zero, (uint32_t)sizeof(zero));
	}
	memset(&cfg, 0, sizeof(cfg));
	cfg.triangles = ExtZc_ReloadTriangles;
	cfg.user = NULL;
	cfg.trackShell = ZmR32(m, recs + EXT_ZC_REC_SIZE * pick + EXT_ZC_REC_SHELL_B);
	cfg.recordClass = ZmR32(m, recs + EXT_ZC_REC_SIZE * pick + EXT_ZC_REC_VTABLE);
	cfg.recordArena = EXT_ZPC_CONTACT_ARENA;
	cfg.radius = 0x3F051EB8u; // 0.52 m
	ExtZc_Configure(&cfg);
	s_extZpCoupleContacts = 1;
	if (!s_extZpc.ready)
	{
		Platform_Log("[CTR ExtZp] contacts: track shell 0x%08x record class 0x%08x arena 0x%08x (record %u of %u)\n", cfg.trackShell, cfg.recordClass,
		             cfg.recordArena, (unsigned)pick, (unsigned)count);
	}
}

internal int ExtZpc_LoadWorld(void)
{
	ExtZpMem *m = s_extZpc.m;
	uint32_t world = 0, owner;
	Ps2F dt = 0;

	ExtZpMem_Clear(m);
	if (!ExtZp_WorldLoad(m, &world, &dt))
	{
		return 0;
	}
	ExtZp_CtxInit(&s_extZpc.c, m, ExtZp_ReloadQuery, NULL);
	s_extZpc.world = world;
	s_extZpc.dt = dt;
	s_extZpc.vehicle = ZmR32(m, ZmR32(m, EXT_ZC_G_VEHICLE) + 0x28u);
	s_extZpc.kart = ZmR32(m, s_extZpc.vehicle + EXT_ZPC_V_KART);
	s_extZpc.entity = ZmR32(m, ZmR32(m, s_extZpc.kart + EXT_ZPC_K_SHELL));
	s_extZpc.body = ZmR32(m, s_extZpc.entity + EXT_ZPC_E_BODY);
	owner = ZmR32(m, s_extZpc.kart + EXT_ZPC_K_OWNER);
	s_extZpc.input = (owner != 0u) ? owner + EXT_ZPC_OWNER_INPUT : 0u;
	ExtZpc_ConfigureContacts();
	return (s_extZpc.world != 0u) && (s_extZpc.kart != 0u) && (s_extZpc.entity != 0u) && (s_extZpc.body != 0u) && (s_extZpc.input != 0u) &&
	       (ExtZpMem_Faults(m) == 0u);
}

// Once per level (ExtPhys_OnLevelLoaded, after physics.source was read).
void ExtZp_CoupleLevel(void)
{
	s_extZpc.ready = 0;
	s_extZpc.active = 0;
	s_extZpc.blocked = 0;
	if (!g_cfg_extZonePort)
	{
		return;
	}
	if (!ExtZp_PortWanted() || (ExtZp_WorldPages() == 0))
	{
		Platform_Log("[CTR ExtZp] --dev-ext-zone-port: the track has no port world (physics.source port %d, %d page(s)) - off\n", ExtZp_PortWanted(),
		             ExtZp_WorldPages());
		return;
	}
	if (s_extZpc.m == NULL)
	{
		s_extZpc.m = ExtZpMem_Create();
	}
	if ((s_extZpc.m == NULL) || !ExtZpc_LoadWorld())
	{
		Platform_Log("[CTR ExtZp] --dev-ext-zone-port: the start world lacks the kart objects (W 0x%08x V 0x%08x K 0x%08x E 0x%08x H 0x%08x input 0x%08x, %u fault(s)) - off\n",
		             s_extZpc.world, s_extZpc.vehicle, s_extZpc.kart, s_extZpc.entity, s_extZpc.body, s_extZpc.input,
		             (s_extZpc.m != NULL) ? (unsigned)ExtZpMem_Faults(s_extZpc.m) : 0u);
		return;
	}
	s_extZpc.ready = 1;
	s_extZpc.steps = 0;
	s_extZpc.entries = 0;
	Platform_Log("[CTR ExtZp] --dev-ext-zone-port: on - W 0x%08x K 0x%08x E 0x%08x H 0x%08x input 0x%08x dt 0x%08x\n", s_extZpc.world, s_extZpc.kart,
	             s_extZpc.entity, s_extZpc.body, s_extZpc.input, s_extZpc.dt);
	if (ExtZp_LogOn())
	{
		const uint32_t head[1] = {1};
		char note[160];
		snprintf(note, sizeof(note), "zone port coupling; K=0x%08x E=0x%08x H=0x%08x W=0x%08x", s_extZpc.kart, s_extZpc.entity, s_extZpc.body, s_extZpc.world);
		ExtZp_LogBegin(EXT_ZP_TAG('L', 'E', 'V', 'L'), head, 1);
		ExtZp_LogBlob(EXT_ZP_TAG('N', 'O', 'T', 'E'), note, (uint32_t)strlen(note));
		ExtZp_LogEnd();
	}
}

// ---- vectors (double, Reload or source axes)

internal void ExtZpc_Norm(double v[3])
{
	const double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 0.0)
	{
		v[0] /= l;
		v[1] /= l;
		v[2] /= l;
	}
}

internal void ExtZpc_Cross(const double a[3], const double b[3], double out[3])
{
	const double x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
	out[0] = x;
	out[1] = y;
	out[2] = z;
}

// The rotation with columns right (local x), forward (local y), up (local z) as a
// unit quaternion (w, x, y, z) - the source body's convention (entity rows
// +0x30/+0x10/+0x20 are the body rotation's columns, measured on two dumps).
internal void ExtZpc_QuatFromAxes(const double r[3], const double f[3], const double u[3], double q[4])
{
	const double m00 = r[0], m01 = f[0], m02 = u[0];
	const double m10 = r[1], m11 = f[1], m12 = u[1];
	const double m20 = r[2], m21 = f[2], m22 = u[2];
	const double tr = m00 + m11 + m22;
	double s;

	if (tr > 0.0)
	{
		s = sqrt(tr + 1.0) * 2.0;
		q[0] = 0.25 * s;
		q[1] = (m21 - m12) / s;
		q[2] = (m02 - m20) / s;
		q[3] = (m10 - m01) / s;
	}
	else if ((m00 > m11) && (m00 > m22))
	{
		s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
		q[0] = (m21 - m12) / s;
		q[1] = 0.25 * s;
		q[2] = (m01 + m10) / s;
		q[3] = (m02 + m20) / s;
	}
	else if (m11 > m22)
	{
		s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
		q[0] = (m02 - m20) / s;
		q[1] = (m01 + m10) / s;
		q[2] = 0.25 * s;
		q[3] = (m12 + m21) / s;
	}
	else
	{
		s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
		q[0] = (m10 - m01) / s;
		q[1] = (m02 + m20) / s;
		q[2] = (m12 + m21) / s;
		q[3] = 0.25 * s;
	}
}

internal void ExtZpc_AxesFromQuat(const double q[4], double r[3], double f[3], double u[3])
{
	const double w = q[0], x = q[1], y = q[2], z = q[3];
	r[0] = 1.0 - 2.0 * (y * y + z * z);
	r[1] = 2.0 * (x * y + w * z);
	r[2] = 2.0 * (x * z - w * y);
	f[0] = 2.0 * (x * y - w * z);
	f[1] = 1.0 - 2.0 * (x * x + z * z);
	f[2] = 2.0 * (y * z + w * x);
	u[0] = 2.0 * (x * z + w * y);
	u[1] = 2.0 * (y * z - w * x);
	u[2] = 1.0 - 2.0 * (x * x + y * y);
}

internal void ExtZpc_WriteVec(uint32_t a, const double v[3])
{
	for (int k = 0; k < 3; k++)
	{
		ZmWF(s_extZpc.m, a + 4u * (uint32_t)k, ExtZp_ToPs2(v[k]));
	}
}

internal void ExtZpc_ReadVec(uint32_t a, double v[3])
{
	for (int k = 0; k < 3; k++)
	{
		v[k] = Ps2F_ToDouble(ZmRF(s_extZpc.m, a + 4u * (uint32_t)k));
	}
}

// Reload's kart -> the body, the entity rows (entry).
internal void ExtZpc_Enter(const struct Driver *d)
{
	const MATRIX *mt = &d->matrixFacingDir;
	double upR[3], fwR[3], up[3], fw[3], ri[3], p[3], ps[3], v[3], vs[3], q[4];

	for (int k = 0; k < 3; k++)
	{
		upR[k] = mt->m[k][1] / 4096.0;
		fwR[k] = mt->m[k][2] / 4096.0;
	}
	ExtZpc_Norm(upR);
	{
		const double dd = fwR[0] * upR[0] + fwR[1] * upR[1] + fwR[2] * upR[2];
		for (int k = 0; k < 3; k++)
			fwR[k] -= dd * upR[k];
	}
	ExtZpc_Norm(fwR);
	ExtZp_DirToSource(upR, up);
	ExtZp_DirToSource(fwR, fw);
	ExtZpc_Cross(fw, up, ri); // right = forward x up (ZUORDNUNG 3)
	for (int k = 0; k < 3; k++)
	{
		p[k] = ((&d->posCurr.x)[k]) / 256.0 + EXT_ZPC_BODY_UP_E * upR[k];
		v[k] = ((&d->velocity.x)[k]) / EXT_ZPC_VEL_PER_MS;
	}
	ExtZp_PointToSource(p, ps);
	ExtZp_DirToSource(v, vs);
	ExtZpc_QuatFromAxes(ri, fw, up, q);

	ExtZpc_WriteVec(s_extZpc.body + EXT_ZPC_H_POS, ps);
	ExtZpc_WriteVec(s_extZpc.body + EXT_ZPC_H_VEL, vs);
	{
		const double zero[3] = {0.0, 0.0, 0.0};
		ExtZpc_WriteVec(s_extZpc.body + EXT_ZPC_H_SPIN, zero);
	}
	ZmWF(s_extZpc.m, s_extZpc.body + EXT_ZPC_H_QUAT_W, ExtZp_ToPs2(q[0]));
	ExtZpc_WriteVec(s_extZpc.body + EXT_ZPC_H_QUAT_XYZ, &q[1]);
	ExtZpc_WriteVec(s_extZpc.entity + EXT_ZPC_E_FORWARD, fw);
	ExtZpc_WriteVec(s_extZpc.entity + EXT_ZPC_E_UP, up);
	ExtZpc_WriteVec(s_extZpc.entity + EXT_ZPC_E_RIGHT, ri);
	ExtZpc_WriteVec(s_extZpc.entity + EXT_ZPC_E_VELOCITY, vs);
}

// The pad -> the kart's input block.
internal void ExtZpc_Inputs(const struct Driver *d)
{
	const struct GamepadBuffer *pad = &sdata->gGamepads->gamepad[d->driverID];
	const u32 held = ((sdata->gGT->gameMode1 & END_OF_RACE) == 0) ? pad->buttonsHeldCurrFrame : 0u;
	double steer = 0.0;
	const uint32_t in = s_extZpc.input;

	// +1 turns the source's kart counter-clockwise about its up (probe on slot 7) = left; measured in
	// the race (frames 122-132, the same key in every run): Reload's own physics and the port with
	// RIGHT = -1 turn the same way, with RIGHT = +1 the other way (yaw compared over both runs)
	if ((held & BTN_LEFT) != 0)
		steer = 1.0;
	else if ((held & BTN_RIGHT) != 0)
		steer = -1.0;
	else
	{
		const int x = (int)pad->stickLX - 0x80;
		if ((x > 8) || (x < -8))
		{
			steer = -(double)x / 127.0;
			steer = (steer > 1.0) ? 1.0 : ((steer < -1.0) ? -1.0 : steer);
		}
	}
	ZmWF(s_extZpc.m, in + 0x00u, ExtZp_ToPs2(steer));
	ZmWF(s_extZpc.m, in + 0x04u, ExtZp_ToPs2(steer));
	ZmWF(s_extZpc.m, in + 0x08u, ((held & BTN_CROSS) != 0) ? PS2F_ONE : 0u);
	ZmWF(s_extZpc.m, in + 0x0Cu, ((held & BTN_SQUARE) != 0) ? PS2F_ONE : 0u);
	ZmWF(s_extZpc.m, in + 0x10u, ((held & BTN_SQUARE) != 0) ? PS2F_ONE : 0u);
	ZmW8(s_extZpc.m, in + 0x14u, ((held & BTN_R1) != 0) ? 1u : 0u);
	ZmW8(s_extZpc.m, in + 0x15u, ((held & BTN_L1) != 0) ? 1u : 0u);
}

// The body -> Reload's kart (after every tick).
internal void ExtZpc_WriteBack(struct Driver *d)
{
	double ps[3], vs[3], q[4], ri[3], fw[3], up[3], upR[3], fwR[3], s0[3], p[3], v[3];

	ExtZpc_ReadVec(s_extZpc.body + EXT_ZPC_H_POS, ps);
	ExtZpc_ReadVec(s_extZpc.body + EXT_ZPC_H_VEL, vs);
	q[0] = Ps2F_ToDouble(ZmRF(s_extZpc.m, s_extZpc.body + EXT_ZPC_H_QUAT_W));
	ExtZpc_ReadVec(s_extZpc.body + EXT_ZPC_H_QUAT_XYZ, &q[1]);
	{
		const double l = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
		if (l > 0.0)
			for (int k = 0; k < 4; k++)
				q[k] /= l;
	}
	ExtZpc_AxesFromQuat(q, ri, fw, up);
	ExtZp_DirFromSource(up, upR);
	ExtZp_DirFromSource(fw, fwR);
	ExtZpc_Norm(upR);
	ExtZpc_Norm(fwR);
	ExtZp_PointFromSource(ps, p);
	ExtZp_DirFromSource(vs, v);
	for (int k = 0; k < 3; k++)
	{
		const double pk = (p[k] - EXT_ZPC_BODY_UP_E * upR[k]) * 256.0;
		const double vk = v[k] * EXT_ZPC_VEL_PER_MS;
		(&d->posCurr.x)[k] = (s32)floor(pk + 0.5);
		(&d->velocity.x)[k] = (s32)floor(vk + 0.5);
		s_extZpc.keepPos[k] = (&d->posCurr.x)[k];
		s_extZpc.keepVel[k] = (&d->velocity.x)[k];
		s_extZpc.up[k] = upR[k];
		s_extZpc.fwd[k] = fwR[k];
	}
	ExtZpc_Cross(upR, fwR, s0); // column 0 = up x forward (ZUORDNUNG 3)
	for (int k = 0; k < 3; k++)
	{
		d->matrixFacingDir.m[k][0] = (s16)floor(s0[k] * 4096.0 + 0.5);
		d->matrixFacingDir.m[k][1] = (s16)floor(upR[k] * 4096.0 + 0.5);
		d->matrixFacingDir.m[k][2] = (s16)floor(fwR[k] * 4096.0 + 0.5);
	}
	d->matrixMovingDir = d->matrixFacingDir;
	{
		const SVec3 upS = {(s16)floor(upR[0] * 4096.0 + 0.5), (s16)floor(upR[1] * 4096.0 + 0.5), (s16)floor(upR[2] * 4096.0 + 0.5)};
		int heading;
		ExtPhys_PortUp(d, &upS);
		if (ExtPhys_ZoneHeadingTo(d, (int)(fwR[0] * 4096.0), (int)(fwR[1] * 4096.0), (int)(fwR[2] * 4096.0), &heading))
		{
			d->rotCurr.y = (s16)heading;
			d->angle = (s16)heading;
		}
	}
	VehPhysCrash_ConvertVecToSpeed(d, &d->velocity);
}

// The step log of one tick: the record head, the objects, the arguments and the
// pages before the step (queries follow from ExtZp_ReloadQuery), then the pages
// after it. Within one stay in the zone only the pages that differ from the
// last logged image are written (head word 4 = 1; the reader keeps the image).
typedef struct
{
	uint32_t addr;
	uint8_t bytes[4096];
} ExtZpcPage;
global_variable ExtZpcPage *s_extZpcImage;
global_variable int s_extZpcImageCount, s_extZpcImageCap;
global_variable int s_extZpcDelta; // 1 = an image of the previous step is kept

internal void ExtZpc_LogPage(uint32_t pageAddr, uint8_t *bytes, void *user)
{
	static uint8_t blob[4 + 4096];
	ExtZpcPage *img = NULL;

	for (int i = 0; i < s_extZpcImageCount; i++)
	{
		if (s_extZpcImage[i].addr == pageAddr)
		{
			img = &s_extZpcImage[i];
			break;
		}
	}
	if ((img != NULL) && s_extZpcDelta && (memcmp(img->bytes, bytes, 4096) == 0))
	{
		return;
	}
	if (img == NULL)
	{
		if (s_extZpcImageCount == s_extZpcImageCap)
		{
			const int cap = s_extZpcImageCap ? 2 * s_extZpcImageCap : 256;
			ExtZpcPage *grown = (ExtZpcPage *)realloc(s_extZpcImage, (size_t)cap * sizeof(ExtZpcPage));
			if (grown != NULL)
			{
				s_extZpcImage = grown;
				s_extZpcImageCap = cap;
			}
		}
		if (s_extZpcImageCount < s_extZpcImageCap)
		{
			img = &s_extZpcImage[s_extZpcImageCount++];
			img->addr = pageAddr;
		}
	}
	if (img != NULL)
	{
		memcpy(img->bytes, bytes, 4096);
	}
	memcpy(blob, &pageAddr, 4);
	memcpy(blob + 4, bytes, 4096);
	ExtZp_LogBlob(*(const uint32_t *)user, blob, sizeof(blob));
}

global_variable u64 s_extZpcTickTicks[3]; // step, log, write-back (performance counter) since the last tick line
internal void ExtZpc_Tick(struct Driver *d)
{
	const int log = ExtZp_LogOn();
	const u64 t0 = SDL_GetPerformanceCounter();
	u64 t1, t2;

	ExtZpc_Inputs(d);
	if (log)
	{
		const uint32_t head[5] = {(uint32_t)sdata->gGT->timer, (uint32_t)d->driverID, EXT_ZP_LOG_STEP_ZONE, EXT_ZPC_STEP_FUNC, (uint32_t)s_extZpcDelta};
		uint32_t args[17];
		char note[160];
		const uint32_t pre = EXT_ZP_TAG('P', 'R', 'E', ' ');
		ExtZp_LogBegin(EXT_ZP_TAG('S', 'T', 'E', 'P'), head, 5);
		snprintf(note, sizeof(note), "K=0x%08x E=0x%08x H=0x%08x W=0x%08x", s_extZpc.kart, s_extZpc.entity, s_extZpc.body, s_extZpc.world);
		ExtZp_LogBlob(EXT_ZP_TAG('N', 'O', 'T', 'E'), note, (uint32_t)strlen(note));
		memset(args, 0, sizeof(args));
		args[0] = s_extZpc.world;
		args[8] = s_extZpc.dt;
		ExtZp_LogBlob(EXT_ZP_TAG('A', 'R', 'G', 'S'), args, sizeof(args));
		ExtZpMem_ForEachPage(s_extZpc.m, ExtZpc_LogPage, (void *)&pre);
	}
	t1 = SDL_GetPerformanceCounter();
	ExtZp_Step_336b60(&s_extZpc.c, s_extZpc.world, s_extZpc.dt);
	t2 = SDL_GetPerformanceCounter();
	s_extZpc.steps++;
	s_extZpcTickTicks[1] += t1 - t0; // the log before the step (pages)
	s_extZpcTickTicks[0] += t2 - t1; // the step with its queries
	if (log)
	{
		const uint32_t post = EXT_ZP_TAG('P', 'O', 'S', 'T');
		const uint32_t ret[2] = {0u, 0u};
		ExtZpMem_ForEachPage(s_extZpc.m, ExtZpc_LogPage, (void *)&post);
		ExtZp_LogBlob(EXT_ZP_TAG('R', 'E', 'T', ' '), ret, sizeof(ret));
		ExtZp_LogEnd();
		s_extZpcDelta = 1;
	}
	ExtZpc_WriteBack(d);
	s_extZpcTickTicks[2] += SDL_GetPerformanceCounter() - t2; // the log after the step and the write-back
	if ((s_extZpc.steps % 30u) == 1u)
	{
		const double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
		const double n = (s_extZpc.steps > 1u) ? 30.0 : 1.0;
		Platform_Log("[CTR ExtZp] port time per tick (ms, mean of %d): step %.3f (collide %.3f, ray %.3f, material %.3f, other %.3f), log+write-back %.3f; last fault 0x%08x\n",
		             (int)n, s_extZpcTickTicks[0] * ms / n, s_extZpQueryTicks[EXT_ZP_Q_COLLIDE] * ms / n, s_extZpQueryTicks[EXT_ZP_Q_RAY] * ms / n,
		             s_extZpQueryTicks[EXT_ZP_Q_MATERIAL] * ms / n, s_extZpQueryTicks[7] * ms / n,
		             (s_extZpcTickTicks[1] + s_extZpcTickTicks[2]) * ms / n, (unsigned)ExtZpMem_LastFault(s_extZpc.m));
		memset(s_extZpcTickTicks, 0, sizeof(s_extZpcTickTicks));
		memset(s_extZpQueryTicks, 0, sizeof(s_extZpQueryTicks));
		const ExtZpMem *m = s_extZpc.m;
		Platform_Log("[CTR ExtZp] port tick %u frame %d pos %d %d %d vel %d %d %d state %u ground %u maglev %u spline %d speed %.2f faults %u leaves %d\n",
		             (unsigned)s_extZpc.steps, (int)sdata->gGT->timer, d->posCurr.x >> 8, d->posCurr.y >> 8, d->posCurr.z >> 8, d->velocity.x, d->velocity.y,
		             d->velocity.z, (unsigned)ZmR32((ExtZpMem *)m, s_extZpc.kart + EXT_ZPC_K_STATE), (unsigned)ZmR8((ExtZpMem *)m, s_extZpc.kart + EXT_ZPC_K_GROUND),
		             (unsigned)ZmR8((ExtZpMem *)m, s_extZpc.kart + EXT_ZPC_K_MAGLEV), (int)ZmR32((ExtZpMem *)m, s_extZpc.kart + EXT_ZPC_K_SPLINE),
		             Ps2F_ToDouble(ZmRF((ExtZpMem *)m, s_extZpc.kart + EXT_ZPC_K_SPEED)), (unsigned)ExtZpMem_Faults(m), s_extZpc.c.leaves);
	}
}

// The driver stage hooks (game/MAIN/MainFrame.c): 1 = Reload does not run this
// stage for this driver. Without the switch both return at once.
int ExtZp_StageSkip(struct Thread *t, struct Driver *d, int stage)
{
	(void)t;
	if (!s_extZpc.ready || (d->driverID != 0))
	{
		return 0;
	}
	if (stage == DRIVER_FUNC_PHYS_LINEAR)
	{
		const int zone = ExtPhys_DriverZone(d);
		if (s_extZpc.blocked)
		{
			// after the port gave up: again only once Reload has the kart out of the zone or set it down anew
			const s32 jump = abs(d->posCurr.x - s_extZpc.keepPos[0]) + abs(d->posCurr.y - s_extZpc.keepPos[1]) + abs(d->posCurr.z - s_extZpc.keepPos[2]);
			if ((zone == 0) || (jump > (2000 << 8)))
			{
				s_extZpc.blocked = 0;
			}
			for (int k = 0; k < 3; k++)
				s_extZpc.keepPos[k] = (&d->posCurr.x)[k];
		}
		else if (!s_extZpc.active && (zone != 0))
		{
			if (!ExtZpc_LoadWorld())
			{
				Platform_Log("[CTR ExtZp] port entry frame %d: the world did not load - Reload keeps the kart\n", (int)sdata->gGT->timer);
				s_extZpc.ready = 0;
				return 0;
			}
			ExtZpc_Enter(d);
			s_extZpc.airTicks = 0;
			s_extZpcDelta = 0; // the first step of a stay logs every page
			s_extZpc.active = 1;
			s_extZpc.entries++;
			s_extZpc.enteredFrame = (uint32_t)sdata->gGT->timer;
			Platform_Log("[CTR ExtZp] port entry %u frame %d zone %d pos %d %d %d\n", (unsigned)s_extZpc.entries, (int)sdata->gGT->timer, zone, d->posCurr.x >> 8,
			             d->posCurr.y >> 8, d->posCurr.z >> 8);
		}
		else if (s_extZpc.active)
		{
			// the source's own reset state (9: the kart is lost) or no ground under the port's kart for 5 s:
			// Reload takes the kart back (its own set-down applies); otherwise the zone decides
			const uint32_t st = ZmR32(s_extZpc.m, s_extZpc.kart + EXT_ZPC_K_STATE);
			const int ground = ZmR8(s_extZpc.m, s_extZpc.kart + EXT_ZPC_K_GROUND);
			const char *why = NULL;
			s_extZpc.airTicks = ground ? 0u : s_extZpc.airTicks + 1u;
			if (zone == 0)
				why = "zone left";
			else if (st == 9u)
				why = "the source resets the kart (state 9) - given up";
			else if (s_extZpc.airTicks > 150u)
				why = "no ground for 5 s - given up";
			if (why != NULL)
			{
				s_extZpc.active = 0;
				s_extZpc.blocked = (zone != 0);
				Platform_Log("[CTR ExtZp] port exit frame %d after %u tick(s) pos %d %d %d: %s\n", (int)sdata->gGT->timer,
				             (unsigned)((uint32_t)sdata->gGT->timer - s_extZpc.enteredFrame), d->posCurr.x >> 8, d->posCurr.y >> 8, d->posCurr.z >> 8, why);
			}
		}
		return 0;
	}
	if (!s_extZpc.active)
	{
		return 0;
	}
	switch (stage)
	{
	case DRIVER_FUNC_APPLY_FORCES:
		ExtZpc_Tick(d);
		// the bookkeeping Reload's force stage does for the ground search after it (VehPhysForce_OnApplyForces):
		// without it COLL_FIXED keeps the first quad it touched, the checkpoint (lap, the helper's target) never moves
		d->collisionFlags = 0;
		d->currBlockTouching = NULL;
		return 1;
	case DRIVER_FUNC_COLL_MOVED:
	case DRIVER_FUNC_COLLIDE_DRIVERS:
	case DRIVER_FUNC_JUMP_FRICTION:
		return 1;
	default:
		return 0;
	}
}

void ExtZp_StageDone(struct Thread *t, struct Driver *d, int stage)
{
	(void)t;
	if (!s_extZpc.ready || !s_extZpc.active || (d->driverID != 0))
	{
		return;
	}
	if (stage == DRIVER_FUNC_COLL_FIXED)
	{
		// the ground search keeps its bookkeeping; position and velocity stay the port's
		for (int k = 0; k < 3; k++)
		{
			(&d->posCurr.x)[k] = s_extZpc.keepPos[k];
			(&d->velocity.x)[k] = s_extZpc.keepVel[k];
		}
	}
	else if (stage == DRIVER_FUNC_TRANSLATE_MATRIX)
	{
		// the drawn matrix is the port's orientation (TranslateMatrix built it from up and heading)
		struct Instance *inst = t->inst;
		double s0[3];
		const SVec3 upS = {(s16)floor(s_extZpc.up[0] * 4096.0 + 0.5), (s16)floor(s_extZpc.up[1] * 4096.0 + 0.5), (s16)floor(s_extZpc.up[2] * 4096.0 + 0.5)};
		ExtZpc_Cross(s_extZpc.up, s_extZpc.fwd, s0);
		for (int k = 0; k < 3; k++)
		{
			d->matrixFacingDir.m[k][0] = (s16)floor(s0[k] * 4096.0 + 0.5);
			d->matrixFacingDir.m[k][1] = (s16)floor(s_extZpc.up[k] * 4096.0 + 0.5);
			d->matrixFacingDir.m[k][2] = (s16)floor(s_extZpc.fwd[k] * 4096.0 + 0.5);
		}
		if ((inst != NULL) && (d->matrixArray == BAKED_GTE_MATRIX_NONE))
		{
			for (int r = 0; r < 3; r++)
				for (int k = 0; k < 3; k++)
					inst->matrix.m[r][k] = d->matrixFacingDir.m[r][k];
		}
		ExtPhys_PortUp(d, &upS);
	}
}

int ExtZp_PortKartState(ExtZpKartView *out)
{
	ExtZpMem *m = s_extZpc.m;
	double q[4];

	memset(out, 0, sizeof(*out));
	if (!s_extZpc.ready || !s_extZpc.active || (m == NULL))
	{
		return 0;
	}
	out->active = 1;
	out->kart = s_extZpc.kart;
	out->body = s_extZpc.body;
	out->entity = s_extZpc.entity;
	out->world = s_extZpc.world;
	out->state = (int)ZmR32(m, s_extZpc.kart + EXT_ZPC_K_STATE);
	out->ground = ZmR8(m, s_extZpc.kart + EXT_ZPC_K_GROUND);
	out->maglev = ZmR8(m, s_extZpc.kart + EXT_ZPC_K_MAGLEV);
	out->spline = (int)ZmR32(m, s_extZpc.kart + EXT_ZPC_K_SPLINE);
	out->node = (int)ZmR32(m, s_extZpc.kart + 0x468u);
	out->speed = Ps2F_ToDouble(ZmRF(m, s_extZpc.kart + EXT_ZPC_K_SPEED));
	out->airTime = Ps2F_ToDouble(ZmRF(m, s_extZpc.kart + 0x498u));
	ExtZpc_ReadVec(s_extZpc.body + EXT_ZPC_H_POS, out->pos);
	ExtZpc_ReadVec(s_extZpc.body + EXT_ZPC_H_VEL, out->vel);
	q[0] = Ps2F_ToDouble(ZmRF(m, s_extZpc.body + EXT_ZPC_H_QUAT_W));
	ExtZpc_ReadVec(s_extZpc.body + EXT_ZPC_H_QUAT_XYZ, &q[1]);
	{
		const double l = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
		if (l > 0.0)
			for (int k = 0; k < 4; k++)
				q[k] /= l;
	}
	ExtZpc_AxesFromQuat(q, out->right, out->forward, out->up);
	for (int k = 0; k < 3; k++)
	{
		out->posReload[k] = s_extZpc.keepPos[k] / 256.0;
		out->upReload[k] = s_extZpc.up[k];
		out->forwardReload[k] = s_extZpc.fwd[k];
	}
	return 1;
}

// The selftest part of the coupling (--ext-selftest phys): the orientation
// conversions on the two measured dumps (a turn about up; a general one) and
// their round trip.
void ExtZp_CoupleSelftest(int *checks, int *failures, void (*log)(const char *line))
{
	// slot 6: q (w 0.71396, z 0.70019) -> right (0.01947, 0.99981, 0), forward (-0.99981, 0.01947, 0), up (0, 0, 1)
	// slot 7: q (0.09573, -0.08436, 0.00832, 0.99179) -> right (-0.96744, 0.18848, -0.16892), forward (-0.19128, -0.98154, 0.00035), up (-0.16573, 0.03264, 0.98563)
	static const double qs[2][4] = {{0.71396, 0.0, 0.0, 0.70019}, {0.09573, -0.08436, 0.00832, 0.99179}};
	static const double want[2][3][3] = {{{0.01947, 0.99981, 0.0}, {-0.99981, 0.01947, 0.0}, {0.0, 0.0, 1.0}},
	                                     {{-0.96744, 0.18848, -0.16892}, {-0.19128, -0.98154, 0.00035}, {-0.16573, 0.03264, 0.98563}}};
	for (int t = 0; t < 2; t++)
	{
		double q[4], r[3], f[3], u[3], q2[4], err = 0.0, err2 = 0.0, l = 0.0;
		for (int k = 0; k < 4; k++)
			l += qs[t][k] * qs[t][k];
		for (int k = 0; k < 4; k++)
			q[k] = qs[t][k] / sqrt(l);
		ExtZpc_AxesFromQuat(q, r, f, u);
		for (int k = 0; k < 3; k++)
		{
			err = fmax(err, fabs(r[k] - want[t][0][k]));
			err = fmax(err, fabs(f[k] - want[t][1][k]));
			err = fmax(err, fabs(u[k] - want[t][2][k]));
		}
		ExtZp_Check(err < 2e-4, "coupling: body quaternion -> entity rows (measured dump)", checks, failures, log);
		ExtZpc_QuatFromAxes(r, f, u, q2);
		if (q2[0] * q[0] + q2[1] * q[1] + q2[2] * q[2] + q2[3] * q[3] < 0.0)
			for (int k = 0; k < 4; k++)
				q2[k] = -q2[k];
		for (int k = 0; k < 4; k++)
			err2 = fmax(err2, fabs(q2[k] - q[k]));
		ExtZp_Check(err2 < 1e-12, "coupling: entity rows -> quaternion round trip", checks, failures, log);
	}
	{
		// right = forward x up (ZUORDNUNG 3) on slot 6
		double r[3];
		ExtZpc_Cross(want[0][1], want[0][2], r);
		ExtZp_Check(fabs(r[0] - want[0][0][0]) < 1e-4 && fabs(r[1] - want[0][0][1]) < 1e-4 && fabs(r[2]) < 1e-9, "coupling: right = forward x up", checks, failures, log);
	}
	{
		ExtZpKartView v;
		ExtZp_Check(!ExtZp_PortActive() && (ExtZp_PortMemory(NULL, NULL, NULL, NULL) == NULL) && !ExtZp_PortKartState(&v) && !v.active,
		            "coupling: off without the switch", checks, failures, log);
	}
	{
		// the write journal of an answer (step log 'QRY '): adjacent writes joined, in order, nothing outside Begin/End
		ExtZpMem *m = ExtZpMem_Create();
		uint32_t bytes = 0, count = 0, a0 = 0, l0 = 0, a1 = 0, l1 = 0, v = 0;
		const uint8_t *j;
		int ok = (m != NULL);
		if (ok)
		{
			ZmW32(m, 0x00100000u, 1u);
			ExtZpMem_JournalBegin(m);
			ZmW32(m, 0x00200000u, 0x11111111u);
			ZmW32(m, 0x00200004u, 0x22222222u);
			ZmW8(m, 0x00300001u, 0x33u);
			j = ExtZpMem_JournalEnd(m, &bytes, &count);
			ZmW32(m, 0x00400000u, 4u);
			memcpy(&a0, j, 4);
			memcpy(&l0, j + 4, 4);
			memcpy(&v, j + 12, 4);
			memcpy(&a1, j + 16, 4);
			memcpy(&l1, j + 20, 4);
			ok = (count == 2u) && (bytes == 8u + 8u + 8u + 1u) && (a0 == 0x00200000u) && (l0 == 8u) && (v == 0x22222222u) && (a1 == 0x00300001u) &&
			     (l1 == 1u) && (j[24] == 0x33u);
			ExtZpMem_Destroy(m);
		}
		ExtZp_Check(ok, "coupling: write journal of an answer", checks, failures, log);
	}
}
