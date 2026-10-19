// rldpack_hires.inc - the hi-res driver on the command line: hires-check,
// hires-add, hires-strip (hires-upscale: rldpack_hires_upscale.inc, hires-png:
// rldpack_hires_png.inc).
//
// Included by tools/rldpack.c after every helper it uses (Rld_ReadFile, Rld_Say,
// Rld_WriteEnvelope, s_rldCharFormat). The rules and their messages are
// include/rldhires.inc - the same code the game and Reload Studio run; nothing
// here checks a model a second way.
//
//   hires-check <file.glb | file.rldchar> [--vertex-colors gltf|psx|off] [--kart-part | --game-kart <0..14|template>]
//       prints every finding of the model (and, for a .rldchar, of its CHRG
//       chunk), the vertex colour mode, the parts, then one summary line. A
//       .rldchar is checked in the mode and with the parts its CHRG names (the
//       switches are refused there), a .glb as the switches say (glTF, no
//       parts without them). Exit 0 = no error, 1 = errors (or a .rldchar
//       without CHRG: there is no hi-res part to check), 2 = usage.
//   hires-add <in.rldchar> <model.glb> -o <out.rldchar> [--vertex-colors gltf|psx|off] [--kart-part | --game-kart <0..14|template>]
//       adds CHRG, or replaces the one there is, with the vertex colour mode
//       in its flags (include/rldhires_color.inc; without the switch glTF,
//       flags 0: the bytes hires-add wrote before the mode). Refuses a model
//       with an error (the budget included) and writes nothing then.
//       --kart-part writes the 20-byte header with parts bit 0 (THE PARTS,
//       include/rldhires.inc): the subtree of the node "kart" is left out where
//       the kart is not wanted. Refused when the model has no usable kart part
//       (HRG-37, HRG-38). --game-kart writes bit 1 and the kart donor: the file
//       has no kart, the game puts in the player's own. Without either the
//       header is the 16 bytes of before.
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

// The summary line of hires-check (and of hires-add), then the line of the
// optional clips. "clips n/12" counts the clips every file is asked for
// (RLDHIRES_CLIP_BASE), as before the optional ones existed.
static void RldHiresPack_Summary(const char *what, const RldHiresReport *rep, const RldHiresModel *m)
{
	int clips = 0;
	int finish = 0;
	int textures = 0;
	int i;

	for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
	{
		if (RldHires_ClipOptional(i))
		{
			finish += (m->clipAnim[i] >= 0);
		}
		else
		{
			clips += (m->clipAnim[i] >= 0);
		}
	}
	for (i = 0; i < m->imageCount; i++)
	{
		textures += (m->images[i].used != 0);
	}
	printf("%s: %d errors, %d warnings, %d notes, clips %d/%d, wheels %d/%d, skin %u joints, %llu triangles, textures %d (%llu bytes RGBA8)\n", what,
	       rep->errors, rep->warnings, rep->notes, clips, RLDHIRES_CLIP_BASE, m->wheelsFound, RLDHIRES_WHEEL_COUNT, (unsigned)m->joints,
	       (unsigned long long)m->triangles, textures, (unsigned long long)m->textureBytes);
	printf("%s: finish clips %d/%d (optional: finish_win %s, finish_lose %s; a missing one plays what the game played before)\n", what, finish,
	       RLDHIRES_CLIP_COUNT - RLDHIRES_CLIP_BASE, (m->clipAnim[RLDHIRES_CLIP_FINISH_WIN] >= 0) ? "yes" : "no",
	       (m->clipAnim[RLDHIRES_CLIP_FINISH_LOSE] >= 0) ? "yes" : "no");
}

// What a vertex colour mode means, for the line hires-check and hires-add print.
static const char *RldHiresPack_ModeText(int mode)
{
	return (mode == RLDHIRES_VCOL_GLTF)  ? "gltf (white is neutral)"
	       : (mode == RLDHIRES_VCOL_PSX) ? "psx (PSX blend: grey 128 is neutral, texture x colour / 128)"
	       : (mode == RLDHIRES_VCOL_OFF) ? "off (COLOR_0 is ignored)"
	                                     : "unknown";
}

// Takes --vertex-colors <gltf|psx|off> out of argv (anywhere, once): the other
// arguments go to rest in their order, their count is returned. *mode is the
// mode (glTF without the switch), *given 1 when the switch was there. -1 = a
// missing or unknown value, or the switch twice (a usage error).
static int RldHiresPack_VertexColours(int argc, char **argv, char **rest, int *mode, int *given)
{
	int n = 0;
	int i;

	*mode = RLDHIRES_VCOL_GLTF;
	*given = 0;
	for (i = 0; i < argc; i++)
	{
		if (strcmp(argv[i], "--vertex-colors") == 0)
		{
			if (*given || ((i + 1) >= argc) || (RldHiresColor_ModeByName(argv[i + 1]) < 0))
			{
				return -1;
			}
			*mode = RldHiresColor_ModeByName(argv[++i]);
			*given = 1;
			continue;
		}
		rest[n++] = argv[i];
	}
	return n;
}

// Takes --kart-part and --game-kart <0..14|template> out of argv (anywhere,
// once, not both): the other arguments go to rest in their order, their count
// is returned. *parts is the field 0x10 they ask for (0 without them), *given 1
// when one was there. -1 = a usage error.
static int RldHiresPack_Parts(int argc, char **argv, char **rest, u32 *parts, int *given)
{
	int n = 0;
	int i;

	*parts = 0u;
	*given = 0;
	for (i = 0; i < argc; i++)
	{
		if (strcmp(argv[i], "--kart-part") == 0)
		{
			if (*given)
			{
				return -1;
			}
			*parts = RLDHIRES_PART_KART;
			*given = 1;
			continue;
		}
		if (strcmp(argv[i], "--game-kart") == 0)
		{
			u32 donor = RLDHIRES_PART_DONOR_TEMPLATE;

			if (*given || ((i + 1) >= argc))
			{
				return -1;
			}
			i++;
			if (strcmp(argv[i], "template") != 0)
			{
				char *end = NULL;
				const unsigned long v = strtoul(argv[i], &end, 10);

				if ((end == argv[i]) || (*end != '\0') || (v > RLDHIRES_PART_DONOR_MAX))
				{
					return -1;
				}
				donor = (u32)v;
			}
			*parts = RLDHIRES_PART_GAME_KART | (donor << RLDHIRES_PART_DONOR_SHIFT);
			*given = 1;
			continue;
		}
		rest[n++] = argv[i];
	}
	return n;
}

// What a parts field means, for the line hires-check and hires-add print.
static void RldHiresPack_PartsLine(u32 parts, const RldHiresModel *m)
{
	if (parts == 0u)
	{
		printf("  parts: none (16-byte header)\n");
	}
	else if ((parts & RLDHIRES_PART_KART) != 0u)
	{
		printf("  parts: 0x%08x, kart part %s (%u nodes)\n", (unsigned)parts, m->kartPart ? "on" : "off", (unsigned)m->kartNodes);
	}
	else if (((parts & RLDHIRES_PART_GAME_KART) != 0u) && (m->kartDonor == (int)RLDHIRES_PART_DONOR_TEMPLATE))
	{
		printf("  parts: 0x%08x, the game's kart (of the template)\n", (unsigned)parts);
	}
	else if ((parts & RLDHIRES_PART_GAME_KART) != 0u)
	{
		printf("  parts: 0x%08x, the game's kart (of driver %d)\n", (unsigned)parts, m->kartDonor);
	}
	else
	{
		printf("  parts: 0x%08x\n", (unsigned)parts);
	}
}

// Runs every rule on a GLB in a vertex colour mode with a parts field:
// RldHires_OpenParts and, when it opened, the budget. Fills rep and m (close m
// with RldHires_Close). 1 = usable.
static int RldHiresPack_CheckGlbParts(const u8 *glb, size_t size, int vcolMode, u32 parts, RldHiresModel *m, RldHiresReport *rep)
{
	int ok;
	const u32 header = (parts != 0u) ? RLDHIRES_CHUNK_HEADER_BYTES_PARTS : RLDHIRES_CHUNK_HEADER_BYTES;

	if (size > (size_t)(RLDHIRES_CHUNK_LIMIT - header))
	{
		memset(m, 0, sizeof(*m));
		RldHires_Say(rep, "HRG-5", "the model is %u bytes - with its %u-byte header CHRG may be 64 MiB at most", (unsigned)size, (unsigned)header);
		return 0;
	}
	ok = RldHires_OpenParts(glb, (u32)size, vcolMode, parts, m, rep);
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

// RldHiresPack_CheckGlbParts without the parts field.
static int RldHiresPack_CheckGlbAs(const u8 *glb, size_t size, int vcolMode, RldHiresModel *m, RldHiresReport *rep)
{
	return RldHiresPack_CheckGlbParts(glb, size, vcolMode, 0u, m, rep);
}

// RldHiresPack_CheckGlbAs in glTF mode.
static int RldHiresPack_CheckGlb(const u8 *glb, size_t size, RldHiresModel *m, RldHiresReport *rep)
{
	return RldHiresPack_CheckGlbAs(glb, size, RLDHIRES_VCOL_GLTF, m, rep);
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
	char *rest[8];
	char *rest2[8];
	int mode = RLDHIRES_VCOL_GLTF;
	int given = 0;
	u32 parts = 0u;
	int partsGiven = 0;
	const int restCount = (argc > 8) ? -1 : RldHiresPack_VertexColours(argc, argv, rest, &mode, &given);

	if ((restCount < 0) || (RldHiresPack_Parts(restCount, rest, rest2, &parts, &partsGiven) != 1))
	{
		fprintf(stderr, "usage: rldpack hires-check <file.glb | file.rldchar> [--vertex-colors gltf|psx|off] [--kart-part | --game-kart <0..14|template>]\n");
		return 2;
	}
	argv = rest2;
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
		if (given || partsGiven)
		{
			if (why == NULL)
			{
				Rld_Close(&reader);
			}
			fprintf(stderr, "rldpack: --vertex-colors, --kart-part and --game-kart are for a .glb - a .rldchar is checked as its CHRG says\n");
			return 2;
		}
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
		mode = RldHiresColor_Mode(c.flags); // ParseChunk refused mode 3
		parts = c.parts;
	}

	printf("  vertex colours: %s\n", RldHiresPack_ModeText(mode));
	RldHiresPack_CheckGlbParts(glb, glbSize, mode, parts, &m, &rep);
	RldHiresPack_PartsLine(parts, &m);
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

// hires-add --game-kart on a file with CHRI bit 5 (make-char --figure-only,
// include/rldchar.inc THE GAME KART): the classic model and the hi-res part show
// the same kart, so the donor of --game-kart goes into CHRI too (one line when
// it changes). A file without bit 5, or one whose field is not there, stays as it is.
static void RldHiresPack_GameKartDonor(struct RldHiresPackIn *in, u32 donor)
{
	int i;

	for (i = 0; i < in->c.chunkCount; i++)
	{
		struct RldChunk *chunk = &in->c.chunks[i];
		struct RldCharInfo info;
		struct RldCharGameKart gk;
		char was[12];
		char now[12];

		if ((memcmp(chunk->type, "CHRI", 4) != 0) || (chunk->sizeStored != chunk->sizeRaw) || (chunk->stored == NULL))
		{
			continue;
		}
		if ((RldChar_ParseInfo(&info, chunk->stored, (size_t)chunk->sizeStored) != NULL) ||
		    (RldChar_ParseGameKart(&info, chunk->stored, (size_t)chunk->sizeStored, &gk) != NULL) || (gk.donor == donor))
		{
			return;
		}
		RldMk_DonorText(gk.donor, was, sizeof(was));
		RldMk_DonorText(donor, now, sizeof(now));
		chunk->stored[RLDCHAR_CHRI_KART_DONOR] = (u8)donor;
		Sha256(chunk->stored, (size_t)chunk->sizeStored, chunk->hash);
		printf("  game kart: the classic model's kart donor (CHRI) set from %s to %s, the hi-res part's\n", was, now);
		Rld_Say("info", "game-kart", NULL, "The classic model's kart donor (CHRI) was %s; it is %s now, as the hi-res part's, so both show the same kart.", was, now);
		return;
	}
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
	int kartRefused;
	int result;
	char *rest[10];
	char *rest2[10];
	int mode = RLDHIRES_VCOL_GLTF;
	int given = 0;
	u32 parts = 0u;
	int partsGiven = 0;
	int restCount = (argc > 10) ? -1 : RldHiresPack_VertexColours(argc, argv, rest, &mode, &given);

	if (restCount >= 0)
	{
		restCount = RldHiresPack_Parts(restCount, rest, rest2, &parts, &partsGiven);
	}
	if ((restCount < 0) || !RldHiresPack_Args(restCount, rest2, 2, paths, &outPath))
	{
		fprintf(stderr, "usage: rldpack hires-add <in.rldchar> <model.glb> -o <out.rldchar> [--vertex-colors gltf|psx|off] [--kart-part | --game-kart <0..14|template>]\n");
		return 2;
	}
	if (!Rld_ReadFile(paths[1], &glb, &glbSize))
	{
		fprintf(stderr, "rldpack: %s cannot be opened\n", paths[1]);
		return 1;
	}
	RldHires_ReportClear(&rep);
	printf("hires-add: %s + %s\n", paths[0], paths[1]);
	printf("  vertex colours: %s\n", RldHiresPack_ModeText(mode));
	ok = RldHiresPack_CheckGlbParts(glb, glbSize, mode, parts, &m, &rep);
	RldHiresPack_PartsLine(parts, &m);
	RldHiresPack_Print(&rep);
	RldHiresPack_Summary("hires check", &rep, &m);
	kartRefused = ((parts & RLDHIRES_PART_KART) != 0u) && !m.kartPart;
	RldHires_Close(&m);
	if (!ok || (rep.errors != 0))
	{
		printf("hires-add: the model has %d error(s) - nothing written\n", rep.errors);
		free(glb);
		return 1;
	}
	if (kartRefused)
	{
		printf("hires-add: --kart-part, but the model has no usable kart part (HRG-37, HRG-38) - nothing written\n");
		Rld_Say("error", "kart-part", paths[1], "The model has no node 'kart' whose subtree can be left out (HRG-37, HRG-38). Nothing was written.");
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
	if ((parts & RLDHIRES_PART_GAME_KART) != 0u)
	{
		RldHiresPack_GameKartDonor(&in, (parts & RLDHIRES_PART_DONOR_MASK) >> RLDHIRES_PART_DONOR_SHIFT);
	}
	chunk = RldHires_BuildChunkParts(glb, (u32)glbSize, (u32)mode, parts, &chunkSize);
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
	fprintf(out, "  rldpack hires-check <file.glb | file.rldchar> [--vertex-colors gltf|psx|off] [--kart-part | --game-kart <n>]\n");
	fprintf(out, "  Checks a glTF binary model (.glb) with the game's rules (include/rldhires.inc),\n");
	fprintf(out, "  or the CHRG chunk of a character. One line per finding (error: the hi-res\n");
	fprintf(out, "  model is refused; warning: something falls back to a standard; note: ignored),\n");
	fprintf(out, "  then a summary. Exit 0 = no error, 1 = errors or no CHRG. A .glb is checked\n");
	fprintf(out, "  in the vertex colour mode of the switch (gltf without it), a .rldchar in its own.\n");
	fprintf(out, "  rldpack hires-add <in.rldchar> <model.glb> -o <out.rldchar> [--vertex-colors gltf|psx|off]\n");
	fprintf(out, "                    [--kart-part | --game-kart <0..14|template>]\n");
	fprintf(out, "  Adds the model as CHRG (or replaces it). Refuses a model with an error, the\n");
	fprintf(out, "  budget included. Every other chunk stays byte for byte. --vertex-colors says\n");
	fprintf(out, "  how the game reads the model's vertex colours (COLOR_0): gltf (the default)\n");
	fprintf(out, "  white is neutral; psx grey 128 is neutral, texture x colour / 128 as on the\n");
	fprintf(out, "  PS1 (for PS1-style shading); off the colours are ignored.\n");
	fprintf(out, "  --kart-part: the subtree of the node 'kart' is the kart; the game leaves it\n");
	fprintf(out, "  and the wheels out on the podium, in the menu scene and the driver select\n");
	fprintf(out, "  (a 20-byte CHRG header; an older game reads past it and draws the kart).\n");
	fprintf(out, "  --game-kart <0..14|template>: the model has no kart; the game puts in the\n");
	fprintf(out, "  original kart from the player's own game data (that driver's colours).\n");
	fprintf(out, "  rldpack hires-strip <in.rldchar> -o <out.rldchar>\n");
	fprintf(out, "  Removes CHRG; every other chunk stays byte for byte.\n");
	fprintf(out, "  rldpack hires-upscale <in.glb> -o <out.glb>\n");
	fprintf(out, "  Writes a new model whose small base colour textures (larger edge below 128)\n");
	fprintf(out, "  are enlarged 2x or 4x with Scale2x, a pixel-art upscaler (no new colours,\n");
	fprintf(out, "  sharp edges). Everything else stays as it is; the source is only read.\n");
	fprintf(out, "  Pack the new model with hires-add.\n");
	fprintf(out, "  rldpack hires-png <in.glb> -o <out.glb>\n");
	fprintf(out, "  Writes a new model whose JPEG textures are PNG: each JPEG embedded in the\n");
	fprintf(out, "  model is decoded and written as a compressed RGBA PNG of exactly its pixels\n");
	fprintf(out, "  (the game reads PNG only, rule HRG-16), in place of the JPEG's bytes, which\n");
	fprintf(out, "  are gone. Every accessor reads the same values; the source is only read;\n");
	fprintf(out, "  a model without JPEG is written as it is. Run it before hires-upscale and\n");
	fprintf(out, "  hires-add.\n\n");
}
