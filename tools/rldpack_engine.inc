// rldpack_engine.inc - rldpack make-engine: a folder of the external track
// (exttrack.json and the files it names) as an engine track container,
// format 4.4 (include/rldtrack.inc, docs/CONTAINER_FORMAT.md section 2a).
//
// AN INCLUDE OF tools/rldpack.c, after Rld_ReadFile, Rld_ParseModes,
// Rld_IsUtf8, Rld_BuildMeta and Rld_AddChunk, which it uses.
//
// PROVISIONAL - MAINTAINER DECIDES: the split of the description over the
// section chunks (s_rldEngineSections and RldEngine_IsZoneMember below) is
// the packer's choice alone - a reader joins whatever section chunks it finds
// (Rld_JoinSections) - so another split is a change here and nowhere else.
//
// THE VALUES ARE NOT WRITTEN AGAIN. Each section goes into its chunk as the
// bytes it has in exttrack.json (the member's key and value spans), so a
// number keeps its spelling and the joined description holds the source's
// values byte for byte; only the order of the sections and the white space
// between them follow the container.
//
// What it checks before it writes: the description is one JSON object
// (structure, no key twice on the first level, none twice inside physics), the
// sections every engine track needs are there (render, collision, race), the
// files it names exist, have allowed names and start like a GLB 2 / a RIFF
// WAVE, every chunk keeps its limit. After writing it opens the file with the
// game's reader: kind engine, every hash, every file chunk, the join, and the
// joined sections against the source, value bytes for value bytes.

#define RLD_ENGINE_SECTION_COUNT 10

// Section chunk per top-level key; a key not listed goes to TDSC. physics is
// split between PHYS and ZONE (RldEngine_IsZoneMember).
struct RldEngineSectionRule
{
	const char *key;
	const char *chunk;
};

static const struct RldEngineSectionRule s_rldEngineSections[] = {
    {"render", "GDEF"}, {"sky", "GDEF"},       {"uv_anims", "GDEF"}, {"node_anims", "GDEF"}, {"collision", "COLS"}, {"race", "PATH"},
    {"nav", "PATH"},    {"crates", "PLAC"},    {"obstacles", "OBST"}, {"portals", "PORT"},   {"map", "MINI"},
};

// The order the chunks are written in (META first, the files where they belong).
static const char *const s_rldEngineOrder[15] = {"META", "TDSC", "GDEF", "GEOM", "SKYB", "COLS", "COLG", "PATH",
                                                 "PLAC", "PHYS", "ZONE", "OBST", "PORT", "MINI", "STRM"};

// A key span (with its quotes) equals the C string name.
static int RldEngine_KeyIs(const struct RldJsonMember *m, const char *name)
{
	const size_t length = strlen(name);

	return (m->keyLength == length + 2u) && (memcmp(&m->key[1], name, length) == 0);
}

static int RldEngine_KeyStarts(const struct RldJsonMember *m, const char *prefix)
{
	const size_t length = strlen(prefix);

	return (m->keyLength >= length + 2u) && (memcmp(&m->key[1], prefix, length) == 0);
}

// The members of physics that go into ZONE: the anti-gravity zones, volumes
// and splines. Provisional - maintainer decides.
static int RldEngine_IsZoneMember(const struct RldJsonMember *m)
{
	return RldEngine_KeyIs(m, "zones") || RldEngine_KeyStarts(m, "maglev") || RldEngine_KeyStarts(m, "maggrav");
}

static const char *RldEngine_ChunkOf(const struct RldJsonMember *m)
{
	size_t i;

	for (i = 0; i < sizeof(s_rldEngineSections) / sizeof(s_rldEngineSections[0]); i++)
	{
		if (RldEngine_KeyIs(m, s_rldEngineSections[i].key))
		{
			return s_rldEngineSections[i].chunk;
		}
	}
	return "TDSC";
}

// The string value of member name inside the object span obj (an object's
// value span), copied without its quotes. 1 found, 0 absent, -1 not a plain
// string (escapes are not allowed in a file name).
static int RldEngine_FileName(const char *obj, size_t objSize, const char *name, char *out, size_t outSize)
{
	struct RldJsonMember members[RLD_JSON_MEMBERS_MAX];
	const int n = Rld_JsonMembers(obj, objSize, 0, members, RLD_JSON_MEMBERS_MAX);
	int i;

	out[0] = '\0';
	for (i = 0; i < n; i++)
	{
		if (RldEngine_KeyIs(&members[i], name))
		{
			const struct RldJsonMember *m = &members[i];

			if ((m->valueLength < 2u) || (m->value[0] != '"') || (memchr(m->value, '\\', m->valueLength) != NULL) ||
			    ((m->valueLength - 2u) >= outSize))
			{
				return -1;
			}
			memcpy(out, &m->value[1], m->valueLength - 2u);
			out[m->valueLength - 2u] = '\0';
			return 1;
		}
	}
	return 0;
}

// A growing text buffer for one section chunk: the 8-byte head, then "{...}".
struct RldEngineText
{
	u8 *bytes;
	size_t used;
	size_t room;
	int members;
};

static int RldEngine_Put(struct RldEngineText *t, const void *bytes, size_t size)
{
	if (t->used + size + 2u > t->room)
	{
		size_t room = (t->room != 0u) ? t->room : 256u;
		u8 *grown;

		while (t->used + size + 2u > room)
		{
			room *= 2u;
		}
		grown = (u8 *)realloc(t->bytes, room);
		if (grown == NULL)
		{
			return 0;
		}
		t->bytes = grown;
		t->room = room;
	}
	memcpy(&t->bytes[t->used], bytes, size);
	t->used += size;
	return 1;
}

static int RldEngine_Begin(struct RldEngineText *t)
{
	u8 head[RLD_SECTION_HEAD];

	memset(head, 0, sizeof(head));
	Rld_WriteLE32(&head[0], RLD_ENGINE_LAYOUT);
	return RldEngine_Put(t, head, sizeof(head)) && RldEngine_Put(t, "{", 1);
}

// One member "key":value, with a comma before all but the first.
static int RldEngine_Member(struct RldEngineText *t, const char *key, size_t keyLength, const char *value, size_t valueLength)
{
	int ok = 1;

	if (t->members > 0)
	{
		ok = RldEngine_Put(t, ",", 1);
	}
	ok = ok && RldEngine_Put(t, key, keyLength) && RldEngine_Put(t, ":", 1) && RldEngine_Put(t, value, valueLength);
	t->members++;
	return ok;
}

// The file chunk of one file: head, name, bytes.
static u8 *RldEngine_Blob(const char *name, const u8 *file, size_t fileSize, size_t *sizeOut)
{
	const size_t nameLength = strlen(name);
	u8 *bytes = (u8 *)calloc(1, RLD_BLOB_HEAD + nameLength + fileSize + 1u);

	*sizeOut = 0;
	if (bytes == NULL)
	{
		return NULL;
	}
	Rld_WriteLE32(&bytes[0x00], RLD_ENGINE_LAYOUT);
	Rld_WriteLE32(&bytes[0x04], (u32)nameLength);
	memcpy(&bytes[RLD_BLOB_HEAD], name, nameLength);
	if (fileSize > 0u)
	{
		memcpy(&bytes[RLD_BLOB_HEAD + nameLength], file, fileSize);
	}
	*sizeOut = RLD_BLOB_HEAD + nameLength + fileSize;
	return bytes;
}

struct RldEngineInput
{
	const char *folder;
	const char *strings[RLD_STRING_COUNT];
	u32 modes;
	u32 trackVersion;
	u32 minVersion;
};

// A file of the folder by its name in the description, checked: name, size, start.
static const char *RldEngine_ReadNamed(const char *folder, const char *type, const char *name, u8 **file, size_t *fileSize, char *why, size_t whySize)
{
	char path[1024];
	u8 head[RLD_BLOB_HEAD + RLD_BLOB_NAME_MAX];
	char back[RLD_BLOB_NAME_MAX + 1];
	const u8 *data;
	size_t dataSize;
	const char *error;
	const size_t nameLength = strlen(name);

	*file = NULL;
	*fileSize = 0;
	// The name rule is the reader's: build a head and let Rld_BlobView judge it.
	memset(head, 0, sizeof(head));
	Rld_WriteLE32(&head[0], RLD_ENGINE_LAYOUT);
	Rld_WriteLE32(&head[4], (u32)nameLength);
	if ((nameLength == 0u) || (nameLength > RLD_BLOB_NAME_MAX))
	{
		snprintf(why, whySize, "%s: the file name '%s' is empty or longer than 64 bytes", type, name);
		return why;
	}
	memcpy(&head[RLD_BLOB_HEAD], name, nameLength);
	error = Rld_BlobView(head, RLD_BLOB_HEAD + nameLength, back, &data, &dataSize);
	if (error != NULL)
	{
		snprintf(why, whySize, "%s: '%s' - %s", type, name, error);
		return why;
	}
	if (snprintf(path, sizeof(path), "%s/%s", folder, name) >= (int)sizeof(path))
	{
		snprintf(why, whySize, "%s: the path of '%s' is too long", type, name);
		return why;
	}
	if (!Rld_ReadFile(path, file, fileSize))
	{
		snprintf(why, whySize, "%s: %s cannot be read", type, path);
		return why;
	}
	error = Rld_BlobCheckContent((const u8 *)type, *file, *fileSize);
	if (error == NULL && ((RLD_BLOB_HEAD + nameLength + *fileSize) > Rld_ChunkLimit((const u8 *)type)))
	{
		error = "the file is larger than the chunk's limit";
	}
	if (error != NULL)
	{
		snprintf(why, whySize, "%s: %s - %s", type, path, error);
		free(*file);
		*file = NULL;
		*fileSize = 0;
		return why;
	}
	return NULL;
}

// Builds the container in memory (the caller frees). NULL and why on a refusal.
static u8 *Rld_BuildEngine(const struct RldEngineInput *in, size_t *sizeOut, char *why, size_t whySize)
{
	struct RldEngineText texts[RLD_ENGINE_SECTION_COUNT];
	struct RldJsonMember *top = NULL;
	struct RldJsonMember *sub = NULL;
	struct RldContainer container;
	struct RldMemNeed need;
	char path[1024];
	char names[4][RLD_BLOB_NAME_MAX + 1];
	const char *blobTypes[4] = {"GEOM", "SKYB", "COLG", "STRM"};
	u8 *files[4] = {NULL, NULL, NULL, NULL};
	size_t fileSizes[4] = {0, 0, 0, 0};
	u8 *json = NULL;
	size_t jsonSize = 0;
	u8 *out = NULL;
	u8 *meta = NULL;
	size_t metaSize = 0;
	int topCount;
	int haveRender = 0;
	int haveCollision = 0;
	int haveRace = 0;
	int i;
	int j;
	int ok = 1;

	*sizeOut = 0;
	why[0] = '\0';
	memset(texts, 0, sizeof(texts));
	memset(&container, 0, sizeof(container));
	memset(&need, 0, sizeof(need));
	memset(names, 0, sizeof(names));

	if (snprintf(path, sizeof(path), "%s/exttrack.json", in->folder) >= (int)sizeof(path))
	{
		snprintf(why, whySize, "the folder path is too long");
		return NULL;
	}
	if (!Rld_ReadFile(path, &json, &jsonSize))
	{
		snprintf(why, whySize, "%s cannot be read", path);
		return NULL;
	}
	top = (struct RldJsonMember *)malloc(sizeof(*top) * RLD_JSON_MEMBERS_MAX);
	sub = (struct RldJsonMember *)malloc(sizeof(*sub) * RLD_JSON_MEMBERS_MAX);
	if ((top == NULL) || (sub == NULL))
	{
		snprintf(why, whySize, "out of memory");
		goto finish;
	}
	if (jsonSize > RLD_ENGINE_TEXT_MAX)
	{
		snprintf(why, whySize, "%s is larger than 32 MiB", path);
		goto finish;
	}
	topCount = Rld_JsonMembers((const char *)json, jsonSize, 0, top, RLD_JSON_MEMBERS_MAX);
	if (topCount < 0)
	{
		snprintf(why, whySize, "%s is not one JSON object (or has more than 512 sections)", path);
		goto finish;
	}
	for (i = 0; i < topCount; i++)
	{
		for (j = 0; j < i; j++)
		{
			if (Rld_JsonSameKey(&top[i], &top[j]))
			{
				snprintf(why, whySize, "%s: the section %.*s appears twice", path, (int)top[i].keyLength, top[i].key);
				goto finish;
			}
		}
	}

	// The sections into their chunks, in source order.
	for (i = 0; i < RLD_ENGINE_SECTION_COUNT; i++)
	{
		ok = ok && RldEngine_Begin(&texts[i]);
	}
	for (i = 0; ok && (i < topCount); i++)
	{
		const struct RldJsonMember *m = &top[i];

		haveRender |= RldEngine_KeyIs(m, "render");
		haveCollision |= RldEngine_KeyIs(m, "collision");
		haveRace |= RldEngine_KeyIs(m, "race");
		if (RldEngine_KeyIs(m, "physics"))
		{
			struct RldEngineText *phys = &texts[5];
			struct RldEngineText *zone = &texts[6];
			int physOpen = 0;
			int zoneOpen = 0;
			int n;
			int a;

			n = (m->value[0] == '{') ? Rld_JsonMembers(m->value, m->valueLength, 0, sub, RLD_JSON_MEMBERS_MAX) : -1;
			if (n < 0)
			{
				snprintf(why, whySize, "%s: physics is not an object", path);
				goto finish;
			}
			for (a = 0; a < n; a++)
			{
				int b;

				for (b = 0; b < a; b++)
				{
					if (Rld_JsonSameKey(&sub[a], &sub[b]))
					{
						snprintf(why, whySize, "%s: physics.%.*s appears twice", path, (int)sub[a].keyLength, sub[a].key);
						goto finish;
					}
				}
			}
			for (a = 0; ok && (a < n); a++)
			{
				struct RldEngineText *t = RldEngine_IsZoneMember(&sub[a]) ? zone : phys;
				int *open = (t == zone) ? &zoneOpen : &physOpen;

				if (!*open)
				{
					ok = RldEngine_Member(t, m->key, m->keyLength, "{", 1);
					*open = 1;
					t->members = 0;
				}
				ok = ok && RldEngine_Member(t, sub[a].key, sub[a].keyLength, sub[a].value, sub[a].valueLength);
			}
			// physics {} stays a section: PHYS {"physics":{}}.
			if (ok && (n == 0))
			{
				ok = RldEngine_Member(phys, m->key, m->keyLength, "{", 1);
				physOpen = 1;
			}
			if (physOpen)
			{
				ok = ok && RldEngine_Put(phys, "}", 1);
				phys->members = 1;
			}
			if (zoneOpen)
			{
				ok = ok && RldEngine_Put(zone, "}", 1);
				zone->members = 1;
			}
			continue;
		}
		for (j = 0; j < RLD_ENGINE_SECTION_COUNT; j++)
		{
			if (strcmp(s_rldSectionTypes[j], RldEngine_ChunkOf(m)) == 0)
			{
				ok = RldEngine_Member(&texts[j], m->key, m->keyLength, m->value, m->valueLength);
			}
		}
	}
	for (i = 0; ok && (i < RLD_ENGINE_SECTION_COUNT); i++)
	{
		ok = RldEngine_Put(&texts[i], "}", 1);
	}
	if (!ok)
	{
		snprintf(why, whySize, "out of memory");
		goto finish;
	}
	if (!haveRender || !haveCollision || !haveRace)
	{
		snprintf(why, whySize, "%s: an engine track needs the sections render, collision and race (missing:%s%s%s)", path, haveRender ? "" : " render",
		         haveCollision ? "" : " collision", haveRace ? "" : " race");
		goto finish;
	}

	// The files the description names.
	{
		const char *section[4] = {"render", "sky", "collision", "music"};
		const char *member[4] = {"glb", "glb", "glb", "file"};
		const int required[4] = {1, 0, 1, 0};

		for (i = 0; i < 4; i++)
		{
			int found = 0;

			for (j = 0; j < topCount; j++)
			{
				if (RldEngine_KeyIs(&top[j], section[i]) && (top[j].value[0] == '{'))
				{
					found = RldEngine_FileName(top[j].value, top[j].valueLength, member[i], names[i], sizeof(names[i]));
				}
			}
			if (found < 0)
			{
				snprintf(why, whySize, "%s: %s.%s is not a plain file name", path, section[i], member[i]);
				goto finish;
			}
			if (found == 0)
			{
				if (required[i])
				{
					snprintf(why, whySize, "%s: %s.%s (the file of %s) is missing", path, section[i], member[i], blobTypes[i]);
					goto finish;
				}
				continue;
			}
			for (j = 0; j < i; j++)
			{
				if ((names[j][0] != '\0') && (strcmp(names[j], names[i]) == 0))
				{
					snprintf(why, whySize, "%s: %s names the same file as another section (%s)", path, section[i], names[i]);
					goto finish;
				}
			}
			if (strcmp(names[i], "exttrack.json") == 0)
			{
				snprintf(why, whySize, "%s: %s.%s names the description itself", path, section[i], member[i]);
				goto finish;
			}
			if (RldEngine_ReadNamed(in->folder, blobTypes[i], names[i], &files[i], &fileSizes[i], why, whySize) != NULL)
			{
				goto finish;
			}
		}
	}

	// META: name and author, no memory numbers (no LEV).
	meta = Rld_BuildMeta(in->trackVersion, in->modes, &need, in->strings, &metaSize);
	if (meta == NULL)
	{
		snprintf(why, whySize, "out of memory");
		goto finish;
	}

	// The chunks in the order of s_rldEngineOrder.
	for (i = 0; i < 15; i++)
	{
		const char *type = s_rldEngineOrder[i];

		if (strcmp(type, "META") == 0)
		{
			Rld_AddChunk(&container, "META", meta, metaSize, RLD_COMPRESSION_NONE);
			continue;
		}
		for (j = 0; j < 4; j++)
		{
			if ((strcmp(type, blobTypes[j]) == 0) && (files[j] != NULL))
			{
				size_t blobSize = 0;
				u8 *blob = RldEngine_Blob(names[j], files[j], fileSizes[j], &blobSize);

				if (blob == NULL)
				{
					snprintf(why, whySize, "out of memory");
					goto finish;
				}
				Rld_AddChunk(&container, type, blob, blobSize, RLD_COMPRESSION_NONE);
				free(blob);
			}
		}
		for (j = 0; j < RLD_ENGINE_SECTION_COUNT; j++)
		{
			if ((strcmp(type, s_rldSectionTypes[j]) == 0) && (texts[j].members > 0))
			{
				if (texts[j].used > Rld_ChunkLimit((const u8 *)type))
				{
					snprintf(why, whySize, "%s: the sections of %s are larger than its limit (%llu bytes)", path, type,
					         (unsigned long long)Rld_ChunkLimit((const u8 *)type));
					goto finish;
				}
				Rld_AddChunk(&container, type, texts[j].bytes, texts[j].used, RLD_COMPRESSION_NONE);
			}
		}
	}

	out = Rld_WriteEnvelope(&s_rldTrackFormat, RLD_FEATURE_MIN_RELOAD | RLD_FEATURE_ENGINE_TRACK, &container, sizeOut);
	if (out == NULL)
	{
		snprintf(why, whySize, "out of memory");
		goto finish;
	}
	// The engine track's own minor and the lowest reader (header bytes; no hash covers them).
	Rld_WriteLE16(&out[0x0a], RLD_VERSION_MINOR_ENGINE);
	Rld_WriteLE32(&out[0x14], in->minVersion);

finish:
	for (i = 0; i < container.chunkCount; i++)
	{
		free(container.chunks[i].stored);
	}
	for (i = 0; i < RLD_ENGINE_SECTION_COUNT; i++)
	{
		free(texts[i].bytes);
	}
	for (i = 0; i < 4; i++)
	{
		free(files[i]);
	}
	free(meta);
	free(top);
	free(sub);
	free(json);
	if ((out == NULL) && (why[0] == '\0'))
	{
		snprintf(why, whySize, "refused");
	}
	return out;
}

// The joined description of an opened engine track against the source text:
// every section with its value bytes, physics member by member. NULL = equal.
static const char *RldEngine_SameAsSource(const char *joined, size_t joinedSize, const char *source, size_t sourceSize)
{
	struct RldJsonMember *a = (struct RldJsonMember *)malloc(sizeof(*a) * RLD_JSON_MEMBERS_MAX);
	struct RldJsonMember *b = (struct RldJsonMember *)malloc(sizeof(*b) * RLD_JSON_MEMBERS_MAX);
	struct RldJsonMember *sa = (struct RldJsonMember *)malloc(sizeof(*sa) * RLD_JSON_MEMBERS_MAX);
	struct RldJsonMember *sb = (struct RldJsonMember *)malloc(sizeof(*sb) * RLD_JSON_MEMBERS_MAX);
	const char *error = NULL;
	int na;
	int nb;
	int i;
	int j;

	if ((a == NULL) || (b == NULL) || (sa == NULL) || (sb == NULL))
	{
		error = "out of memory";
		goto finish;
	}
	na = Rld_JsonMembers(source, sourceSize, 0, a, RLD_JSON_MEMBERS_MAX);
	nb = Rld_JsonMembers(joined, joinedSize, 0, b, RLD_JSON_MEMBERS_MAX);
	if ((na < 0) || (nb != na))
	{
		error = "the joined description has another number of sections than the source";
		goto finish;
	}
	for (i = 0; (error == NULL) && (i < na); i++)
	{
		const struct RldJsonMember *found = NULL;

		for (j = 0; j < nb; j++)
		{
			if (Rld_JsonSameKey(&a[i], &b[j]))
			{
				found = &b[j];
			}
		}
		if (found == NULL)
		{
			error = "a section of the source is missing in the container";
		}
		else if ((found->valueLength == a[i].valueLength) && (memcmp(found->value, a[i].value, a[i].valueLength) == 0))
		{
			continue;
		}
		else if (RldEngine_KeyIs(&a[i], "physics"))
		{
			const int ma = Rld_JsonMembers(a[i].value, a[i].valueLength, 0, sa, RLD_JSON_MEMBERS_MAX);
			const int mb = Rld_JsonMembers(found->value, found->valueLength, 0, sb, RLD_JSON_MEMBERS_MAX);
			int x;

			if ((ma < 0) || (mb != ma))
			{
				error = "physics in the container has another number of members than the source";
			}
			for (x = 0; (error == NULL) && (x < ma); x++)
			{
				int same = 0;
				int y;

				for (y = 0; y < mb; y++)
				{
					same |= Rld_JsonSameKey(&sa[x], &sb[y]) && (sa[x].valueLength == sb[y].valueLength) &&
					        (memcmp(sa[x].value, sb[y].value, sa[x].valueLength) == 0);
				}
				if (!same)
				{
					error = "a member of physics differs from the source";
				}
			}
		}
		else
		{
			error = "a section in the container differs from the source";
		}
	}

finish:
	free(a);
	free(b);
	free(sa);
	free(sb);
	return error;
}

// Opens the written file as the game does and checks it (see the head). A file
// for a newer CTR Reload than this build (minVersion) is opened as a reader of
// that version would open it - everything else is the same reader.
static const char *RldEngine_Verify(const char *path, const char *folder, u32 minVersion, char *why, size_t whySize)
{
	struct RldReader reader;
	char source[1024];
	u8 *json = NULL;
	size_t jsonSize = 0;
	char *joined = NULL;
	size_t joinedSize = 0;
	const u32 savedVersion = s_rldReaderVersionForTest;
	const char *error;
	int i;

	if (minVersion > Rld_ReaderVersion())
	{
		s_rldReaderVersionForTest = minVersion;
	}
	error = Rld_Open(&reader, path);
	s_rldReaderVersionForTest = savedVersion;

	if (error != NULL)
	{
		snprintf(why, whySize, "the written file does not open: %s", error);
		return why;
	}
	if (reader.kind != RLD_KIND_ENGINE)
	{
		Rld_Close(&reader);
		snprintf(why, whySize, "the written file does not read as an engine track");
		return why;
	}
	for (i = 0; (error == NULL) && (i < reader.chunkCount); i++)
	{
		const u8 *entry = &reader.directory[i * RLD_DIR_ENTRY_SIZE];
		size_t size = 0;
		u8 *data = Rld_ReadChunk(&reader, i, &size, &error);

		if ((data != NULL) && Rld_IsBlobType(entry))
		{
			char name[RLD_BLOB_NAME_MAX + 1];
			const u8 *file;
			size_t fileSize;

			error = Rld_BlobView(data, size, name, &file, &fileSize);
			if (error == NULL)
			{
				error = Rld_BlobCheckContent(entry, file, fileSize);
			}
		}
		free(data);
	}
	if (error == NULL)
	{
		error = Rld_JoinSections(&reader, &joined, &joinedSize);
	}
	Rld_Close(&reader);
	if ((error == NULL) && (snprintf(source, sizeof(source), "%s/exttrack.json", folder) < (int)sizeof(source)) && Rld_ReadFile(source, &json, &jsonSize))
	{
		error = RldEngine_SameAsSource(joined, joinedSize, (const char *)json, jsonSize);
	}
	else if (error == NULL)
	{
		error = "the source description cannot be read again";
	}
	free(json);
	free(joined);
	if (error != NULL)
	{
		snprintf(why, whySize, "the written file does not check: %s", error);
		return why;
	}
	return NULL;
}

// rldpack make-engine <folder> <out.rldtrack> [--name N] [--author A]
//                     [--modes list] [--track-version N] [--min-reload X.Y[.Z]]
static int Cmd_MakeEngine(int argc, char *argv[])
{
	struct RldEngineInput in;
	const char *folder = NULL;
	const char *outPath = NULL;
	const char *base;
	char nameBuffer[65];
	char why[600];
	char needText[16];
	u8 *bytes;
	size_t size = 0;
	FILE *file;
	int i;

	memset(&in, 0, sizeof(in));
	in.modes = RLD_MODE_RACE;
	in.trackVersion = 1;
	in.minVersion = Rld_ReaderVersion();
	in.strings[0] = NULL;
	in.strings[1] = "";

	for (i = 0; i < argc; i++)
	{
		const char *value = (i + 1 < argc) ? argv[i + 1] : NULL;

		if ((strcmp(argv[i], "--name") == 0) && (value != NULL))
		{
			in.strings[0] = value;
			i++;
		}
		else if ((strcmp(argv[i], "--author") == 0) && (value != NULL))
		{
			in.strings[1] = value;
			i++;
		}
		else if ((strcmp(argv[i], "--modes") == 0) && (value != NULL))
		{
			in.modes = Rld_ParseModes(value);
			if (in.modes == 0u)
			{
				fprintf(stderr, "rldpack: make-engine: --modes '%s' is not a list of modes (race, ctr, time, crystal)\n", value);
				return 2;
			}
			i++;
		}
		else if ((strcmp(argv[i], "--track-version") == 0) && (value != NULL))
		{
			in.trackVersion = (u32)strtoul(value, NULL, 10);
			i++;
		}
		else if ((strcmp(argv[i], "--min-reload") == 0) && (value != NULL))
		{
			in.minVersion = Rld_PackVersion(value);
			if ((in.minVersion == 0u) || (strspn(value, "0123456789.") != strlen(value)))
			{
				fprintf(stderr, "rldpack: make-engine: --min-reload '%s' is not a version x.y or x.y.z\n", value);
				return 2;
			}
			i++;
		}
		else if ((argv[i][0] != '-') && (folder == NULL))
		{
			folder = argv[i];
		}
		else if ((argv[i][0] != '-') && (outPath == NULL))
		{
			outPath = argv[i];
		}
		else
		{
			fprintf(stderr, "rldpack: make-engine: unknown or incomplete switch '%s'\n", argv[i]);
			return 2;
		}
	}
	if ((folder == NULL) || (outPath == NULL))
	{
		fprintf(stderr, "usage: rldpack make-engine <folder> <out.rldtrack> [--name N] [--author A] [--modes list] [--track-version N] [--min-reload X.Y[.Z]]\n");
		return 2;
	}
	if ((in.modes & RLD_MODE_BATTLE) != 0u)
	{
		fprintf(stderr, "rldpack: make-engine: Battle is reserved and not playable yet\n");
		return 2;
	}
	if (in.strings[0] == NULL)
	{
		base = folder + strlen(folder);
		while ((base > folder) && ((base[-1] == '/') || (base[-1] == '\\')))
		{
			base--;
		}
		{
			const char *end = base;

			while ((base > folder) && (base[-1] != '/') && (base[-1] != '\\'))
			{
				base--;
			}
			snprintf(nameBuffer, sizeof(nameBuffer), "%.*s", (int)(end - base), base);
		}
		in.strings[0] = nameBuffer;
	}
	for (i = 0; i < RLD_STRING_COUNT; i++)
	{
		if ((strlen(in.strings[i]) > 64u) || !Rld_IsUtf8(in.strings[i]))
		{
			fprintf(stderr, "rldpack: make-engine: %s must be UTF-8 and at most 64 bytes\n", (i == 0) ? "--name" : "--author");
			return 2;
		}
	}
	in.folder = folder;

	bytes = Rld_BuildEngine(&in, &size, why, sizeof(why));
	if (bytes == NULL)
	{
		fprintf(stderr, "rldpack: make-engine: %s\n", why);
		return 1;
	}
	file = fopen(outPath, "wb");
	if ((file == NULL) || (fwrite(bytes, 1, size, file) != size))
	{
		if (file != NULL)
		{
			fclose(file);
		}
		free(bytes);
		fprintf(stderr, "rldpack: make-engine: %s cannot be written\n", outPath);
		return 1;
	}
	fclose(file);
	free(bytes);

	if (RldEngine_Verify(outPath, folder, in.minVersion, why, sizeof(why)) != NULL)
	{
		fprintf(stderr, "rldpack: make-engine: %s\n", why);
		remove(outPath);
		return 1;
	}
	Rld_VersionText(in.minVersion, needText, sizeof(needText));
	printf("%s: engine track (format %u.%u), %llu bytes, needs CTR Reload %s or newer, checked with the game's reader\n", outPath,
	       (unsigned)RLD_VERSION_MAJOR, (unsigned)RLD_VERSION_MINOR_ENGINE, (unsigned long long)size, needText);
	return 0;
}
