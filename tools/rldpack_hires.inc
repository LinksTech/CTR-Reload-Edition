// rldpack_hires.inc - the hi-res driver on the command line: hires-check,
// hires-add, hires-strip.
//
// Included by tools/rldpack.c after every helper it uses (Rld_ReadFile, Rld_Say,
// Rld_WriteEnvelope, s_rldCharFormat). The rules and their messages are
// include/rldhires.inc - the same code the game and Reload Studio run; nothing
// here checks a model a second way.
//
//   hires-check <file.glb | file.rldchar>
//       prints every finding of the model (and, for a .rldchar, of its CHRG
//       chunk), then one summary line. Exit 0 = no error, 1 = errors (or a
//       .rldchar without CHRG: there is no hi-res part to check), 2 = usage.
//   hires-add <in.rldchar> <model.glb> -o <out.rldchar>
//       adds CHRG, or replaces the one there is. Refuses a model with an
//       error (the budget included) and writes nothing then.
//   hires-strip <in.rldchar> -o <out.rldchar>
//       removes CHRG.
//
// WHAT STAYS THE SAME. hires-add and hires-strip copy every other chunk as it
// is: its bytes, and its directory entry (type, flags, both sizes, SHA-256) -
// only the offsets follow the new layout, which is the one Rld_WriteEnvelope
// always writes (chunks back to back in directory order, then the directory).
// CHRG goes last. The header keeps its minor and its flags. So the same input
// gives the same bytes, and hires-strip of hires-add of a file rldpack wrote
// is that file again, byte for byte. Every chunk is read through its hash
// first: a damaged container is refused, not copied on.
//
// --machine adds one @msg line per finding (severity, rule id, text), see
// tools/reloadstudio/reloadstudio.h.

// A .rldchar read whole: the envelope checked by Rld_OpenAs, every chunk's
// stored bytes and its directory entry as they are in the file.
struct RldHiresPackIn
{
	struct RldContainer c;
	u32 minor;
	u32 flags;
};

static void RldHiresPack_Free(struct RldContainer *c)
{
	int i;

	for (i = 0; i < c->chunkCount; i++)
	{
		free(c->chunks[i].stored);
		c->chunks[i].stored = NULL;
	}
	c->chunkCount = 0;
}

// Reads a .rldchar into in. NULL = fine, else a fixed text (in is then empty).
static const char *RldHiresPack_Load(const char *path, struct RldHiresPackIn *in)
{
	struct RldReader reader;
	const char *why;
	int i;

	memset(in, 0, sizeof(*in));
	why = Rld_OpenAs(&reader, path, &s_rldCharFormat);
	if (why != NULL)
	{
		return why;
	}
	in->minor = reader.minor;
	{
		u8 header[RLD_HEADER_SIZE];

		if (!Rld_ReadAt(&reader, 0, header, sizeof(header)))
		{
			Rld_Close(&reader);
			return "the header cannot be read";
		}
		in->flags = Rld_ReadLE32(&header[0x0c]);
	}
	for (i = 0; i < reader.chunkCount; i++)
	{
		const u8 *entry = &reader.directory[i * RLD_DIR_ENTRY_SIZE];
		struct RldChunk *chunk = &in->c.chunks[in->c.chunkCount];
		const u64 offset = Rld_ReadLE64(&entry[0x08]);
		const u64 stored = Rld_ReadLE64(&entry[0x10]);
		const u64 raw = Rld_ReadLE64(&entry[0x18]);
		const u32 flags = Rld_ReadLE32(&entry[0x04]);

		memset(chunk, 0, sizeof(*chunk));
		memcpy(chunk->type, entry, 4);
		chunk->type[4] = '\0';
		chunk->flags = flags;
		chunk->sizeStored = stored;
		chunk->sizeRaw = raw;
		memcpy(chunk->hash, &entry[RLD_DIR_HASH_OFFSET], 32);
		chunk->stored = (u8 *)malloc((size_t)stored ? (size_t)stored : 1u);
		if (chunk->stored == NULL)
		{
			Rld_Close(&reader);
			RldHiresPack_Free(&in->c);
			return "out of memory";
		}
		in->c.chunkCount++;
		if (!Rld_ReadAt(&reader, offset, chunk->stored, (size_t)stored))
		{
			Rld_Close(&reader);
			RldHiresPack_Free(&in->c);
			return "a chunk cannot be read - the file is shorter than its own directory says";
		}
		// The hash is over the raw bytes; a stored chunk is its raw bytes. A
		// compressed one (no type of this format is) is copied as it is.
		if (((flags & 0x0fu) == RLD_COMPRESSION_NONE) && (stored == raw))
		{
			u8 digest[32];

			Sha256(chunk->stored, (size_t)stored, digest);
			if (memcmp(digest, chunk->hash, 32) != 0)
			{
				Rld_Close(&reader);
				RldHiresPack_Free(&in->c);
				return "a chunk does not match its hash - the file is altered or damaged";
			}
		}
	}
	Rld_Close(&reader);
	return NULL;
}

// THE PURE PART: the container of in without CHRG, plus chrg (NULL = none) as
// the last chunk, written with the header of in. malloc'ed bytes, NULL with a
// fixed text in *why. in is not changed (the chunks are shared, not copied).
static u8 *RldHiresPack_Rebuild(const struct RldHiresPackIn *in, const u8 *chrg, size_t chrgSize, size_t *sizeOut, const char **why)
{
	static struct RldContainer out;
	u8 *bytes;
	int i;

	*sizeOut = 0;
	*why = NULL;
	memset(&out, 0, sizeof(out));
	for (i = 0; i < in->c.chunkCount; i++)
	{
		if (memcmp(in->c.chunks[i].type, RLDHIRES_CHUNK_TYPE, 4) == 0)
		{
			continue;
		}
		out.chunks[out.chunkCount++] = in->c.chunks[i];
	}
	if (chrg != NULL)
	{
		struct RldChunk *chunk;

		if (out.chunkCount >= RLD_CHUNK_MAX)
		{
			*why = "the container already holds 64 chunks - there is no room for CHRG";
			return NULL;
		}
		chunk = &out.chunks[out.chunkCount++];
		memcpy(chunk->type, RLDHIRES_CHUNK_TYPE, 4);
		chunk->type[4] = '\0';
		chunk->flags = RLD_COMPRESSION_NONE;
		chunk->sizeStored = (u64)chrgSize;
		chunk->sizeRaw = (u64)chrgSize;
		Sha256(chrg, chrgSize, chunk->hash);
		chunk->stored = (u8 *)chrg; // only read by Rld_WriteEnvelope
	}
	bytes = Rld_WriteEnvelope(&s_rldCharFormat, in->flags, &out, sizeOut);
	if (bytes == NULL)
	{
		*why = "out of memory";
		return NULL;
	}
	Rld_WriteLE16(&bytes[0x0a], in->minor); // the minor of the file, not the one rldpack writes
	return bytes;
}

// Writes bytes as <path>.part and renames it into place (the way make-char
// writes a character: a failed or stopped write never leaves half a file under
// the real name). 1 = written.
static int RldHiresPack_Write(const char *path, const u8 *bytes, size_t size)
{
	static char partPath[RLD_PATH_MAX + 32];
	static char oldPath[RLD_PATH_MAX + 32];
	FILE *file;
	int written;
	int moved;

	snprintf(partPath, sizeof(partPath), "%s.part", path);
	snprintf(oldPath, sizeof(oldPath), "%s.old", path);
	file = fopen(partPath, "wb");
	if (file == NULL)
	{
		return 0;
	}
	written = (size == 0u) || (fwrite(bytes, 1, size, file) == size);
	written = (fclose(file) == 0) && written;
	if (!written)
	{
		remove(partPath);
		return 0;
	}
	remove(oldPath);
	moved = (rename(path, oldPath) == 0); // fails when there is no old file - fine
	if (rename(partPath, path) != 0)
	{
		if (moved)
		{
			rename(oldPath, path);
		}
		remove(partPath);
		return 0;
	}
	if (moved)
	{
		remove(oldPath);
	}
	return 1;
}

// Prints a report, one line per message, and the @msg lines with --machine.
static void RldHiresPack_Print(const RldHiresReport *rep)
{
	int i;

	for (i = 0; i < rep->count; i++)
	{
		const RldHiresMessage *msg = &rep->msg[i];

		printf("  %-7s %-6s %s\n", RldHires_LevelName(msg->level), msg->rule, msg->text);
		Rld_Say(RldHires_LevelName(msg->level), msg->rule, NULL, "%s", msg->text);
	}
	if (rep->dropped != 0)
	{
		printf("  (%d more messages not shown)\n", rep->dropped);
	}
}

// The summary line of hires-check (and of hires-add).
static void RldHiresPack_Summary(const char *what, const RldHiresReport *rep, const RldHiresModel *m)
{
	int clips = 0;
	int textures = 0;
	int i;

	for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
	{
		clips += (m->clipAnim[i] >= 0);
	}
	for (i = 0; i < m->imageCount; i++)
	{
		textures += (m->images[i].used != 0);
	}
	printf("%s: %d errors, %d warnings, %d notes, clips %d/%d, wheels %d/%d, skin %u joints, %llu triangles, textures %d (%llu bytes RGBA8)\n", what,
	       rep->errors, rep->warnings, rep->notes, clips, RLDHIRES_CLIP_COUNT, m->wheelsFound, RLDHIRES_WHEEL_COUNT, (unsigned)m->joints,
	       (unsigned long long)m->triangles, textures, (unsigned long long)m->textureBytes);
}

// Runs every rule on a GLB: RldHires_Open and, when it opened, the budget.
// Fills rep and m (close m with RldHires_Close). 1 = usable.
static int RldHiresPack_CheckGlb(const u8 *glb, size_t size, RldHiresModel *m, RldHiresReport *rep)
{
	int ok;

	if (size > (size_t)(RLDHIRES_CHUNK_LIMIT - RLDHIRES_CHUNK_HEADER_BYTES))
	{
		memset(m, 0, sizeof(*m));
		RldHires_Say(rep, "HRG-5", "the model is %u bytes - with its 16-byte header CHRG may be 64 MiB at most", (unsigned)size);
		return 0;
	}
	ok = RldHires_Open(glb, (u32)size, m, rep);
	if (m->data != NULL)
	{
		ok = RldHires_CheckBudget(m, rep) && ok;
	}
	// The poses of every clip (include/rldhires_hull.inc), as the engine's
	// loader builds them - only for a model that opened without an error.
	if (m->usable)
	{
		float lo[3];
		float hi[3];

		ok = RldHires_CheckClipHull(m, rep, lo, hi, NULL) && ok;
	}
	return ok;
}

static int RldHires_CheckCommand(int argc, char **argv)
{
	static RldHiresReport rep;
	RldHiresModel m;
	u8 *bytes = NULL;
	size_t size = 0;
	u8 *chunk = NULL;
	const u8 *glb;
	size_t glbSize;

	if (argc != 1)
	{
		fprintf(stderr, "usage: rldpack hires-check <file.glb | file.rldchar>\n");
		return 2;
	}
	RldHires_ReportClear(&rep);
	memset(&m, 0, sizeof(m));
	printf("hires check of %s\n", argv[0]);

	if (!Rld_ReadFile(argv[0], &bytes, &size))
	{
		fprintf(stderr, "rldpack: %s cannot be opened\n", argv[0]);
		return 1;
	}
	glb = bytes;
	glbSize = size;
	if ((size >= 8u) && (memcmp(bytes, RLDCHAR_MAGIC, 8) == 0))
	{
		struct RldReader reader;
		const char *why = Rld_OpenAs(&reader, argv[0], &s_rldCharFormat);
		RldHiresChunk c;
		const char *rule = NULL;
		char detail[RLDHIRES_TEXT_MAX];
		size_t chunkSize = 0;
		int index;

		free(bytes);
		bytes = NULL;
		if (why != NULL)
		{
			printf("  the file is no readable .rldchar: %s\n", why);
			printf("hires check: 1 errors, 0 warnings, 0 notes\n");
			return 1;
		}
		index = RldHires_FindChunk(&reader);
		if (index < 0)
		{
			Rld_Close(&reader);
			printf("  no CHRG chunk - this driver has no hi-res part (the game draws its classic model)\n");
			printf("hires check: no CHRG\n");
			return 1;
		}
		chunk = RldHires_ReadChunk(&reader, index, &chunkSize, &why);
		Rld_Close(&reader);
		if (chunk == NULL)
		{
			RldHires_Say(&rep, "HRG-5", "%s", (why != NULL) ? why : "CHRG cannot be read");
			RldHiresPack_Print(&rep);
			printf("hires check: %d errors, %d warnings, %d notes\n", rep.errors, rep.warnings, rep.notes);
			return 1;
		}
		if (!RldHires_ParseChunk(chunk, chunkSize, &c, &rule, detail, (int)sizeof(detail)))
		{
			RldHires_Say(&rep, (rule != NULL) ? rule : "HRG-2", "%s", detail);
			RldHiresPack_Print(&rep);
			printf("hires check: %d errors, %d warnings, %d notes\n", rep.errors, rep.warnings, rep.notes);
			free(chunk);
			return 1;
		}
		printf("  CHRG version %u, %u-byte header, GLB %u bytes\n", (unsigned)c.version, (unsigned)c.headerBytes, (unsigned)c.glbSize);
		glb = c.glb;
		glbSize = c.glbSize;
	}

	RldHiresPack_CheckGlb(glb, glbSize, &m, &rep);
	RldHiresPack_Print(&rep);
	RldHiresPack_Summary("hires check", &rep, &m);
	RldHires_Close(&m);
	free(chunk);
	free(bytes);
	return (rep.errors != 0) ? 1 : 0;
}

// The arguments of hires-add and hires-strip: count inputs, then -o <out>.
static int RldHiresPack_Args(int argc, char **argv, int inputs, const char **in, const char **out)
{
	int i;
	int n = 0;

	*out = NULL;
	for (i = 0; i < argc; i++)
	{
		if ((strcmp(argv[i], "-o") == 0) && ((i + 1) < argc) && (*out == NULL))
		{
			*out = argv[++i];
			continue;
		}
		if (n >= inputs)
		{
			return 0;
		}
		in[n++] = argv[i];
	}
	return (n == inputs) && (*out != NULL);
}

static int RldHiresPack_Finish(const struct RldHiresPackIn *in, const u8 *chrg, size_t chrgSize, const char *outPath)
{
	const char *why = NULL;
	size_t size = 0;
	u8 *bytes = RldHiresPack_Rebuild(in, chrg, chrgSize, &size, &why);
	u8 digest[32];
	char hex[65];
	int k;

	if (bytes == NULL)
	{
		fprintf(stderr, "rldpack: %s\n", why);
		Rld_Say("error", "write", NULL, "%s", why);
		return 1;
	}
	if (!RldHiresPack_Write(outPath, bytes, size))
	{
		free(bytes);
		fprintf(stderr, "rldpack: %s cannot be written\n", outPath);
		Rld_Say("error", "write", outPath, "The character file cannot be written. Is the folder write-protected, or is the file open in another program?");
		return 1;
	}
	Sha256(bytes, size, digest);
	for (k = 0; k < 32; k++)
	{
		snprintf(&hex[k * 2], 3, "%02x", digest[k]);
	}
	printf("wrote %s (%u bytes, %d chunks, sha256 %s)\n", outPath, (unsigned)size, (int)Rld_ReadLE32(&bytes[0x10]), hex);
	Rld_Emit("written", outPath, hex, (const char *)NULL);
	free(bytes);
	return 0;
}

static int RldHires_AddCommand(int argc, char **argv)
{
	static RldHiresReport rep;
	static struct RldHiresPackIn in;
	const char *paths[2];
	const char *outPath;
	RldHiresModel m;
	u8 *glb = NULL;
	size_t glbSize = 0;
	u8 *chunk;
	size_t chunkSize = 0;
	const char *why;
	int ok;
	int result;

	if (!RldHiresPack_Args(argc, argv, 2, paths, &outPath))
	{
		fprintf(stderr, "usage: rldpack hires-add <in.rldchar> <model.glb> -o <out.rldchar>\n");
		return 2;
	}
	if (!Rld_ReadFile(paths[1], &glb, &glbSize))
	{
		fprintf(stderr, "rldpack: %s cannot be opened\n", paths[1]);
		return 1;
	}
	RldHires_ReportClear(&rep);
	printf("hires-add: %s + %s\n", paths[0], paths[1]);
	ok = RldHiresPack_CheckGlb(glb, glbSize, &m, &rep);
	RldHiresPack_Print(&rep);
	RldHiresPack_Summary("hires check", &rep, &m);
	RldHires_Close(&m);
	if (!ok || (rep.errors != 0))
	{
		printf("hires-add: the model has %d error(s) - nothing written\n", rep.errors);
		free(glb);
		return 1;
	}

	why = RldHiresPack_Load(paths[0], &in);
	if (why != NULL)
	{
		fprintf(stderr, "rldpack: %s: %s\n", paths[0], why);
		Rld_Say("error", "rldchar", paths[0], "%s is no readable character: %s", paths[0], why);
		free(glb);
		return 1;
	}
	chunk = RldHires_BuildChunk(glb, (u32)glbSize, &chunkSize);
	free(glb);
	if (chunk == NULL)
	{
		RldHiresPack_Free(&in.c);
		fprintf(stderr, "rldpack: out of memory\n");
		return 1;
	}
	result = RldHiresPack_Finish(&in, chunk, chunkSize, outPath);
	free(chunk);
	RldHiresPack_Free(&in.c);
	return result;
}

static int RldHires_StripCommand(int argc, char **argv)
{
	static struct RldHiresPackIn in;
	const char *paths[1];
	const char *outPath;
	const char *why;
	int had = 0;
	int i;
	int result;

	if (!RldHiresPack_Args(argc, argv, 1, paths, &outPath))
	{
		fprintf(stderr, "usage: rldpack hires-strip <in.rldchar> -o <out.rldchar>\n");
		return 2;
	}
	why = RldHiresPack_Load(paths[0], &in);
	if (why != NULL)
	{
		fprintf(stderr, "rldpack: %s: %s\n", paths[0], why);
		return 1;
	}
	for (i = 0; i < in.c.chunkCount; i++)
	{
		had |= (memcmp(in.c.chunks[i].type, RLDHIRES_CHUNK_TYPE, 4) == 0);
	}
	printf("hires-strip: %s%s\n", paths[0], had ? "" : " (it has no CHRG - written without change of content)");
	result = RldHiresPack_Finish(&in, NULL, 0, outPath);
	RldHiresPack_Free(&in.c);
	return result;
}

static void RldHires_Usage(FILE *out)
{
	fprintf(out, "HI-RES DRIVER (chunk CHRG, preview)\n");
	fprintf(out, "  rldpack hires-check <file.glb | file.rldchar>\n");
	fprintf(out, "  Checks a glTF binary model (.glb) with the game's rules (include/rldhires.inc),\n");
	fprintf(out, "  or the CHRG chunk of a character. One line per finding (error: the hi-res\n");
	fprintf(out, "  model is refused; warning: something falls back to a standard; note: ignored),\n");
	fprintf(out, "  then a summary. Exit 0 = no error, 1 = errors or no CHRG.\n");
	fprintf(out, "  rldpack hires-add <in.rldchar> <model.glb> -o <out.rldchar>\n");
	fprintf(out, "  Adds the model as CHRG (or replaces it). Refuses a model with an error, the\n");
	fprintf(out, "  budget included. Every other chunk stays byte for byte.\n");
	fprintf(out, "  rldpack hires-strip <in.rldchar> -o <out.rldchar>\n");
	fprintf(out, "  Removes CHRG; every other chunk stays byte for byte.\n\n");
}
