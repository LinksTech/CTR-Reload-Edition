#include <platform/native_extzoneport.h>

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
	s->present = 1;
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

internal void ExtZp_RecPut(const void *data, uint32_t n)
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
	memcpy(s_extZpRec + s_extZpRecLen, data, n);
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

void ExtZp_LogBlob(uint32_t tag, const void *data, uint32_t length)
{
	static const unsigned char pad[4] = {0, 0, 0, 0};

	ExtZp_RecPut(&tag, 4);
	ExtZp_RecPut(&length, 4);
	ExtZp_RecPut(data, length);
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
