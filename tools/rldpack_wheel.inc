// rldpack_wheel.inc - rldpack char-wheel: a wheel model of one's own, for the preview only
//
// Part of the packer, never of the game. tools/rldpack.c includes this file
// after tools/rldpack_char.inc and uses its PLY reader and its chain as they
// are (RldMk_ParsePly, RldMk_StepWeld ... RldMk_StepReduce); nothing there is
// changed for the wheel.
//
// WHAT IT DOES. A PLY of ONE wheel is read and repaired like a character
// (--repair auto, tools/rldpack_repair.inc), reduced to at most
// RLDWHEEL_TRIANGLES_MAX triangles (--reduce auto), scaled so that it is as
// large across as the wheel the game draws (2 x RLDDUM_TIRE_HALF_SIZE game
// units, tools/rldpack_dummy.inc) and centred on its axle. The only output is
// the --preview file for Reload Studio, which draws the wheel at the four
// wheel points of its reference dummy. char-wheel never writes a container
// and refuses --out: no part of a .rldchar comes from here yet.
//
// THE WHEEL'S AXES (one contract for the preview and, later, the game): the
// centre in the origin, the axle along X, the outer side (the rim) toward +X,
// +Y up, +Z forward - the axes of make-char (--up, --forward as there). The
// size across is the larger extent in Y and Z; a wheel wider in X than across,
// or not round about X (height and length differ by more than RLDWHEEL_ROUND),
// probably lies on another axle (wheel-off-axis). A wheel without extent in Y
// and Z (all on the axle) is refused before the chain (wheel-flat), and so is
// one of more than RLDWHEEL_TRIANGLES_IN_MAX triangles (wheel-big).
//
// THE --preview FILE ("RLDPW1"), little endian:
//   "RLDPW1\0\0"               8 bytes
//   u32 triangles              1 or more
//   per triangle 3 corners of
//     s16 x, s16 y, s16 z      1/16 game units (RLDWHEEL_PREVIEW_UNIT), the wheel's
//                              own axes above, centred on the axle
//     u8 r, u8 g, u8 b         the vertex color of the PLY
//     u8 pad                   bit 0 = 1: drawn from both sides (an open part),
//                              the same in all three corners
// Nothing follows the last triangle. The corners of a triangle run
// counter-clockwise seen from the side that is drawn, as in the RLDPV1 file of
// make-char --preview. The finer unit keeps a wheel of 16 game units round.
//
// DETERMINISM as make-char: the same PLY gives the same bytes. The file is
// built twice and compared before it is written.

#define RLDWHEEL_TRIANGLES_MAX 128u   // per wheel: 4 x 128 x 28 bytes of draw memory per view, about a retail HI driver
#define RLDWHEEL_TRIANGLES_IN_MAX 2000u  // read at most: the reduction of more takes minutes (3000 -> 128: over 2 minutes)
#define RLDWHEEL_ROUND 0.8            // height and length within this ratio: round about X
#define RLDWHEEL_PREVIEW_UNIT 16.0    // the file's unit: 1/16 game unit
#define RLDWHEEL_HEAD_BYTES 12u
#define RLDWHEEL_CORNER_BYTES 10u
#define RLDWHEEL_ACROSS (2.0 * RLDDUM_TIRE_HALF_SIZE)   // the game's wheel, game units across

struct RldWheelOptions
{
	int upZ;        // --up z
	int backwards;  // --forward -z
	int repair;     // --repair auto
	int reduce;     // --reduce auto
};

struct RldWheelResult
{
	u32 vertices;          // of the PLY
	u32 faces;
	u32 triangles;         // in the file
	u32 dropped;           // triangles that lost their area in the file's unit
	u32 twoSided;          // triangles of open parts, drawn from both sides
	int fitted;            // the factor below was found (a probe of the PLY got through)
	double factor;         // 4 significant digits, as --fit crash
	double acrossBefore;   // across (game units) at scale 1
	double across;         // across and wide after the factor and the chain
	double width;
	int reduced;
	u32 reduceFrom;
	int repaired;
	u32 repair[10];        // as struct RldMkBuild
	u8 *preview;           // the RLDPW1 file, malloc'ed
	size_t previewSize;
};

static void RldWheel_FreeResult(struct RldWheelResult *r)
{
	free(r->preview);
	memset(r, 0, sizeof(*r));
}

// The factor that brings the wheel to RLDWHEEL_ACROSS: a silent probe of steps
// 2 and 3 at scale 1 (RldMk_FitProbe). 0 = the probe did not get through or
// the wheel has no extent across; the real pass then says why.
static int RldWheel_Fit(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldWheelResult *res, double *scale)
{
	struct RldMkOptions at = *opt;
	struct RldMkBuild probe;
	double lo[3];
	double hi[3];
	double across;
	int ok;

	at.scale = 1.0;
	ok = RldMk_FitProbe(ply, &at, &probe, 0);
	if (ok)
	{
		RldMk_Bounds(&probe, -1, lo, hi);
	}
	RldMk_FreeBuild(&probe);
	if (!ok)
	{
		return 0;
	}

	across = ((hi[1] - lo[1]) > (hi[2] - lo[2])) ? (hi[1] - lo[1]) : (hi[2] - lo[2]);
	res->acrossBefore = across;
	if (!(across > 0.0))
	{
		return 0;
	}
	res->fitted = 1;
	res->factor = RldMk_FitRound(RLDWHEEL_ACROSS / across);
	*scale = res->factor;
	return 1;
}

// A position in the file's unit, kept in 16 bits.
static int RldWheel_Unit(double value)
{
	s64 unit = RldMk_Round(value * RLDWHEEL_PREVIEW_UNIT);

	unit = (unit < -32768) ? -32768 : ((unit > 32767) ? 32767 : unit);
	return (int)unit;
}

// The RLDPW1 bytes of a built wheel, shifted by `shift`. NULL = out of memory.
static u8 *RldWheel_Preview(const struct RldMkPly *ply, const struct RldMkBuild *b, const double shift[3], struct RldWheelResult *res)
{
	const size_t size = RLDWHEEL_HEAD_BYTES + (size_t)b->triangleCount * 3u * RLDWHEEL_CORNER_BYTES;
	u8 *out = (u8 *)calloc(1, size);
	size_t at = RLDWHEEL_HEAD_BYTES;
	u32 count = 0;
	u32 t;

	if (out == NULL)
	{
		return NULL;
	}
	memcpy(out, "RLDPW1\0\0", 8u);

	res->dropped = 0;
	res->twoSided = 0;
	for (t = 0; t < b->triangleCount; t++)
	{
		const int open = (b->triangleOpen != NULL) && (b->triangleOpen[t] != 0u);
		int v[3][3];
		int k;
		int a;

		for (k = 0; k < 3; k++)
		{
			const u32 p = b->vertexPosition[b->triangle[3u * t + (u32)k]];

			for (a = 0; a < 3; a++)
			{
				v[k][a] = RldWheel_Unit(b->position[3u * p + (u32)a] + shift[a]);
			}
		}

		// Two corners on one point of the file: the triangle has no area there.
		if ((memcmp(v[0], v[1], sizeof(v[0])) == 0) || (memcmp(v[1], v[2], sizeof(v[1])) == 0) || (memcmp(v[0], v[2], sizeof(v[0])) == 0))
		{
			res->dropped++;
			continue;
		}

		for (k = 0; k < 3; k++)
		{
			const u32 vertex = b->triangle[3u * t + (u32)k];
			const u8 *rgb = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * vertex] : &ply->rgb[3u * vertex];

			for (a = 0; a < 3; a++)
			{
				Rld_WriteLE16(&out[at + 2u * (u32)a], (u32)v[k][a] & 0xFFFFu);
			}
			out[at + 6u] = rgb[0];
			out[at + 7u] = rgb[1];
			out[at + 8u] = rgb[2];
			out[at + 9u] = (u8)(open ? 1u : 0u);
			at += RLDWHEEL_CORNER_BYTES;
		}
		res->twoSided += open ? 1u : 0u;
		count++;
	}

	Rld_WriteLE32(&out[8], count);
	res->triangles = count;
	res->previewSize = at;
	return out;
}

// The chain on a PLY in memory, once: read, weld, triangles, repair, one rigid
// part, reduce, fit, centre, the file. 1 = the wheel is usable; every reason
// against it was said (RldMk_Say). Nothing is read from or written to disk.
static int RldWheel_BuildOnce(const u8 *data, size_t size, const struct RldWheelOptions *wo, struct RldWheelResult *res)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	double lo[3];
	double hi[3];
	double shift[3];
	u32 keyCount = 0;
	const int errors = s_rldMkErrors;
	int ok = 0;
	int a;

	memset(res, 0, sizeof(*res));
	memset(&ply, 0, sizeof(ply));
	memset(&b, 0, sizeof(b));
	if (!RldMk_ParsePly(data, size, &ply))
	{
		return 0;
	}
	res->vertices = ply.vertexCount;
	res->faces = ply.faceCount;

	RldMk_DefaultOptions(&opt);
	opt.upZ = wo->upZ;
	opt.backwards = wo->backwards;
	opt.repair = wo->repair;
	opt.reduce = wo->reduce;
	opt.fit = 0;
	opt.still = 1;
	opt.reduceTarget = RLDWHEEL_TRIANGLES_MAX;
	opt.reduceForce = 1;
	opt.reduceWhy = "a wheel may have at most 128 triangles";
	// All corners on the axle: nothing to scale, and the chain would only find
	// triangles without area. Checked on the PLY's corners in the game's axes.
	{
		double c[3];
		u32 v;

		for (a = 0; a < 3; a++)
		{
			lo[a] = 1.0e30;
			hi[a] = -1.0e30;
		}
		for (v = 0; v < ply.vertexCount; v++)
		{
			RldMk_Axes(&opt, &ply.xyz[3u * v], c);
			for (a = 0; a < 3; a++)
			{
				lo[a] = (c[a] < lo[a]) ? c[a] : lo[a];
				hi[a] = (c[a] > hi[a]) ? c[a] : hi[a];
			}
		}
		if ((ply.vertexCount == 0u) || (!((hi[1] - lo[1]) > 1.0e-6) && !((hi[2] - lo[2]) > 1.0e-6)))
		{
			RldMk_Say("error", "wheel-flat", NULL,
			          "The wheel has no height and no length - all its corners lie on one line. Export the whole wheel, round about its axle.");
			goto done;
		}
	}
	(void)RldWheel_Fit(&ply, &opt, res, &opt.scale);

	b.vertexCount = ply.vertexCount;
	b.faceCount = ply.faceCount;
	b.kartPart = -1;
	b.driverPart = -1;
	if (!RldMk_StepWeld(&ply, &opt, &b, &keyCount) || !RldMk_StepTriangles(&ply, &b, keyCount))
	{
		goto done;
	}
	if (b.triangleCount > RLDWHEEL_TRIANGLES_IN_MAX)
	{
		RldMk_Say("error", "wheel-big", NULL,
		          "The wheel has %u triangles; char-wheel takes at most %u and reduces them to %u. Simplify the wheel in your 3D program first (in Blender: Decimate modifier).",
		          b.triangleCount, RLDWHEEL_TRIANGLES_IN_MAX, RLDWHEEL_TRIANGLES_MAX);
		goto done;
	}
	RldMk_StepOpenEdges(&b);
	if (!RldMk_StepRepair(&ply, &b, &opt) || !RldMk_StepOpenParts(&b, &opt) || !RldMk_StepMaskPart(&b) || !RldMk_StepReduce(&ply, &b, &opt))
	{
		goto done;
	}
	res->repaired = b.repaired;
	memcpy(res->repair, b.repair, sizeof(res->repair));
	res->reduced = b.reduced;
	res->reduceFrom = b.reduceFrom;

	if (!res->fitted)
	{
		RldMk_Say("error", "wheel-flat", NULL, "The wheel has no height and no length - all its corners lie on one line. Export the whole wheel, round about its axle.");
		goto done;
	}

	// the centre of its box onto the axle
	RldMk_Bounds(&b, -1, lo, hi);
	for (a = 0; a < 3; a++)
	{
		shift[a] = -0.5 * (lo[a] + hi[a]);
	}
	res->across = ((hi[1] - lo[1]) > (hi[2] - lo[2])) ? (hi[1] - lo[1]) : (hi[2] - lo[2]);
	res->width = hi[0] - lo[0];

	if (b.triangleCount > RLDWHEEL_TRIANGLES_MAX)
	{
		RldMk_Say("error", "wheel-tris", NULL,
		          "The wheel has %u triangles; a wheel may have at most %u (all four together about as many as a driver of the game). Reduce it in Blender (Decimate modifier).",
		          b.triangleCount, RLDWHEEL_TRIANGLES_MAX);
		goto done;
	}
	if (res->width > res->across)
	{
		RldMk_Say("warning", "wheel-off-axis", NULL,
		          "The wheel is %.1f wide but only %.1f across: its axle is probably not the X axis. Turn it in Blender so that the axle runs from left to right.",
		          res->width, res->across);
	}
	else if (((hi[1] - lo[1]) < (RLDWHEEL_ROUND * (hi[2] - lo[2]))) || ((hi[2] - lo[2]) < (RLDWHEEL_ROUND * (hi[1] - lo[1]))))
	{
		RldMk_Say("warning", "wheel-off-axis", NULL,
		          "The wheel is %.1f high but %.1f long: it is not round about the X axis, so its axle is probably another one. Turn it in Blender so that the axle runs from left to right.",
		          hi[1] - lo[1], hi[2] - lo[2]);
	}

	res->preview = RldWheel_Preview(&ply, &b, shift, res);
	if (res->preview == NULL)
	{
		RldMk_OutOfMemory();
		goto done;
	}
	if (res->triangles == 0u)
	{
		RldMk_Say("error", "ply-no-faces", NULL, "The wheel has no triangles left. Export the mesh, not only its points.");
		goto done;
	}
	ok = (s_rldMkErrors == errors);

done:
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	return ok;
}

// The chain twice; the second build silent. 1 = usable and both builds the same.
static int RldWheel_Build(const u8 *data, size_t size, const struct RldWheelOptions *wo, struct RldWheelResult *res)
{
	struct RldWheelResult again;
	const int quiet = s_rldMkQuiet;
	const int errors = s_rldMkErrors;
	int same;

	if (!RldWheel_BuildOnce(data, size, wo, res))
	{
		return 0;
	}

	s_rldMkQuiet = 1;
	same = RldWheel_BuildOnce(data, size, wo, &again) && (again.previewSize == res->previewSize) &&
	       (memcmp(again.preview, res->preview, res->previewSize) == 0);
	s_rldMkQuiet = quiet;
	s_rldMkErrors = errors;
	RldWheel_FreeResult(&again);
	if (!same)
	{
		RldMk_Say("error", "model-roundtrip", "two builds of the same wheel differ", "Internal error: two builds of the same wheel differ. Please report it.");
		return 0;
	}
	return 1;
}

// A switch of two words (--up, --repair, --reduce): *out = 1 for `yes`, 0 for
// `no`; anything else is said with `code` and `text`.
static void RldWheel_Word(const char *arg, const char *yes, const char *no, int *out, const char *code, const char *text)
{
	char word[16];
	const int known = RldMk_Lower(arg, word, sizeof(word));

	if (known && (strcmp(word, yes) == 0))
	{
		*out = 1;
	}
	else if (known && (strcmp(word, no) == 0))
	{
		*out = 0;
	}
	else
	{
		RldMk_Say("error", code, arg, "%s", text);
	}
}

static int RldWheel_Command(int argc, char **argv)
{
	const char *modelArg = NULL;
	const char *previewArg = NULL;
	const char *upArg = NULL;
	const char *forwardArg = NULL;
	const char *repairArg = NULL;
	const char *reduceArg = NULL;
	struct RldWheelOptions wo;
	struct RldWheelResult res;
	u8 *data = NULL;
	size_t size = 0;
	char number[4][32];
	int built;
	int i;

	memset(&wo, 0, sizeof(wo));
	memset(&res, 0, sizeof(res));
	wo.repair = 1;
	wo.reduce = 1;
	RldMk_ResetSaid();

	for (i = 0; i < argc; i++)
	{
		const char *arg = argv[i];
		const int hasValue = ((i + 1) < argc);

		if ((strcmp(arg, "--model") == 0) && hasValue)
		{
			modelArg = argv[++i];
		}
		else if ((strcmp(arg, "--preview") == 0) && hasValue)
		{
			previewArg = argv[++i];
		}
		else if ((strcmp(arg, "--up") == 0) && hasValue)
		{
			upArg = argv[++i];
		}
		else if ((strcmp(arg, "--forward") == 0) && hasValue)
		{
			forwardArg = argv[++i];
		}
		else if ((strcmp(arg, "--repair") == 0) && hasValue)
		{
			repairArg = argv[++i];
		}
		else if ((strcmp(arg, "--reduce") == 0) && hasValue)
		{
			reduceArg = argv[++i];
		}
		else if (strcmp(arg, "--out") == 0)
		{
			RldMk_Say("error", "usage", arg, "char-wheel writes no container: own wheels cannot be packed yet. It only writes --preview <file>.");
			return 2;
		}
		else
		{
			RldMk_Say("error", "usage", arg, "rldpack does not know the argument \"%s\".", arg);
			if (!s_rldMkQuiet)
			{
				fprintf(stderr, "\n");
				RldWheel_Usage(stderr);
			}
			return 2;
		}
	}

	if (modelArg == NULL)
	{
		RldMk_Say("error", "usage", NULL, "No wheel model was given (--model <ply>).");
		if (!s_rldMkQuiet)
		{
			fprintf(stderr, "\n");
			RldWheel_Usage(stderr);
		}
		return 2;
	}

	// A wrong value is said and the other checks go on, as with make-char.
	if (upArg != NULL)
	{
		RldWheel_Word(upArg, "z", "y", &wo.upZ, "up", "The up axis must be y or z.");
	}
	if ((forwardArg != NULL) && (strcmp(forwardArg, "z") != 0) && (strcmp(forwardArg, "+z") != 0))
	{
		if (strcmp(forwardArg, "-z") == 0)
		{
			wo.backwards = 1;
		}
		else
		{
			RldMk_Say("error", "forward", forwardArg, "Forward must be z or -z.");
		}
	}
	if (repairArg != NULL)
	{
		RldWheel_Word(repairArg, "auto", "off", &wo.repair, "repair", "Repair must be auto or off.");
	}
	if (reduceArg != NULL)
	{
		RldWheel_Word(reduceArg, "auto", "off", &wo.reduce, "reduce", "Reduce must be auto or off.");
	}

	Rld_Emit("value", "model", modelArg, "switch", (const char *)NULL);
	Rld_Emit("value", "up", wo.upZ ? "z" : "y", (upArg != NULL) ? "switch" : "default", (const char *)NULL);
	Rld_Emit("value", "forward", wo.backwards ? "-z" : "z", (forwardArg != NULL) ? "switch" : "default", (const char *)NULL);
	Rld_Emit("value", "repair", wo.repair ? "auto" : "off", (repairArg != NULL) ? "switch" : "default", (const char *)NULL);
	Rld_Emit("value", "reduce", wo.reduce ? "auto" : "off", (reduceArg != NULL) ? "switch" : "default", (const char *)NULL);
	Rld_Emit("value", "preview", (previewArg != NULL) ? previewArg : "", (previewArg != NULL) ? "switch" : "default", (const char *)NULL);
	if (s_rldMkErrors != 0)
	{
		return 2;
	}

	if (!Rld_ReadFile(modelArg, &data, &size))
	{
		Rld_Emit("file", "ply", "missing", modelArg, "0", (const char *)NULL);
		RldMk_Say("error", "ply-open", modelArg, "The wheel model file %s cannot be opened.", modelArg);
		return 1;
	}
	snprintf(number[0], sizeof(number[0]), "%u", (u32)size);
	Rld_Emit("file", "ply", "ok", modelArg, number[0], (const char *)NULL);
	if (!s_rldMkQuiet)
	{
		printf("rldpack char-wheel %s\n", modelArg);
	}

	// PLY only (tools/rldpack_import.inc: an OBJ is model-ply-only).
	if (!RldMk_ModelAllowed(modelArg, data, size, RLDMK_ROLE_WHEEL))
	{
		free(data);
		return 1;
	}

	built = RldWheel_Build(data, size, &wo, &res);
	free(data);

	// The facts for Reload Studio (@char), as far as the chain came.
	if (s_machine && !s_rldMkQuiet)
	{
		if (res.repaired)
		{
			char text[10][16];
			int k;

			for (k = 0; k < 10; k++)
			{
				snprintf(text[k], sizeof(text[k]), "%u", res.repair[k]);
			}
			Rld_Emit("char", "repaired", text[0], text[1], text[2], text[3], text[4], text[5], text[6], text[7], text[8], text[9], (const char *)NULL);
		}
		if (res.fitted)
		{
			snprintf(number[0], sizeof(number[0]), "%.4g", res.factor);
			snprintf(number[1], sizeof(number[1]), "%.1f", res.acrossBefore);
			snprintf(number[2], sizeof(number[2]), "%.1f", res.across);
			Rld_Emit("char", "fit", number[0], number[1], number[2], (const char *)NULL);
		}
		if (res.reduced)
		{
			snprintf(number[0], sizeof(number[0]), "%u", res.reduceFrom);
			snprintf(number[1], sizeof(number[1]), "%u", res.triangles + res.dropped);
			Rld_Emit("char", "reduced", number[0], number[1], (const char *)NULL);
		}
	}
	if (!s_rldMkQuiet)
	{
		printf("  read          %u vertices, %u faces\n", res.vertices, res.faces);
	}

	if (!built)
	{
		RldWheel_FreeResult(&res);
		return 1;
	}

	snprintf(number[0], sizeof(number[0]), "%u", res.triangles);
	snprintf(number[1], sizeof(number[1]), "%.1f", res.across);
	snprintf(number[2], sizeof(number[2]), "%.1f", res.width);
	snprintf(number[3], sizeof(number[3]), "%u", res.twoSided);
	if (s_machine && !s_rldMkQuiet)
	{
		Rld_Emit("char", "wheel", number[0], number[1], number[2], number[3], (const char *)NULL);
		Rld_Emit("value", "wheel-size", number[1], "built", (const char *)NULL);
	}
	if (!s_rldMkQuiet)
	{
		printf("  wheel         %u triangles (%u drawn from both sides), %.1f across, %.1f wide in game units\n", res.triangles, res.twoSided, res.across,
		       res.width);
		printf("                x%.4g: %.1f -> %.1f across (the game's wheel: %.0f); centred on the axle; built twice\n", res.factor, res.acrossBefore,
		       res.across, RLDWHEEL_ACROSS);
	}

	if (previewArg != NULL)
	{
		FILE *file = fopen(previewArg, "wb");
		const int openError = (file == NULL) ? errno : 0;

		if ((file == NULL) || (fwrite(res.preview, 1, res.previewSize, file) != res.previewSize) || (fclose(file) != 0))
		{
			RldMk_EmitFile("preview", "failed", previewArg, 0u);
			if (!RldMk_SayMissingFolder("error", "preview", previewArg, "The preview file", openError))
			{
				RldMk_Say("error", "preview", previewArg, "The preview file %s cannot be written. Is the folder write-protected?", previewArg);
			}
			RldWheel_FreeResult(&res);
			return 1;
		}
		RldMk_EmitFile("preview", "ok", previewArg, res.previewSize);
		if (!s_rldMkQuiet)
		{
			printf("  preview       %s, %u bytes\n", previewArg, (u32)res.previewSize);
		}
	}

	RldWheel_FreeResult(&res);
	return 0;
}

static void RldWheel_Usage(FILE *out)
{
	fprintf(out, "CHAR-WHEEL - A WHEEL OF ONE'S OWN, FOR THE PREVIEW ONLY\n");
	fprintf(out, "  rldpack char-wheel --model <wheel.ply> [--preview <file>] [switches]\n");
	fprintf(out, "  Reads a PLY of one wheel, repairs and reduces it as make-char does, scales\n");
	fprintf(out, "  it to the size of the game's wheel (%.0f game units across) and centres it\n", RLDWHEEL_ACROSS);
	fprintf(out, "  on its axle. The wheel: axle along X, outer side +X, +Y up, +Z forward;\n");
	fprintf(out, "  at most %u triangles (up to %u are reduced to it). It writes only the\n", RLDWHEEL_TRIANGLES_MAX,
	        RLDWHEEL_TRIANGLES_IN_MAX);
	fprintf(out, "  preview file for Reload Studio\n");
	fprintf(out, "  (RLDPW1, tools/rldpack_wheel.inc), never a container: --out is refused.\n");
	fprintf(out, "  --preview <file>            the wheel for the 3D preview of Reload Studio\n");
	fprintf(out, "  --up y|z, --forward z|-z    the axes of the PLY, as with make-char\n");
	fprintf(out, "  --repair auto|off           as with make-char (default auto)\n");
	fprintf(out, "  --reduce auto|off           more than %u triangles are reduced (default auto)\n", RLDWHEEL_TRIANGLES_MAX);
	fprintf(out, "\n");
	fprintf(out, "  machine lines (--machine): @value model, up, forward, repair, reduce,\n");
	fprintf(out, "  preview; @file ply; @msg; @char repaired (as make-char), fit <factor>\n");
	fprintf(out, "  <across before> <after>, reduced <before> <after>, wheel <triangles>\n");
	fprintf(out, "  <across> <wide> <two-sided>; @value wheel-size <across> built; @file\n");
	fprintf(out, "  preview; @end. No @result: nothing is built.\n");
	fprintf(out, "\n");
}

//========================================================================================
// SELF-TEST
//========================================================================================
//
// Every wheel is made here as PLY text (RldMk_TestPly); no file is read or
// written. The cases: size and centre, scale, the axes, the triangle limit
// with and without the reduction, a wheel too wide and one on the Y axle,
// a flat one, one too large to read, faces turned outward, two builds the
// same, and --out refused.

static int s_rldWheelTestFailed;

static void RldWheel_Test(int ok, const char *name, const char *format, ...)
{
	char text[320];
	va_list list;

	if (ok)
	{
		printf("  ok   char-wheel %s\n", name);
		return;
	}

	va_start(list, format);
	vsnprintf(text, sizeof(text), format, list);
	va_end(list);
	printf("  FAIL char-wheel %s: %s\n", name, text);
	s_rldWheelTestFailed++;
}

// A closed wheel: `segments` around the axle X, `radius` and half `width` in
// PLY units, its centre at c; quads on the tread, triangle fans on the sides,
// wound so that the right-hand normal points out. upZ: written with +Z up and
// the front along -Y (Blender's axes, RldMk_Axes), the axle still X.
static void RldWheel_TestWheel(struct RldMkTestMesh *mesh, u32 segments, double radius, double width, const double c[3], int upZ)
{
	u32 ring[2];
	u32 hub[2];
	u32 i;
	int side;

	memset(mesh, 0, sizeof(*mesh));
	for (side = 0; side < 2; side++)
	{
		const double x = c[0] + (side ? -width : width);

		ring[side] = mesh->vertexCount;
		for (i = 0; i < segments; i++)
		{
			double s;
			double co;

			RldMk_SinCos(360.0 * (double)i / (double)segments, &s, &co);
			(void)RldMk_TestVertex(mesh, x, upZ ? (c[1] - radius * s) : (c[1] + radius * co), upZ ? (c[2] + radius * co) : (c[2] + radius * s),
			                       (u32)side * 64u + (i % 8u));
		}
	}
	for (side = 0; side < 2; side++)
	{
		hub[side] = RldMk_TestVertex(mesh, c[0] + (side ? -width : width), c[1], c[2], 200u + (u32)side);
	}

	for (i = 0; i < segments; i++)
	{
		const u32 j = (i + 1u) % segments;

		RldMk_TestFace(mesh, 4u, ring[0] + i, ring[1] + i, ring[1] + j, ring[0] + j, 0u);
		RldMk_TestFace(mesh, 3u, hub[0], ring[0] + i, ring[0] + j, 0u, 0u);
		RldMk_TestFace(mesh, 3u, hub[1], ring[1] + j, ring[1] + i, 0u, 0u);
	}
}

// Build a test wheel; 1 = built. res is freed by the caller.
static int RldWheel_TestBuild(const struct RldMkTestMesh *mesh, const struct RldWheelOptions *wo, struct RldWheelResult *res)
{
	size_t size = 0;
	u8 *data = RldMk_TestPly(mesh, 0, 1, 0, &size);
	int ok;

	memset(res, 0, sizeof(*res));
	RldMk_ResetSaid();
	if (data == NULL)
	{
		return 0;
	}
	ok = RldWheel_Build(data, size, wo, res);
	free(data);
	return ok;
}

// The box of the file's corners (x0 y0 z0 x1 y1 z1, 1/16 game units), and the
// triangles whose normal points toward the centre (inward).
static void RldWheel_TestMeasure(const struct RldWheelResult *res, int box[6], u32 *inward)
{
	const u32 count = Rld_ReadLE32(&res->preview[8]);
	u32 t;
	int a;

	for (a = 0; a < 3; a++)
	{
		box[a] = 32767;
		box[3 + a] = -32768;
	}
	*inward = 0;
	for (t = 0; t < count; t++)
	{
		double v[3][3];
		double n[3];
		double mid[3];
		int k;

		for (k = 0; k < 3; k++)
		{
			for (a = 0; a < 3; a++)
			{
				const int unit = (int)(s16)Rld_ReadLE16(&res->preview[RLDWHEEL_HEAD_BYTES + (3u * t + (u32)k) * RLDWHEEL_CORNER_BYTES + 2u * (u32)a]);

				v[k][a] = (double)unit;
				box[a] = (unit < box[a]) ? unit : box[a];
				box[3 + a] = (unit > box[3 + a]) ? unit : box[3 + a];
			}
		}
		RldMk_Cross(v[0], v[1], v[2], n);
		for (a = 0; a < 3; a++)
		{
			mid[a] = (v[0][a] + v[1][a] + v[2][a]) / 3.0;
		}
		if (RldMk_Dot(n, mid) < 0.0)
		{
			(*inward)++;
		}
	}
}

static int RldWheel_SelfTest(void)
{
	struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	static const double origin[3] = {0.0, 0.0, 0.0};
	static const double away[3] = {1.0, 2.0, -3.0};
	const int quietBefore = s_rldMkQuiet;
	struct RldWheelOptions wo;
	struct RldWheelResult res;
	struct RldWheelResult again;
	int box[6] = {0, 0, 0, 0, 0, 0};
	u32 inward = 0;
	int ok;

	s_rldWheelTestFailed = 0;
	if (mesh == NULL)
	{
		printf("  FAIL char-wheel: out of memory\n");
		return 1;
	}
	s_rldMkQuiet = 1;
	memset(&wo, 0, sizeof(wo));
	wo.repair = 1;
	wo.reduce = 1;

	// 1. The game's size: radius 0.25 = 16 game units (256 in the file), 10 wide;
	// 16 segments = 64 triangles.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	if (ok)
	{
		RldWheel_TestMeasure(&res, box, &inward);
	}
	RldWheel_Test(ok && (res.triangles == 64u) && (res.factor == 1.0) && (box[1] == -256) && (box[4] == 256) && (box[2] == -256) && (box[5] == 256) &&
	                  (box[0] == -80) && (box[3] == 80) && (inward == 0u) && (memcmp(res.preview, "RLDPW1\0\0", 8u) == 0) &&
	                  (res.previewSize == RLDWHEEL_HEAD_BYTES + 64u * 3u * RLDWHEEL_CORNER_BYTES),
	              "game size", "built %d, %u triangles, x%.4g, box %d %d %d %d %d %d, %u inward", ok, res.triangles, res.factor, box[0], box[1], box[2],
	              box[3], box[4], box[5], inward);
	RldWheel_FreeResult(&res);

	// 2. Four times as large and away from the origin: scaled down, centred.
	RldWheel_TestWheel(mesh, 16u, 1.0, 0.3125, away, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	if (ok)
	{
		RldWheel_TestMeasure(&res, box, &inward);
	}
	RldWheel_Test(ok && (res.factor == 0.25) && (box[1] == -256) && (box[4] == 256) && (box[0] == -80) && (box[3] == 80) && ((box[2] + box[5]) == 0) &&
	                  (inward == 0u),
	              "fit and centre", "built %d, x%.4g, box %d %d %d %d %d %d, %u inward", ok, res.factor, box[0], box[1], box[2], box[3], box[4], box[5],
	              inward);
	RldWheel_FreeResult(&res);

	// 3. Written with +Z up (--up z): the same wheel.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 1);
	wo.upZ = 1;
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	wo.upZ = 0;
	if (ok)
	{
		RldWheel_TestMeasure(&res, box, &inward);
	}
	RldWheel_Test(ok && (box[0] == -80) && (box[3] == 80) && (box[1] == -256) && (box[4] == 256) && (inward == 0u), "up z",
	              "built %d, box %d %d %d %d %d %d, %u inward", ok, box[0], box[1], box[2], box[3], box[4], box[5], inward);
	RldWheel_FreeResult(&res);

	// 4. 48 segments = 192 triangles: reduced to the limit, refused without the reduction.
	RldWheel_TestWheel(mesh, 48u, 0.25, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(ok && res.reduced && (res.reduceFrom == 192u) && (res.triangles <= RLDWHEEL_TRIANGLES_MAX) && RldMk_Said("model-reduced"), "reduced",
	              "built %d, reduced %d, %u -> %u", ok, res.reduced, res.reduceFrom, res.triangles);
	RldWheel_FreeResult(&res);
	wo.reduce = 0;
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	wo.reduce = 1;
	RldWheel_Test(!ok && RldMk_Said("wheel-tris"), "limit", "built %d, wheel-tris said %d", ok, RldMk_Said("wheel-tris"));
	RldWheel_FreeResult(&res);

	// 5. Four times wider than across: a warning, still built.
	RldWheel_TestWheel(mesh, 16u, 0.25, 1.0, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(ok && RldMk_Said("wheel-off-axis"), "off axis", "built %d, wheel-off-axis said %d", ok, RldMk_Said("wheel-off-axis"));
	RldWheel_FreeResult(&res);

	// 5b. The axle along Y (turned by 90 degrees about Z): as wide as across, but not round about X.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 0);
	{
		u32 v;

		for (v = 0; v < mesh->vertexCount; v++)
		{
			const double x = mesh->xyz[v][0];

			mesh->xyz[v][0] = -mesh->xyz[v][1];
			mesh->xyz[v][1] = x;
		}
	}
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(ok && RldMk_Said("wheel-off-axis"), "axle along Y", "built %d, wheel-off-axis said %d", ok, RldMk_Said("wheel-off-axis"));
	RldWheel_FreeResult(&res);

	// 5c. All corners on the axle (radius 0): wheel-flat before the chain, not "no faces".
	RldWheel_TestWheel(mesh, 16u, 0.0, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-flat") && !RldMk_Said("ply-no-faces"), "flat", "built %d, wheel-flat said %d, ply-no-faces said %d", ok,
	              RldMk_Said("wheel-flat"), RldMk_Said("ply-no-faces"));
	RldWheel_FreeResult(&res);

	// 5d. 1000 segments = 4000 triangles: refused before the reduction.
	RldWheel_TestWheel(mesh, 1000u, 0.25, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-big") && !RldMk_Said("model-reduced"), "too large", "built %d, wheel-big said %d", ok,
	              RldMk_Said("wheel-big"));
	RldWheel_FreeResult(&res);

	// 6. Every face wound the other way round: the repair turns them outward.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 0);
	{
		u32 f;

		for (f = 0; f < mesh->faceCount; f++)
		{
			const u32 first = mesh->corners[f][0];

			mesh->corners[f][0] = mesh->corners[f][1];
			mesh->corners[f][1] = first;
		}
	}
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	if (ok)
	{
		RldWheel_TestMeasure(&res, box, &inward);
	}
	RldWheel_Test(ok && (inward == 0u) && res.repaired && (res.repair[3] > 0u), "turned outward", "built %d, %u inward, %u turned", ok, inward,
	              res.repair[3]);
	RldWheel_FreeResult(&res);

	// 7. Two builds, the same bytes.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, away, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	ok = RldWheel_TestBuild(mesh, &wo, &again) && ok;
	RldWheel_Test(ok && (res.previewSize == again.previewSize) && (memcmp(res.preview, again.preview, res.previewSize) == 0), "same bytes",
	              "built %d, %u / %u bytes", ok, (u32)res.previewSize, (u32)again.previewSize);
	RldWheel_FreeResult(&res);
	RldWheel_FreeResult(&again);

	// 8. --out is refused before anything is read.
	{
		char *args[5];
		int code;

		args[0] = (char *)"--model";
		args[1] = (char *)"wheel.ply";
		args[2] = (char *)"--out";
		args[3] = (char *)"wheel.rldchar";
		args[4] = NULL;
		code = RldWheel_Command(4, args);
		RldWheel_Test((code == 2) && RldMk_Said("usage"), "no container", "exit code %d, usage said %d", code, RldMk_Said("usage"));
	}

	s_rldMkQuiet = quietBefore;
	RldMk_ResetSaid();
	free(mesh);
	return s_rldWheelTestFailed;
}
