// rldpack_native_test.inc - the native model (CNET, CTXT): test set, self-test
// and rldpack make-native-tests; the self-test of the native model of an OBJ
// (make-char --native-model on, RldMk_SelfTestNativeObj)
//
// Part of the packer's self-test (tools/rldpack_char.inc includes it before
// RldChar_SelfTest). ONE SET of cases, made in memory from the self-test's
// standard model (the one of the golden hash "make-char --fit none"):
//
//   old_      the container without CNET and CTXT - the golden bytes of today
//   none_     CTXT without CNET: nothing native
//   good_     the probe body still, with 47 poses, without wheels (CHRI
//             hides them), and without a texture
//   bad_<RULE>_   one change that breaks exactly <RULE>; the driver keeps CMDL
//   damaged_  a generic envelope finding (outside the data area, a type
//             twice, two chunks overlapping): the whole file is refused, as
//             for any chunk
//
// The self-test checks the chunk cases in memory with the game's rules
// (RldMk_CheckNativeBytes) and the golden hashes; rldpack make-native-tests
// <folder> writes the whole set as .rldchar files, and the game reads them
// with --char-native-selftest <folder> (ctest char_native_selftest), the
// envelope cases included. The folder must lie in a CMake build folder: the
// files never go into the source tree (and the content guard refuses a
// .rldchar anyway).

// The container with --native-probe still of the standard model (CHRI, CMDL,
// CNET, CTXT), and the probe texture as the game makes it
// (NATIVE_PROBE_TEXTURE_GOLDEN, platform/native_probe.c).
#define RLDMK_GOLDEN_NATIVE_STILL "d1e570d8cc6c0c0ef5f4e2a9071ad24072631d6fc3e6c4c1b0e99a4a2da1906a"
#define RLDMK_GOLDEN_NATIVE_POSES "91a6634abbde504c9b8f3586abbfa55c78bf10ec50a8c78b1f81607f6533bc1f"
#define RLDMK_GOLDEN_NATIVE_TEXTURE "91a84b2163c3ec26e128e4d17ebc15b063b496055f830afc28341c20252cea4f"

#define RLDMK_NATIVE_CASES_MAX 40

struct RldMkNativeCase
{
	char name[64];
	u8 *file;                   // the container
	size_t size;
	const u8 *cnet;             // inside file, NULL for none
	size_t cnetSize;
	const u8 *ctxt;
	size_t ctxtSize;
	u32 chriFlags;
	int envelope;               // 1: the case is about the directory, not the chunk bytes
};

struct RldMkNativeSet
{
	struct RldMkNativeCase item[RLDMK_NATIVE_CASES_MAX];
	int count;
	u8 *cmdl;                   // the standard model
	size_t cmdlSize;
	int failed;
};

// Where a chunk of a container lies (its directory entry), NULL when absent.
static u8 *RldMk_NativeEntry(u8 *file, const char type[4])
{
	const u64 dirAt = Rld_ReadLE64(&file[0x18]);
	const u32 count = Rld_ReadLE32(&file[0x10]);
	u32 i;

	for (i = 0; i < count; i++)
	{
		u8 *entry = &file[(size_t)dirAt + ((size_t)i * RLD_DIR_ENTRY_SIZE)];

		if (memcmp(entry, type, 4u) == 0)
		{
			return entry;
		}
	}
	return NULL;
}

// One case: CHRI (flags), CMDL, then cnet and ctxt as given (NULL = none);
// a copy is made, the caller keeps its buffers.
static void RldMk_NativeAdd(struct RldMkNativeSet *set, const char *name, u32 chriFlags, const u8 *cnet, size_t cnetSize, const u8 *ctxt, size_t ctxtSize)
{
	struct RldMkNativeCase *c;
	u8 field[RLDCHAR_NAME_FIELD];
	size_t chriSize = 0;
	u8 *chri;
	u8 *entry;

	if (set->count >= RLDMK_NATIVE_CASES_MAX)
	{
		set->failed = 1;
		return;
	}
	c = &set->item[set->count];
	memset(c, 0, sizeof(*c));
	snprintf(c->name, sizeof(c->name), "%s", name);
	c->chriFlags = chriFlags;

	memset(field, 0, sizeof(field));
	memcpy(field, "SELF TEST", 9u);
	chri = RldMk_BuildChri(14u, 0u, field, 1u, "", chriFlags, &chriSize);
	if (chri == NULL)
	{
		set->failed = 1;
		return;
	}
	c->file = RldMk_ContainerNative(chri, chriSize, set->cmdl, set->cmdlSize, NULL, NULL, 0u, NULL, 0u, cnet, cnetSize, ctxt, ctxtSize, &c->size);
	free(chri);
	if (c->file == NULL)
	{
		set->failed = 1;
		return;
	}
	if ((entry = RldMk_NativeEntry(c->file, "CNET")) != NULL)
	{
		c->cnet = &c->file[(size_t)Rld_ReadLE64(&entry[0x08])];
		c->cnetSize = (size_t)Rld_ReadLE64(&entry[0x10]);
	}
	if ((entry = RldMk_NativeEntry(c->file, "CTXT")) != NULL)
	{
		c->ctxt = &c->file[(size_t)Rld_ReadLE64(&entry[0x08])];
		c->ctxtSize = (size_t)Rld_ReadLE64(&entry[0x10]);
	}
	set->count++;
}

static void RldMk_NativeFreeSet(struct RldMkNativeSet *set)
{
	int i;

	for (i = 0; i < set->count; i++)
	{
		free(set->item[i].file);
	}
	free(set->cmdl);
	memset(set, 0, sizeof(*set));
}

// A copy of bytes to break.
static u8 *RldMk_NativeCopy(const u8 *bytes, size_t size)
{
	u8 *copy = (u8 *)malloc(size ? size : 1u);

	if (copy != NULL)
	{
		memcpy(copy, bytes, size);
	}
	return copy;
}

// The offset of a CNET section, by tag (0 = none).
static u32 RldMk_NativeSectionAt(const u8 *cnet, const char tag[4], u32 *entryAt)
{
	const u32 table = Rld_ReadLE16(&cnet[0x04]);
	const u32 count = Rld_ReadLE16(&cnet[0x06]);
	u32 s;

	for (s = 0; s < count; s++)
	{
		const u32 at = table + (s * RLDCHAR_NET_SECTION_BYTES);

		if (memcmp(&cnet[at], tag, 4u) == 0)
		{
			if (entryAt != NULL)
			{
				*entryAt = at;
			}
			return Rld_ReadLE32(&cnet[at + 8u]);
		}
	}
	return 0;
}

// The whole set. 0 = out of memory or the standard model cannot be built.
static int RldMk_NativeMakeSet(struct RldMkNativeSet *set)
{
	struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	const char *why = NULL;
	u8 *still = NULL;
	u8 *poses = NULL;
	u8 *bare = NULL;
	u8 *ctxt = NULL;
	size_t stillSize = 0;
	size_t posesSize = 0;
	size_t bareSize = 0;
	size_t ctxtSize = 0;
	int ok;

	memset(set, 0, sizeof(*set));
	if (mesh == NULL)
	{
		return 0;
	}

	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	free(mesh);
	if (ok)
	{
		set->cmdl = RldMk_NativeCopy(b.cmdl, b.cmdlSize);
		set->cmdlSize = b.cmdlSize;
	}
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	if (!ok || (set->cmdl == NULL))
	{
		return 0;
	}

	still = RldMk_NativeCnet(set->cmdl, set->cmdlSize, 0, 1, &stillSize, &why);
	poses = RldMk_NativeCnet(set->cmdl, set->cmdlSize, 1, 1, &posesSize, &why);
	bare = RldMk_NativeCnet(set->cmdl, set->cmdlSize, 0, 0, &bareSize, &why);
	ctxt = RldMk_NativeCtxt(&ctxtSize);
	if ((still == NULL) || (poses == NULL) || (bare == NULL) || (ctxt == NULL))
	{
		free(still);
		free(poses);
		free(bare);
		free(ctxt);
		return 0;
	}

	RldMk_NativeAdd(set, "old_plain", 0u, NULL, 0u, NULL, 0u);
	RldMk_NativeAdd(set, "none_ctxt-without-cnet", 0u, NULL, 0u, ctxt, ctxtSize);
	RldMk_NativeAdd(set, "good_probe-still", 0u, still, stillSize, ctxt, ctxtSize);
	RldMk_NativeAdd(set, "good_probe-poses", 0u, poses, posesSize, ctxt, ctxtSize);
	RldMk_NativeAdd(set, "good_probe-wheels-hidden", RLDCHAR_FLAG_NO_WHEELS, bare, bareSize, ctxt, ctxtSize);

	// The cases that change CNET.
	{
		static const struct
		{
			const char *name;
			int what;
		} cases[] = {
		    {"good_untextured", 0},          {"bad_CNET-1_version-2", 1},      {"bad_CNET-2_flags", 2},
		    {"bad_CNET-3_five-poses", 3},    {"bad_CNET-4_section-outside", 4}, {"bad_CNET-5_no-tris", 5},
		    {"bad_CNET-6_nan", 6},           {"bad_CNET-7_index", 7},          {"bad_CNET-8_texture-3", 8},
		    {"bad_CNET-9_outside-frame", 9}, {"bad_CNET-10_wheel-index", 10},  {"bad_CNET-11_no-ctxt", 11},
		    {"bad_CNET-10_wheel-stride", 12},
		};
		int k;

		for (k = 0; k < (int)(sizeof(cases) / sizeof(cases[0])); k++)
		{
			u8 *c = RldMk_NativeCopy(still, stillSize);
			const u8 *t = ctxt;
			u32 entryAt = 0;
			u32 at;

			if (c == NULL)
			{
				set->failed = 1;
				continue;
			}
			switch (cases[k].what)
			{
			case 0:   // no texture: textureCount 0, material 0 without texture, no CTXT
				Rld_WriteLE16(&c[0x30], 0u);
				Rld_WriteLE16(&c[RldMk_NativeSectionAt(c, "MATL", NULL) + 4u], 0xFFFFu);
				t = NULL;
				break;
			case 1:
				Rld_WriteLE16(&c[0x00], 2u);
				break;
			case 2:
				Rld_WriteLE16(&c[0x02], 1u);
				break;
			case 3:
				Rld_WriteLE16(&c[0x14], 5u);
				break;
			case 4:
				RldMk_NativeSectionAt(c, "POSN", &entryAt);
				Rld_WriteLE32(&c[entryAt + 8u], (u32)stillSize);
				break;
			case 5:
				RldMk_NativeSectionAt(c, "TRIS", &entryAt);
				memcpy(&c[entryAt], "TRIX", 4u);
				break;
			case 6:
				Rld_WriteLE32(&c[RldMk_NativeSectionAt(c, "POSN", NULL)], 0x7FC00000u);
				break;
			case 7:
				Rld_WriteLE16(&c[RldMk_NativeSectionAt(c, "TRIS", NULL)], RLDMK_NATIVE_VERTICES);
				break;
			case 8:
				Rld_WriteLE16(&c[RldMk_NativeSectionAt(c, "MATL", NULL) + 4u], 3u);
				break;
			case 9:   // inside the head's hull, outside every frame of CMDL
				RldMk_PutF32(&c[0x18], -5000.0);
				RldMk_PutF32(&c[RldMk_NativeSectionAt(c, "POSN", NULL)], -4000.0);
				break;
			case 10:
				at = RldMk_NativeSectionAt(c, "WHLS", NULL);
				Rld_WriteLE16(&c[at + RLDCHAR_WHEEL_HEAD_BYTES + (RLDMK_NATIVE_WHEEL_VERTICES * RLDCHAR_WHEEL_VERTEX_BYTES)], RLDMK_NATIVE_WHEEL_VERTICES);
				break;
			case 12:   // WHLS has its own layout: stride 0
				RldMk_NativeSectionAt(c, "WHLS", &entryAt);
				Rld_WriteLE16(&c[entryAt + 6u], 32u);
				break;
			default:   // 11: no CTXT
				t = NULL;
				break;
			}
			RldMk_NativeAdd(set, cases[k].name, 0u, c, stillSize, t, (t != NULL) ? ctxtSize : 0u);
			free(c);
		}
	}

	// Wheels are required unless CHRI hides them.
	RldMk_NativeAdd(set, "bad_CNET-10_no-wheels", 0u, bare, bareSize, ctxt, ctxtSize);

	// The cases that change CTXT.
	{
		static const char *const names[] = {"bad_CTXT-1_version-2", "bad_CTXT-2_entry-size", "bad_CTXT-3_format-2", "bad_CTXT-4_side-100",
		                                    "bad_CTXT-5_outside",   "bad_CTXT-7_hash",       "bad_CTXT-8_png-size", "bad_CTXT-8_png-head-4096",
		                                    "bad_CTXT-8_png-crc"};
		const u32 entry = RLDCHAR_TEX_HEAD_BYTES;
		int k;

		for (k = 0; k < (int)(sizeof(names) / sizeof(names[0])); k++)
		{
			u8 *t = RldMk_NativeCopy(ctxt, ctxtSize);

			if (t == NULL)
			{
				set->failed = 1;
				continue;
			}
			switch (k)
			{
			case 0:
				Rld_WriteLE16(&t[0x00], 2u);
				break;
			case 1:
				Rld_WriteLE16(&t[0x06], 0x50u);
				break;
			case 2:
				t[entry + 0x00] = 2;
				break;
			case 3:
				Rld_WriteLE16(&t[entry + 0x04], 100u);
				break;
			case 4:
				Rld_WriteLE32(&t[entry + 0x08], (u32)ctxtSize);
				break;
			case 5:   // a byte of the image data, behind signature and IHDR
				t[Rld_ReadLE32(&t[entry + 0x08]) + 60u] ^= 0x01u;
				break;
			case 6:   // 128 x 128 declared, the PNG is 256 x 256 (the hash still holds)
				Rld_WriteLE16(&t[entry + 0x04], 128u);
				Rld_WriteLE16(&t[entry + 0x06], 128u);
				break;
			case 7:   // the PNG's head says 4096 x 4096: refused by its head, nothing inflated (hash made to hold)
				RldMk_NativeBE32(&t[Rld_ReadLE32(&t[entry + 0x08]) + 16u], 4096u);
				RldMk_NativeBE32(&t[Rld_ReadLE32(&t[entry + 0x08]) + 20u], 4096u);
				Sha256(&t[Rld_ReadLE32(&t[entry + 0x08])], Rld_ReadLE32(&t[entry + 0x0C]), &t[entry + 0x10]);
				break;
			default:   // a byte of the image data, the hash made to hold: the head passes, the decoder refuses (CRC)
				t[Rld_ReadLE32(&t[entry + 0x08]) + 60u] ^= 0x01u;
				Sha256(&t[Rld_ReadLE32(&t[entry + 0x08])], Rld_ReadLE32(&t[entry + 0x0C]), &t[entry + 0x10]);
				break;
			}
			RldMk_NativeAdd(set, names[k], 0u, still, stillSize, t, ctxtSize);
			free(t);
		}
	}

	// CTXT-6: 16 textures of 2048 x 2048 (on paper: the budget is counted
	// before any byte is hashed or decoded), and CNET naming 16.
	{
		const size_t size = RLDCHAR_TEX_HEAD_BYTES + (16u * RLDCHAR_TEX_ENTRY_BYTES) + (16u * 8u);
		u8 *t = (u8 *)calloc(1, size);
		u8 *c = RldMk_NativeCopy(still, stillSize);
		u32 i;

		if ((t != NULL) && (c != NULL))
		{
			Rld_WriteLE16(&t[0x00], RLDCHAR_TEX_VERSION);
			Rld_WriteLE16(&t[0x04], 16u);
			Rld_WriteLE16(&t[0x06], RLDCHAR_TEX_ENTRY_BYTES);
			for (i = 0; i < 16u; i++)
			{
				u8 *e = &t[RLDCHAR_TEX_HEAD_BYTES + (i * RLDCHAR_TEX_ENTRY_BYTES)];

				e[0x00] = RLDCHAR_TEX_FORMAT_PNG;
				Rld_WriteLE16(&e[0x04], 2048u);
				Rld_WriteLE16(&e[0x06], 2048u);
				Rld_WriteLE32(&e[0x08], RLDCHAR_TEX_HEAD_BYTES + (16u * RLDCHAR_TEX_ENTRY_BYTES) + (i * 8u));
				Rld_WriteLE32(&e[0x0C], 8u);
			}
			Rld_WriteLE16(&c[0x30], 16u);
			RldMk_NativeAdd(set, "bad_CTXT-6_budget", 0u, c, stillSize, t, size);
		}
		else
		{
			set->failed = 1;
		}
		free(t);
		free(c);
	}

	// CNET-11 the other way: CTXT holds another count than CNET names.
	{
		u8 *c = RldMk_NativeCopy(still, stillSize);

		if (c != NULL)
		{
			Rld_WriteLE16(&c[0x30], 2u);
			RldMk_NativeAdd(set, "bad_CNET-11_count", 0u, c, stillSize, ctxt, ctxtSize);
			free(c);
		}
	}

	// The envelope: what only the reader of the native part checks (the
	// driver keeps CMDL), and what every reader checks (the whole file).
	{
		struct RldMkNativeCase *c;
		u8 *entry;

		RldMk_NativeAdd(set, "bad_native-chunk_hash", 0u, still, stillSize, ctxt, ctxtSize);
		c = &set->item[set->count - 1];
		c->envelope = 1;
		c->file[(size_t)Rld_ReadLE64(&RldMk_NativeEntry(c->file, "CNET")[0x08])] ^= 0x01u;

		RldMk_NativeAdd(set, "bad_native-chunk_compressed", 0u, still, stillSize, ctxt, ctxtSize);
		c = &set->item[set->count - 1];
		c->envelope = 1;
		Rld_WriteLE32(&RldMk_NativeEntry(c->file, "CNET")[0x04], 1u);

		RldMk_NativeAdd(set, "bad_native-chunk_sizes", 0u, still, stillSize, ctxt, ctxtSize);
		c = &set->item[set->count - 1];
		c->envelope = 1;
		Rld_WriteLE64(&RldMk_NativeEntry(c->file, "CNET")[0x18], (u64)stillSize + 4u);

		// A type twice: CTXT renamed CNET in the directory.
		RldMk_NativeAdd(set, "damaged_cnet-twice", 0u, still, stillSize, ctxt, ctxtSize);
		c = &set->item[set->count - 1];
		c->envelope = 1;
		memcpy(RldMk_NativeEntry(c->file, "CTXT"), "CNET", 4u);

		// CNET over CMDL.
		RldMk_NativeAdd(set, "damaged_cnet-overlaps-cmdl", 0u, still, stillSize, ctxt, ctxtSize);
		c = &set->item[set->count - 1];
		c->envelope = 1;
		entry = RldMk_NativeEntry(c->file, "CNET");
		Rld_WriteLE64(&entry[0x08], Rld_ReadLE64(&RldMk_NativeEntry(c->file, "CMDL")[0x08]));

		// CNET outside the data area: it starts at the directory (envelope step 14).
		RldMk_NativeAdd(set, "damaged_cnet-outside", 0u, still, stillSize, ctxt, ctxtSize);
		c = &set->item[set->count - 1];
		c->envelope = 1;
		Rld_WriteLE64(&RldMk_NativeEntry(c->file, "CNET")[0x08], Rld_ReadLE64(&c->file[0x18]));
	}

	free(still);
	free(poses);
	free(bare);
	free(ctxt);
	return !set->failed;
}

// THE SELF-TEST: the golden hashes, the texture, and every chunk case in memory.
//========================================================================================
// THE NATIVE MODEL OF AN OBJ (make-char --native-model on): SELF-TEST
//========================================================================================
//
// A mini OBJ made here, in memory, from the self-test's standard model
// (RldMk_TestStandard: kart, driver, steering wheel; 36 quads) with an MTL
// file and its pictures (s_rldObjFiles stands in for the disk):
//   kart     Kd 0.8 0.2 0.1, no texture; every face its own vn (hard edges)
//   driver   map_Kd driver.png, a PNG of 8-bit RGBA (kept as it is), one
//            texel transparent (alpha mode mask); UVs inside 0..1 (clamp)
//   steering wheel   map_Kd -s 2 2 1 face.png, a PNG of RGB (written again as
//            RGBA); the scale puts UVs outside 0..1 (repeat)
// built with --fit none (RldMk_TestOptions). The cases: the automatic poses
// with the test wheel (47 poses, WHLS), --poses still with --wheels off (one
// still pose, no WHLS), a face turned inside out with --repair auto (the
// repair turns it: RldMkBuild.plyPosition), and the refusals.

// make-char --fit none --native-model on of the mini OBJ: CNET and CTXT of
// the automatic poses with the test wheel, and of --poses still --wheels off.
// They follow deliberate changes of the chain (the frames) and of the native
// model's rules (tools/rldpack_native.inc); nothing else may change them.
#define RLDMK_GOLDEN_NATIVE_OBJ_CNET "0b4ee6fe16d44a87d168268df057230a15462e99bd500e2e5fbaaa8e94063cd2"
#define RLDMK_GOLDEN_NATIVE_OBJ_CTXT "201619f4ad3f4c7bc0d54c5a36e1406bec4918d47974e4e55bd8957e4949f586"
#define RLDMK_GOLDEN_NATIVE_OBJ_STILL "ab06f57dc82483f1f1faa8ff6ecc6fa5e9ac75137f70bebb93f96f929685e962"
// The mini OBJ with kart face 3 turned inside out and kart face 2 left out,
// --repair auto: the native triangles repaired as the CMDL's (turned back,
// the hole closed) - the native model of make-native-tests --obj.
#define RLDMK_GOLDEN_NATIVE_OBJ_REPAIR "be878af022b345bef44f29ca5ad220af36daf6d0bae738f285a334682c0b952c"
// The preview block of that native model (RLDPN1, THE NATIVE MODEL IN THE
// PREVIEW), as make-char --preview appends it for Reload Studio.
#define RLDMK_GOLDEN_NATIVE_OBJ_PREVIEW "1c87b550ff8f8a4dc2b98788031daefb298caba0ed2ebcd6f5a3213e6b71d895"

#define RLDMK_NATIVE_OBJ_FILES (4u + RLDCHAR_TEX_COUNT_MAX + 1u)

struct RldMkNativeObjCase
{
	int still;                 // --poses still
	int wheelsOff;             // --wheels off
	int repair;                // --repair auto, with kart face 3 turned inside out
	int hole;                  // kart face 2 left out (a hole of 4 edges, which the repair closes)
	int holeDriver;            // driver face 10 left out (a hole in a textured part)
	int twoSided;              // --two-sided
	int ply;                   // the standard model as PLY instead (no OBJ to take)
	int want;                  // s_rldObjNativeWant while the model is read
	u32 texWidth;              // driver.png; 0 = 16 x 32
	u32 texHeight;
	int noVt;                  // the driver's corners without vt
	int missing;               // driver.png not there
	u32 textures;              // > 0: every triangle its own material, the first `textures` with a picture of their own
	u32 materials;             // > 0: every triangle its own material, `materials` of them in turn
};

struct RldMkNativeObjResult
{
	int built;                 // the CMDL was built
	int made;                  // RldMk_NativeModel returned 1
	u8 *cmdl;
	size_t cmdlSize;
	u8 *cnet;
	size_t cnetSize;
	u8 *ctxt;
	size_t ctxtSize;
	u32 chriFlags;
	u8 *plyBytes;              // the PLY the reader handed the chain, as bytes (xyz, rgb, corners)
	size_t plySize;
	int captured;              // the reader kept the faces (RldObj_CaptureNative)
	int plyPosition;           // the build kept RldMkBuild.plyPosition (the repair changed the model)
	struct RldMkNativeFacts facts;
};

static void RldMk_NativeObjFree(struct RldMkNativeObjResult *r)
{
	free(r->cmdl);
	free(r->cnet);
	free(r->ctxt);
	free(r->plyBytes);
	memset(r, 0, sizeof(*r));
}

// An RGBA picture of the test: a gradient, texel (1, 1) transparent when alpha.
static u8 *RldMk_NativeObjPng(u32 width, u32 height, int alpha, u32 seed, size_t *sizeOut)
{
	u8 *rgba = (u8 *)malloc((size_t)width * height * 4u + 4u);
	u8 *png;
	u32 x;
	u32 y;

	*sizeOut = 0;
	if (rgba == NULL)
	{
		return NULL;
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			u8 *p = &rgba[((size_t)y * width + x) * 4u];

			p[0] = (u8)((x * 255u) / (width - 1u));
			p[1] = (u8)((y * 255u) / (height - 1u));
			p[2] = (u8)(seed * 37u);
			p[3] = (alpha && (x == 1u) && (y == 1u)) ? 0u : 255u;
		}
	}
	png = RldMk_PngRgba(rgba, width, height, sizeOut);
	free(rgba);
	return png;
}

// Read, build and make the native model of one case. 1 = it ran (r says
// what came out); the caller frees r.
static int RldMk_NativeObjMake(const struct RldMkNativeObjCase *k, struct RldMkNativeObjResult *r)
{
	static char names[RLDCHAR_TEX_COUNT_MAX + 1u][32];
	struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	struct RldObjMemFile files[RLDMK_NATIVE_OBJ_FILES];
	u8 *pictures[RLDMK_NATIVE_OBJ_FILES];
	struct RldMkBuffer obj;
	struct RldMkBuffer mtl;
	struct RldMkOptions opt;
	struct RldMkModelInfo info;
	struct RldMkPly ply;
	struct RldMkBuild b;
	u32 fileCount = 0;
	u32 i;
	u32 j;
	int ok = (mesh != NULL);

	memset(r, 0, sizeof(*r));
	memset(&obj, 0, sizeof(obj));
	memset(&mtl, 0, sizeof(mtl));
	memset(&ply, 0, sizeof(ply));
	memset(&b, 0, sizeof(b));
	memset(pictures, 0, sizeof(pictures));
	if (!ok)
	{
		return 0;
	}
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	RldMk_TestOptions(&opt);
	opt.still = k->still;
	opt.noWheels = k->wheelsOff;
	opt.repair = k->repair;
	opt.twoSided = k->twoSided;

	// The MTL file and its pictures.
	RldMk_BufferText(&mtl, "# made by the rldpack self-test\nnewmtl kart\nKd 0.8 0.2 0.1\n");
	RldMk_BufferText(&mtl, "newmtl driver\nmap_Kd driver.png\nnewmtl face\nmap_Kd -s 2 2 1 face.png\n");
	files[fileCount].path = "selftest/native.mtl";
	fileCount++;
	if (!k->missing)
	{
		size_t size = 0;

		pictures[fileCount] = RldMk_NativeObjPng((k->texWidth != 0u) ? k->texWidth : 16u, (k->texHeight != 0u) ? k->texHeight : 32u, 1, 1u, &size);
		files[fileCount].path = "selftest/driver.png";
		files[fileCount].data = pictures[fileCount];
		files[fileCount].size = size;
		ok = ok && (pictures[fileCount] != NULL);
		fileCount++;
	}
	{
		static const u8 rgb[32u * 16u * 3u] = {0};
		u8 *plain = (u8 *)malloc(sizeof(rgb));
		size_t size = 0;

		if (plain != NULL)
		{
			for (i = 0; i < (u32)sizeof(rgb); i++)
			{
				plain[i] = (u8)((i * 7u) & 0xFFu);
			}
			pictures[fileCount] = RldMk_TestPngRgb(32u, 16u, plain, &size);
			free(plain);
		}
		files[fileCount].path = "selftest/face.png";
		files[fileCount].data = pictures[fileCount];
		files[fileCount].size = size;
		ok = ok && (pictures[fileCount] != NULL);
		fileCount++;
	}
	for (i = 0; ok && (i < k->textures) && (i < RLDCHAR_TEX_COUNT_MAX + 1u); i++)
	{
		size_t size = 0;

		snprintf(names[i], sizeof(names[i]), "selftest/t%u.png", i);
		RldMk_BufferText(&mtl, "newmtl t%u\nmap_Kd t%u.png\n", i, i);
		pictures[fileCount] = RldMk_NativeObjPng(16u, 16u, 0, i, &size);
		files[fileCount].path = names[i];
		files[fileCount].data = pictures[fileCount];
		files[fileCount].size = size;
		ok = (pictures[fileCount] != NULL);
		fileCount++;
	}
	for (i = 0; i < k->materials; i++)
	{
		RldMk_BufferText(&mtl, "newmtl m%u\nKd %.3f 0.5 0.5\n", i, (double)i / 100.0);
	}
	files[0].data = mtl.data;
	files[0].size = mtl.size;
	ok = ok && (mtl.data != NULL);

	// The OBJ: v, vt, vn, and the faces.
	RldMk_BufferText(&obj, "# made by the rldpack self-test\nmtllib native.mtl\n");
	for (i = 0; i < mesh->vertexCount; i++)
	{
		RldMk_BufferText(&obj, "v %.6f %.6f %.6f\nvt %.4f %.4f\n", mesh->xyz[i][0], mesh->xyz[i][1], mesh->xyz[i][2], 0.1 + 0.8 * (double)((i * 37u) % 100u) / 100.0,
		                 0.1 + 0.8 * (double)((i * 53u) % 100u) / 100.0);
	}
	for (i = 0; i < 7u; i++)
	{
		RldMk_BufferText(&obj, "vn %d %d %d\n", (i == 0u) ? 1 : 0, (i == 1u) ? 1 : 0, (i >= 2u) ? 1 : 0);
	}
	for (i = 0; i < mesh->faceCount; i++)
	{
		const u32 vn = (i < 6u) ? (i + 1u) : 7u;
		const int split = (k->textures != 0u) || (k->materials != 0u);
		u32 piece;

		if ((k->hole && (i == 2u)) || (k->holeDriver && (i == 10u)))
		{
			continue;
		}
		if (!split && ((i == 0u) || (i == 6u) || (i == 30u)))
		{
			RldMk_BufferText(&obj, "usemtl %s\n", (i == 0u) ? "kart" : ((i == 6u) ? "driver" : "face"));
		}
		for (piece = 0; piece < (split ? (mesh->cornerCount[i] - 2u) : 1u); piece++)
		{
			const u32 count = split ? 3u : mesh->cornerCount[i];
			const u32 triangle = (2u * i) + piece;

			if (split && (triangle < k->textures))
			{
				RldMk_BufferText(&obj, "usemtl t%u\n", triangle);
			}
			else if (split && (k->materials != 0u))
			{
				RldMk_BufferText(&obj, "usemtl m%u\n", triangle % k->materials);
			}
			else if (split && (triangle == k->textures))
			{
				RldMk_BufferText(&obj, "usemtl kart\n");
			}
			RldMk_BufferText(&obj, "f");
			for (j = 0; j < count; j++)
			{
				// a fan of the quad when split; kart face 3 turned with --repair
				const u32 at = split ? ((j == 0u) ? 0u : (piece + j)) : (((i == 3u) && k->repair) ? (count - 1u - j) : j);
				const u32 v = mesh->corners[i][at] + 1u;

				if (k->noVt && (i >= 6u) && (i < 30u))
				{
					RldMk_BufferText(&obj, " %u//%u", v, vn);
				}
				else
				{
					RldMk_BufferText(&obj, " %u/%u/%u", v, v, vn);
				}
			}
			RldMk_BufferText(&obj, "\n");
		}
	}
	ok = ok && (obj.data != NULL);

	// Read and build as make-char does.
	s_rldObjFiles = files;
	s_rldObjFileCount = fileCount;
	RldObj_FreeNative(&s_rldObjNative);
	if (ok && k->ply)
	{
		s_rldObjNativeWant = k->want;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		s_rldObjNativeWant = 0;
		r->built = ok;
	}
	else if (ok)
	{
		s_rldObjNativeWant = k->want;
		ok = RldMk_TestImport("selftest/native.obj", (const char *)obj.data, obj.size, &ply, &info, RLDMK_ROLE_MAIN);
		s_rldObjNativeWant = 0;
		ok = ok && RldMk_BuildModel(&ply, &opt, &b);
		r->built = ok;
	}
	r->captured = s_rldObjNative.captured;
	if (ok)
	{
		struct RldMkBuffer bytes;

		memset(&bytes, 0, sizeof(bytes));
		RldMk_BufferBytes(&bytes, ply.xyz, (size_t)ply.vertexCount * 3u * sizeof(double));
		RldMk_BufferBytes(&bytes, ply.rgb, (size_t)ply.vertexCount * 3u);
		RldMk_BufferBytes(&bytes, ply.corners, (size_t)ply.faceCount * 4u * sizeof(u32));
		RldMk_BufferBytes(&bytes, ply.cornerCount, (size_t)ply.faceCount * sizeof(u32));
		r->plyBytes = bytes.data;
		r->plySize = bytes.size;
		r->cmdl = RldMk_NativeCopy(b.cmdl, b.cmdlSize);
		r->cmdlSize = b.cmdlSize;
		r->plyPosition = (b.plyPosition != NULL);
		r->chriFlags = k->wheelsOff ? RLDCHAR_FLAG_NO_WHEELS : 0u;
		RldMk_ResetSaid();
		r->made = RldMk_NativeModel(&s_rldObjNative, &ply, &b, &opt, !k->wheelsOff, &r->cnet, &r->cnetSize, &r->ctxt, &r->ctxtSize, &r->facts);
	}
	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	RldObj_FreeNative(&s_rldObjNative);

	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	for (i = 0; i < RLDMK_NATIVE_OBJ_FILES; i++)
	{
		free(pictures[i]);
	}
	free(obj.data);
	free(mtl.data);
	free(mesh);
	return ok;
}

// Edges of a checked native part (pose 0), by the positions of their ends
// (the float bits): *open = edges run by one triangle only, *against = edges
// two triangles run the same way (a winding that does not match). A closed
// mesh wound one way has both 0.
static void RldMk_NativeTestEdges(const struct RldCharNative *n, u32 *open, u32 *against)
{
	const u32 edges = 3u * n->triangleCount;
	u32 i;
	u32 j;

	*open = 0;
	*against = 0;
	for (i = 0; i < edges; i++)
	{
		const u32 t = i / 3u;
		const u32 k = i % 3u;
		const u8 *ia = &n->indices[((size_t)(3u * t) + k) * n->indexSize];
		const u8 *ib = &n->indices[((size_t)(3u * t) + ((k + 1u) % 3u)) * n->indexSize];
		const u8 *pa = &n->poses[(size_t)Rld_ReadLE16(ia) * RLDCHAR_NET_VERTEX_BYTES];
		const u8 *pb = &n->poses[(size_t)Rld_ReadLE16(ib) * RLDCHAR_NET_VERTEX_BYTES];
		u32 reverse = 0;
		u32 same = 0;

		for (j = 0; j < edges; j++)
		{
			const u32 u = j / 3u;
			const u32 l = j % 3u;
			const u8 *qa = &n->poses[(size_t)Rld_ReadLE16(&n->indices[((size_t)(3u * u) + l) * n->indexSize]) * RLDCHAR_NET_VERTEX_BYTES];
			const u8 *qb = &n->poses[(size_t)Rld_ReadLE16(&n->indices[((size_t)(3u * u) + ((l + 1u) % 3u)) * n->indexSize]) * RLDCHAR_NET_VERTEX_BYTES];

			if (j == i)
			{
				continue;
			}
			reverse += ((memcmp(qa, pb, 12u) == 0) && (memcmp(qb, pa, 12u) == 0)) ? 1u : 0u;
			same += ((memcmp(qa, pa, 12u) == 0) && (memcmp(qb, pb, 12u) == 0)) ? 1u : 0u;
		}
		*open += (reverse == 0u) ? 1u : 0u;
		*against += (same != 0u) ? 1u : 0u;
	}
}

// Triangles of a checked native part whose three corners have the same UV
// (bit for bit) and a texture: the lids (every other triangle of the mini
// OBJ has three different vt).
static u32 RldMk_NativeTestSameUv(const struct RldCharNative *n)
{
	u32 same = 0;
	u32 t;

	for (t = 0; (n->uv != NULL) && (t < n->triangleCount); t++)
	{
		const u32 v0 = Rld_ReadLE16(&n->indices[(size_t)(3u * t) * n->indexSize]);
		const u32 v1 = Rld_ReadLE16(&n->indices[(size_t)(3u * t + 1u) * n->indexSize]);
		const u32 v2 = Rld_ReadLE16(&n->indices[(size_t)(3u * t + 2u) * n->indexSize]);
		const u8 *m = &n->materials[(size_t)Rld_ReadLE16(&n->triangleMaterials[2u * t]) * RLDCHAR_NET_MATERIAL_BYTES];

		same += ((RldChar_SignedHalf(Rld_ReadLE16(&m[4])) >= 0) && (memcmp(&n->uv[8u * v0], &n->uv[8u * v1], 8u) == 0) &&
		         (memcmp(&n->uv[8u * v0], &n->uv[8u * v2], 8u) == 0))
		            ? 1u
		            : 0u;
	}
	return same;
}

// The files of make-native-tests --obj: the mini OBJ without and with its
// native model (one CMDL for all, the game's self-test compares it), with
// kart face 3 turned inside out and face 2 left out, --repair auto: the
// native model repaired as the CMDL (RLDMK_GOLDEN_NATIVE_OBJ_REPAIR).
static int RldMk_NativeObjSet(struct RldMkNativeSet *set)
{
	struct RldMkNativeObjCase k;
	struct RldMkNativeObjResult wheels;
	struct RldMkNativeObjResult hidden;
	int ok;

	memset(set, 0, sizeof(*set));
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.repair = 1;
	k.hole = 1;
	ok = RldMk_NativeObjMake(&k, &wheels) && wheels.made;
	k.wheelsOff = 1;
	ok = RldMk_NativeObjMake(&k, &hidden) && hidden.made && ok;
	// --wheels off with --fit none keeps the CMDL (its fit and poses are those of the kart's)
	ok = ok && (wheels.cmdlSize == hidden.cmdlSize) && (memcmp(wheels.cmdl, hidden.cmdl, wheels.cmdlSize) == 0);
	if (ok)
	{
		set->cmdl = wheels.cmdl;
		set->cmdlSize = wheels.cmdlSize;
		wheels.cmdl = NULL;
		RldMk_NativeAdd(set, "old_obj-plain", 0u, NULL, 0u, NULL, 0u);
		RldMk_NativeAdd(set, "good_obj-native", 0u, wheels.cnet, wheels.cnetSize, wheels.ctxt, wheels.ctxtSize);
		RldMk_NativeAdd(set, "good_obj-native-wheels-hidden", RLDCHAR_FLAG_NO_WHEELS, hidden.cnet, hidden.cnetSize, hidden.ctxt, hidden.ctxtSize);
		ok = !set->failed;
	}
	RldMk_NativeObjFree(&wheels);
	RldMk_NativeObjFree(&hidden);
	return ok;
}

// One case that must be refused with `code` (and nothing returned).
static void RldMk_NativeObjRefused(const struct RldMkNativeObjCase *k, const char *code, const char *name)
{
	struct RldMkNativeObjResult r;
	const int ran = RldMk_NativeObjMake(k, &r);

	RldMk_Test(ran && r.built && !r.made && (r.cnet == NULL) && (r.ctxt == NULL) && RldMk_Said(code), name, "ran %d, built %d, made %d, said: %s", ran,
	           r.built, r.made, s_rldMkCodes);
	RldMk_NativeObjFree(&r);
}

static void RldMk_SelfTestNativeObj(void)
{
	struct RldMkNativeObjCase k;
	struct RldMkNativeObjResult a;
	struct RldMkNativeObjResult again;
	struct RldMkNativeObjResult off;
	struct RldCharNative native;

	// 1. The automatic poses with the test wheel: golden, ready, twice the same.
	memset(&k, 0, sizeof(k));
	k.want = 1;
	if (!RldMk_NativeObjMake(&k, &a) || !a.made)
	{
		RldMk_Test(0, "native OBJ: the mini OBJ", "cannot be made (built %d, made %d, said: %s)", a.built, a.made, s_rldMkCodes);
		RldMk_NativeObjFree(&a);
		return;
	}
	RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
	RldMk_Test(native.state == RLDCHAR_NATIVE_READY, "native OBJ: the game's rules take it", "refused (%s) %s", (native.rule != NULL) ? native.rule : "",
	           native.detail);
	RldMk_Test((native.poseCount == RLDCHAR_NET_POSES) && (native.wheel != NULL) && (native.textureCount == 2u) && (native.materialCount == 4u) &&
	               (native.uv != NULL) && (native.colors == NULL) && (a.facts.converted[0] == 0) && (a.facts.converted[1] == 1) && a.facts.wheels &&
	               (a.facts.dropped == 0u) && (a.facts.flat == 0u),
	           "native OBJ: 47 poses, the test wheel, 2 textures (the RGBA PNG kept, the RGB one written again), 4 materials, no COL0",
	           "poses %u, wheel %d, textures %u, materials %u, converted %d %d, dropped %u, flat %u", native.poseCount, native.wheel != NULL,
	           native.textureCount, native.materialCount, a.facts.converted[0], a.facts.converted[1], a.facts.dropped, a.facts.flat);
	if (native.state == RLDCHAR_NATIVE_READY)
	{
		const u8 *m = native.materials;

		// driver: white, texture 0, mask (a transparent texel); face: texture 1, opaque; kart: Kd, none
		RldMk_Test((m[0] == 204u) && (m[1] == 51u) && (m[2] == 26u) && (Rld_ReadLE16(&m[4]) == 0xFFFFu) && (m[6] == 0u) && (m[16] == 255u) &&
		               (Rld_ReadLE16(&m[20]) == 0u) && (m[22] == 1u) && (Rld_ReadLE16(&m[36]) == 1u) && (m[38] == 0u) &&
		               ((native.texture[0].flags & 0xFFu) == ((1u << RLDCHAR_TEX_ALPHA_SHIFT) | (1u << RLDCHAR_TEX_WRAP_U_SHIFT) | (1u << RLDCHAR_TEX_WRAP_V_SHIFT))) &&
		               ((native.texture[1].flags & 0xFFu) == 0u),
		           "native OBJ: materials (Kd, white with a texture, mask) and texture flags (clamp inside 0..1, repeat outside)",
		           "kart %u %u %u tex %u alpha %u; driver %u tex %u alpha %u; face tex %u alpha %u; flags 0x%x 0x%x", m[0], m[1], m[2],
		           Rld_ReadLE16(&m[4]), m[6], m[16], Rld_ReadLE16(&m[20]), m[22], Rld_ReadLE16(&m[36]), m[38], native.texture[0].flags,
		           native.texture[1].flags);
	}
	// The preview block (Reload Studio): its size from its counts, golden.
	if (native.state == RLDCHAR_NATIVE_READY)
	{
		size_t blockSize = 0;
		u8 *block = RldMk_NativePreviewBlock(&native, &blockSize);
		const size_t want = 8u + 4u + (12u + 16u * 32u * 4u) + (12u + 32u * 16u * 4u) + 8u + ((size_t)native.triangleCount * (3u * 18u + 40u));

		RldMk_Test((block != NULL) && (blockSize == want) && (memcmp(block, "RLDPN1\0\0", 8u) == 0) && (Rld_ReadLE32(&block[8]) == 2u),
		           "native OBJ: the preview block (RLDPN1) holds 2 textures and every triangle in 3 poses", "size %u, want %u", (u32)blockSize, (u32)want);
		if ((block != NULL) && (Rld_CheckGolden("make-char --native-model on --preview of the mini OBJ: RLDPN1", block, blockSize,
		                                        RLDMK_GOLDEN_NATIVE_OBJ_PREVIEW) != 0))
		{
			s_rldMkTestFailed++;
		}
		free(block);
	}
	{
		struct RldCharNative refused;

		memset(&refused, 0, sizeof(refused));
		refused.state = RLDCHAR_NATIVE_REFUSED;
		{
			size_t none = 1;
			u8 *block = RldMk_NativePreviewBlock(&refused, &none);

			RldMk_Test((block == NULL) && (none == 0u), "native OBJ: no preview block for a refused native part", "made one");
			free(block);
		}
	}
	RldChar_FreeNative(&native);
	if (Rld_CheckGolden("make-char --fit none --native-model on of the mini OBJ: CNET", a.cnet, a.cnetSize, RLDMK_GOLDEN_NATIVE_OBJ_CNET) != 0)
	{
		s_rldMkTestFailed++;
	}
	if (Rld_CheckGolden("make-char --fit none --native-model on of the mini OBJ: CTXT", a.ctxt, a.ctxtSize, RLDMK_GOLDEN_NATIVE_OBJ_CTXT) != 0)
	{
		s_rldMkTestFailed++;
	}
	{
		const int ran = RldMk_NativeObjMake(&k, &again) && again.made;

		RldMk_Test(ran && (again.cnetSize == a.cnetSize) && (memcmp(again.cnet, a.cnet, a.cnetSize) == 0) && (again.ctxtSize == a.ctxtSize) &&
		               (memcmp(again.ctxt, a.ctxt, a.ctxtSize) == 0),
		           "native OBJ: built twice, the same CNET and CTXT", "differ");
		RldMk_NativeObjFree(&again);
	}

	// 2. Without the switch: the reader keeps nothing, hands the chain the same
	//    PLY, and the chain builds the same CMDL - no new chunk, the same bytes.
	k.want = 0;
	if (RldMk_NativeObjMake(&k, &off))
	{
		RldMk_Test(!off.captured && a.captured && (off.plySize == a.plySize) && (memcmp(off.plyBytes, a.plyBytes, a.plySize) == 0) &&
		               (off.cmdlSize == a.cmdlSize) && (memcmp(off.cmdl, a.cmdl, a.cmdlSize) == 0),
		           "native OBJ: without --native-model the reader keeps nothing, the PLY and the CMDL are the same", "captured %d/%d, ply %u/%u, cmdl %u/%u",
		           off.captured, a.captured, (u32)off.plySize, (u32)a.plySize, (u32)off.cmdlSize, (u32)a.cmdlSize);
	}
	else
	{
		RldMk_Test(0, "native OBJ: without --native-model", "cannot be built");
	}
	{
		u8 field[RLDCHAR_NAME_FIELD];
		size_t chriSize = 0;
		size_t plainSize = 0;
		size_t nativeSize = 0;
		u8 *chri;
		u8 *plain = NULL;
		u8 *none = NULL;

		memset(field, 0, sizeof(field));
		memcpy(field, "SELF TEST", 9u);
		chri = RldMk_BuildChri(14u, 0u, field, 1u, "", 0u, &chriSize);
		if ((chri != NULL) && (off.cmdl != NULL))
		{
			plain = RldMk_ContainerVoices(chri, chriSize, off.cmdl, off.cmdlSize, NULL, NULL, 0u, NULL, 0u, &plainSize);
			none = RldMk_ContainerNative(chri, chriSize, off.cmdl, off.cmdlSize, NULL, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, &nativeSize);
		}
		RldMk_Test((plain != NULL) && (none != NULL) && (plainSize == nativeSize) && (memcmp(plain, none, plainSize) == 0) &&
		               (RldMk_NativeEntry(plain, "CNET") == NULL) && (RldMk_NativeEntry(plain, "CTXT") == NULL),
		           "native OBJ: without a native model the container is that of before (no CNET, no CTXT)", "differs");
		free(chri);
		free(plain);
		free(none);
	}
	RldMk_NativeObjFree(&off);
	RldMk_NativeObjFree(&a);

	// 3. --poses still --wheels off: one still pose, no WHLS; golden.
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.still = 1;
	k.wheelsOff = 1;
	if (RldMk_NativeObjMake(&k, &a) && a.made)
	{
		RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
		RldMk_Test((native.state == RLDCHAR_NATIVE_READY) && (native.poseCount == 0u) && (native.wheel == NULL),
		           "native OBJ: --poses still --wheels off: one still pose, no wheels, ready", "state %d (%s) %s, poses %u", native.state,
		           (native.rule != NULL) ? native.rule : "", native.detail, native.poseCount);
		RldChar_FreeNative(&native);
		if (Rld_CheckGolden("make-char --fit none --poses still --wheels off --native-model on of the mini OBJ: CNET", a.cnet, a.cnetSize,
		                    RLDMK_GOLDEN_NATIVE_OBJ_STILL) != 0)
		{
			s_rldMkTestFailed++;
		}
	}
	else
	{
		RldMk_Test(0, "native OBJ: --poses still --wheels off", "not made (said: %s)", s_rldMkCodes);
	}
	RldMk_NativeObjFree(&a);

	// 4. A face turned inside out and one left out, --repair auto: the native
	//    triangles get the repair of the CMDL - turned back, the hole closed,
	//    closed and wound one way afterwards (the game culls back faces).
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.repair = 1;
	k.hole = 1;
	if (RldMk_NativeObjMake(&k, &a) && a.made)
	{
		u32 open = 0;
		u32 against = 0;

		RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
		if (native.state == RLDCHAR_NATIVE_READY)
		{
			RldMk_NativeTestEdges(&native, &open, &against);
		}
		RldMk_Test((native.state == RLDCHAR_NATIVE_READY) && a.plyPosition && a.facts.repaired && (a.facts.flipped == 2u) && (a.facts.lids == 2u) &&
		               (a.facts.openEdges == 0u) && (a.facts.backFaces == 0u) && (native.triangleCount == 72u) && (open == 0u) && (against == 0u),
		           "native OBJ: --repair auto: 2 triangles turned back, the hole closed with 2, closed and wound one way, ready",
		           "state %d (%s) %s, plyPosition %d, flipped %u, lids %u, open edges %u/%u, against %u, back %u, triangles %u", native.state,
		           (native.rule != NULL) ? native.rule : "", native.detail, a.plyPosition, a.facts.flipped, a.facts.lids, a.facts.openEdges, open, against,
		           a.facts.backFaces, native.triangleCount);
		RldChar_FreeNative(&native);
		if (Rld_CheckGolden("make-char --fit none --native-model on of the mini OBJ, a face turned and one left out: CNET", a.cnet, a.cnetSize,
		                    RLDMK_GOLDEN_NATIVE_OBJ_REPAIR) != 0)
		{
			s_rldMkTestFailed++;
		}
	}
	else
	{
		RldMk_Test(0, "native OBJ: --repair auto", "not made (said: %s)", s_rldMkCodes);
	}
	RldMk_NativeObjFree(&a);

	// 4a. A hole in the textured driver: the lid has the material and, at
	//     every corner, the UV of its corner 0 - no texture smeared across it.
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.repair = 1;
	k.holeDriver = 1;
	if (RldMk_NativeObjMake(&k, &a) && a.made)
	{
		u32 open = 0;
		u32 against = 0;
		u32 same = 0;

		RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
		if (native.state == RLDCHAR_NATIVE_READY)
		{
			RldMk_NativeTestEdges(&native, &open, &against);
			same = RldMk_NativeTestSameUv(&native);
		}
		RldMk_Test((native.state == RLDCHAR_NATIVE_READY) && (a.facts.lids == 2u) && (same == 2u) && (open == 0u) && (against == 0u),
		           "native OBJ: a hole in a textured part: its 2 lids textured with one UV each (corner 0), closed, ready",
		           "state %d (%s) %s, lids %u, one-UV textured triangles %u, open %u, against %u", native.state, (native.rule != NULL) ? native.rule : "",
		           native.detail, a.facts.lids, same, open, against);
		RldChar_FreeNative(&native);
	}
	else
	{
		RldMk_Test(0, "native OBJ: a hole in a textured part", "not made (said: %s)", s_rldMkCodes);
	}
	RldMk_NativeObjFree(&a);

	// 4b. --repair off with the hole: the faces as written (one hole of 4 edges).
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.hole = 1;
	if (RldMk_NativeObjMake(&k, &a) && a.made)
	{
		u32 open = 0;
		u32 against = 0;

		RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
		if (native.state == RLDCHAR_NATIVE_READY)
		{
			RldMk_NativeTestEdges(&native, &open, &against);
		}
		RldMk_Test((native.state == RLDCHAR_NATIVE_READY) && !a.facts.repaired && (native.triangleCount == 70u) && (open == 4u) && (against == 0u),
		           "native OBJ: --repair off: the faces as written, the hole stays (4 open edges)", "state %d, triangles %u, open %u, against %u",
		           native.state, native.triangleCount, open, against);
		RldChar_FreeNative(&native);
	}
	else
	{
		RldMk_Test(0, "native OBJ: --repair off with a hole", "not made (said: %s)", s_rldMkCodes);
	}
	RldMk_NativeObjFree(&a);

	// 4c. --two-sided: every triangle a second time, wound the other way.
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.twoSided = 1;
	if (RldMk_NativeObjMake(&k, &a) && a.made)
	{
		RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
		RldMk_Test((native.state == RLDCHAR_NATIVE_READY) && (a.facts.backFaces == 72u) && (native.triangleCount == 144u),
		           "native OBJ: --two-sided: every triangle also wound the other way (144), ready", "state %d (%s) %s, back %u, triangles %u", native.state,
		           (native.rule != NULL) ? native.rule : "", native.detail, a.facts.backFaces, native.triangleCount);
		RldChar_FreeNative(&native);
	}
	else
	{
		RldMk_Test(0, "native OBJ: --two-sided", "not made (said: %s)", s_rldMkCodes);
	}
	RldMk_NativeObjFree(&a);

	// 5. A texture that is not there: drawn in its color (warning), ready.
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.missing = 1;
	if (RldMk_NativeObjMake(&k, &a) && a.made)
	{
		RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
		RldMk_Test((native.state == RLDCHAR_NATIVE_READY) && (native.textureCount == 1u) && (a.facts.untextured == 1u) && RldMk_Said("native-texture"),
		           "native OBJ: a missing texture: its material untextured (native-texture), ready", "state %d, textures %u, untextured %u, said: %s",
		           native.state, native.textureCount, a.facts.untextured, s_rldMkCodes);
		RldChar_FreeNative(&native);
	}
	else
	{
		RldMk_Test(0, "native OBJ: a missing texture", "not made (said: %s)", s_rldMkCodes);
	}
	RldMk_NativeObjFree(&a);

	// 6. The refusals: nothing comes back, the reason is said.
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.texWidth = 24u;
	k.texHeight = 16u;
	RldMk_NativeObjRefused(&k, "native-texture-size", "native OBJ refused: a texture of 24 x 16 (not a power of two)");
	k.texWidth = 4096u;
	k.texHeight = 16u;
	RldMk_NativeObjRefused(&k, "native-texture-size", "native OBJ refused: a texture of 4096 x 16 (over 2048)");
	k.texWidth = 8u;
	k.texHeight = 8u;
	RldMk_NativeObjRefused(&k, "native-texture-size", "native OBJ refused: a texture of 8 x 8 (under 16)");
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.noVt = 1;
	RldMk_NativeObjRefused(&k, "native-model", "native OBJ refused: textured corners without vt");
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.textures = RLDCHAR_TEX_COUNT_MAX + 1u;
	RldMk_NativeObjRefused(&k, "native-model", "native OBJ refused: 17 textures");
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.materials = RLDCHAR_NET_MATERIALS_MAX;
	RldMk_NativeObjRefused(&k, "native-model", "native OBJ refused: 64 materials and the wheels'");
	memset(&k, 0, sizeof(k));
	k.want = 1;
	k.ply = 1;
	RldMk_NativeObjRefused(&k, "native-model", "native OBJ refused: a PLY model");

	// A model the chain reduced: refused (the native vertices would not find
	// their positions). Reduced for real it takes some 7000 triangles; the
	// flag of the build is set by hand here.
	memset(&k, 0, sizeof(k));
	k.want = 1;
	{
		struct RldMkNativeObjResult r;
		struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
		struct RldMkOptions opt;
		struct RldMkPly ply;
		struct RldMkBuild b;
		struct RldObjNative fake;
		u8 *cnet = NULL;
		u8 *ctxt = NULL;
		size_t cnetSize = 0;
		size_t ctxtSize = 0;
		int ok = (mesh != NULL);

		memset(&r, 0, sizeof(r));
		memset(&fake, 0, sizeof(fake));
		fake.captured = 1;
		RldMk_TestOptions(&opt);
		if (ok)
		{
			RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
			ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		}
		if (ok)
		{
			b.reduced = 1;
			RldMk_ResetSaid();
			r.made = RldMk_NativeModel(&fake, &ply, &b, &opt, 1, &cnet, &cnetSize, &ctxt, &ctxtSize, &r.facts);
			b.reduced = 0;
			RldMk_FreeBuild(&b);
			RldMk_FreePly(&ply);
		}
		RldMk_Test(ok && !r.made && (cnet == NULL) && RldMk_Said("native-model"), "native OBJ refused: a model the chain reduced", "built %d, made %d, said: %s", ok,
		           r.made, s_rldMkCodes);
		free(cnet);
		free(ctxt);
		free(mesh);
	}
	RldMk_ResetSaid();
}

static void RldMk_SelfTestNative(void)
{
	struct RldMkNativeSet set;
	struct RldCharNative native;
	u8 *rgba = (u8 *)malloc((size_t)RLDMK_NATIVE_TEXTURE * RLDMK_NATIVE_TEXTURE * 4u);
	int i;

	// The texture is the probe's, byte for byte, and its PNG reads back to it.
	if (rgba != NULL)
	{
		struct RldPngImage image;
		const char *why = NULL;
		size_t pngSize = 0;
		u8 *png;
		int same;

		RldMk_NativeTexture(rgba);
		if (Rld_CheckGolden("native probe texture (the game's NATIVE_PROBE_TEXTURE_GOLDEN)", rgba, (size_t)RLDMK_NATIVE_TEXTURE * RLDMK_NATIVE_TEXTURE * 4u,
		                    RLDMK_GOLDEN_NATIVE_TEXTURE) != 0)
		{
			s_rldMkTestFailed++;
		}
		png = RldMk_PngRgba(rgba, RLDMK_NATIVE_TEXTURE, RLDMK_NATIVE_TEXTURE, &pngSize);
		memset(&image, 0, sizeof(image));
		same = (png != NULL) && (RldPng_Decode(png, pngSize, &image, &why) == RLDPNG_OK) && (image.width == RLDMK_NATIVE_TEXTURE) &&
		       (image.height == RLDMK_NATIVE_TEXTURE) && (image.colorType == 6u) && (image.bitDepth == 8u) &&
		       (memcmp(image.rgba, rgba, (size_t)RLDMK_NATIVE_TEXTURE * RLDMK_NATIVE_TEXTURE * 4u) == 0);
		RldMk_Test(same, "native: the probe texture as PNG reads back to the same RGBA8", "%s", (why != NULL) ? why : "differs");
		RldPng_Free(&image);
		free(png);
		free(rgba);
	}
	else
	{
		RldMk_Test(0, "native: texture", "out of memory");
	}

	if (!RldMk_NativeMakeSet(&set))
	{
		RldMk_Test(0, "native: the test set", "cannot be made");
		RldMk_NativeFreeSet(&set);
		return;
	}

	// Built twice: the same bytes.
	{
		struct RldMkNativeSet again;
		int same = RldMk_NativeMakeSet(&again) && (again.count == set.count);

		for (i = 0; same && (i < set.count); i++)
		{
			same = (set.item[i].size == again.item[i].size) && (memcmp(set.item[i].file, again.item[i].file, set.item[i].size) == 0);
		}
		RldMk_Test(same, "native: the test set built twice: the same bytes", "differ");
		RldMk_NativeFreeSet(&again);
	}

	for (i = 0; i < set.count; i++)
	{
		const struct RldMkNativeCase *c = &set.item[i];
		char expected[32];
		char label[96];
		const char *underscore;

		if (strcmp(c->name, "old_plain") == 0)
		{
			if (Rld_CheckGolden("native: old_plain (no CNET, no CTXT) is make-char --fit none of today", c->file, c->size, RLDMK_GOLDEN_PLAIN) != 0)
			{
				s_rldMkTestFailed++;
			}
			continue;
		}
		if (strcmp(c->name, "good_probe-still") == 0)
		{
			if (Rld_CheckGolden("make-char --fit none --native-probe still", c->file, c->size, RLDMK_GOLDEN_NATIVE_STILL) != 0)
			{
				s_rldMkTestFailed++;
			}
		}
		if (strcmp(c->name, "good_probe-poses") == 0)
		{
			if (Rld_CheckGolden("make-char --fit none --native-probe poses", c->file, c->size, RLDMK_GOLDEN_NATIVE_POSES) != 0)
			{
				s_rldMkTestFailed++;
			}
		}
		if (c->envelope)
		{
			continue;   // the game's self-test reads these from files
		}

		RldMk_CheckNativeBytes(set.cmdl, set.cmdlSize, c->chriFlags, c->cnet, c->cnetSize, c->ctxt, c->ctxtSize, &native);
		snprintf(label, sizeof(label), "native %s", c->name);
		if (strncmp(c->name, "good_", 5u) == 0)
		{
			RldMk_Test(native.state == RLDCHAR_NATIVE_READY, label, "refused (%s) %s", (native.rule != NULL) ? native.rule : "", native.detail);
		}
		else if (strncmp(c->name, "none_", 5u) == 0)
		{
			RldMk_Test(native.state == RLDCHAR_NATIVE_NONE, label, "state %d", native.state);
		}
		else
		{
			underscore = strchr(&c->name[4], '_');
			snprintf(expected, sizeof(expected), "%.*s", (underscore != NULL) ? (int)(underscore - &c->name[4]) : 0, &c->name[4]);
			RldMk_Test((native.state == RLDCHAR_NATIVE_REFUSED) && (native.rule != NULL) && (strcmp(native.rule, expected) == 0), label,
			           "expected %s, got state %d %s %s", expected, native.state, (native.rule != NULL) ? native.rule : "", native.detail);
		}
		RldChar_FreeNative(&native);
	}

	RldMk_NativeFreeSet(&set);

	// The native model of an OBJ (make-char --native-model on).
	RldMk_SelfTestNativeObj();
}

// Is there a CMakeCache.txt in folder or in one of the eight folders above
// it? The folder is first resolved to its real, absolute path (Rld_FullPath:
// _fullpath or realpath), so "<build>/../tools" is judged as the tools folder
// it names and not by its text. A path that cannot be resolved is refused.
static int RldMk_InBuildFolder(const char *folder)
{
	char path[RLD_PATH_MAX];
	size_t length;
	int up;

	if (!Rld_FullPath(folder, path, sizeof(path)))
	{
		return 0;
	}
	for (up = 0; (up <= 8) && (path[0] != '\0'); up++)
	{
		char probe[RLD_PATH_MAX + 32];
		FILE *file;

		snprintf(probe, sizeof(probe), "%s/CMakeCache.txt", path);
		file = fopen(probe, "rb");
		if (file != NULL)
		{
			fclose(file);
			return 1;
		}

		// One folder up: "C:\a\b" -> "C:\a" -> "C:" -> "".
		length = strlen(path);
		while ((length > 0u) && Rld_IsSeparator(path[length - 1u]))
		{
			length--;
		}
		while ((length > 0u) && !Rld_IsSeparator(path[length - 1u]))
		{
			length--;
		}
		while ((length > 0u) && Rld_IsSeparator(path[length - 1u]))
		{
			length--;
		}
		path[length] = '\0';
	}
	return 0;
}

// rldpack make-native-tests [--obj] <folder>: the whole set as files; with
// --obj the set of the mini OBJ instead (old_obj-plain and its native model
// with and without the test wheel, make-char --native-model on).
static int RldChar_NativeTestsCommand(int argc, char **argv)
{
	struct RldMkNativeSet set;
	const int objSet = (argc == 2) && (strcmp(argv[0], "--obj") == 0);
	int written = 0;
	int made;
	int i;

	if ((argc != 1) && !objSet)
	{
		fprintf(stderr, "rldpack make-native-tests [--obj] <folder>  (a folder inside a CMake build folder)\n");
		return 2;
	}
	argv += objSet ? 1 : 0;
	if (!RldMk_InBuildFolder(argv[0]))
	{
		fprintf(stderr, "rldpack: %s is not inside a CMake build folder (no CMakeCache.txt in it or above it) - the test files never go into the source tree\n",
		        argv[0]);
		return 2;
	}
	// The standard model is built quietly, as in the self-test.
	s_rldMkQuiet = 1;
	made = objSet ? RldMk_NativeObjSet(&set) : RldMk_NativeMakeSet(&set);
	s_rldMkQuiet = 0;
	RldMk_ResetSaid();
	if (!made)
	{
		fprintf(stderr, "rldpack: the native test set cannot be made\n");
		RldMk_NativeFreeSet(&set);
		return 1;
	}

	for (i = 0; i < set.count; i++)
	{
		char path[RLD_PATH_MAX + 80];
		FILE *file;
		int ok;

		snprintf(path, sizeof(path), "%s/%s.rldchar", argv[0], set.item[i].name);
		file = fopen(path, "wb");
		ok = (file != NULL) && (fwrite(set.item[i].file, 1, set.item[i].size, file) == set.item[i].size);
		if (file != NULL)
		{
			ok = (fclose(file) == 0) && ok;
		}
		if (!ok)
		{
			fprintf(stderr, "rldpack: %s cannot be written\n", path);
			RldMk_NativeFreeSet(&set);
			return 1;
		}
		printf("  wrote %s (%u bytes)\n", path, (u32)set.item[i].size);
		written++;
	}

	printf("%d native test files written\n", written);
	RldMk_NativeFreeSet(&set);
	return 0;
}
