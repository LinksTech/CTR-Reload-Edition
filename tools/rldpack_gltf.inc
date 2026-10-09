// rldpack_gltf.inc - make-char: glTF 2.0 models (.glb, .gltf) and their shape keys
//
// Part of the packer, never of the game. tools/rldpack_import.inc includes it
// right after the OBJ reader. cgltf (externals/cgltf/cgltf.h, v1.15, MIT; its
// implementation is compiled once in tools/rldpack.c) parses the file; this
// reader turns it into the model of the OBJ reader (struct RldObjModel) and
// hands that on to the same steps the OBJ reader takes after its text: the
// colors, the textures, the native model's source, the split of textured
// triangles and the points. So a glTF is an OBJ with another reader: every
// rule, budget and message of the chain after step 1 is the OBJ's.
//
// WHAT IS READ. The scene (the default one, else the first, else every node
// without a parent), every node with a mesh with its world matrix applied
// (translation, rotation, scale or matrix, in double; a node that mirrors
// keeps its faces facing out), all primitives of a mesh (TRIANGLES only, other
// modes are refused: model-gltf-mode), POSITION, NORMAL (only which corners
// share one, as the vn of an OBJ), the TEXCOORD_n of the base color texture,
// COLOR_0, indices of any size or none. Accessors are read through cgltf with
// sparse data applied and normalized integers converted. The buffers: the GLB
// binary chunk, data: URIs and files next to a .gltf.
//
// THE MODEL OF THE OBJ READER. One v per different (node, position, vertex
// color, shape key deltas) - the vertices a glTF exporter split at seams of
// normals and texture coordinates are one v again, as in Blender's OBJ - one
// vt per different texture coordinate (u, 1 - v: glTF counts v from the top,
// OBJ from the bottom), one vn per different normal of a node. Every node
// with a mesh is a group (@model group, its node's or mesh's name). The
// materials, named as in the file (an unnamed or a repeated name gets its
// number), become materials of the OBJ reader:
//   baseColorFactor           Kd (as written, no conversion - Blender writes
//                             the same values into its MTL file); with a
//                             texture and the factor white: no Kd (Blender's
//                             MTL has none there either), the texture alone
//   emissiveFactor            Ke (a Ke above Kd brightens it, as in an MTL)
//   baseColorTexture          map_Kd: the image's file next to the model, or
//                             for an image in the file (bufferView, data:
//                             URI) a copy in the folder for temporary files
//                             (TEMP, else TMP) named rldpack-gltf-<digest>
//                             (written once, kept; the self-test keeps it in
//                             memory), which @model texture names
//   KHR_texture_transform     -o and -s (offset and scale; a rotation is
//                             said and left out)
//   a sampler CLAMP_TO_EDGE   -clamp on (on both axes)
//   alphaMode MASK or BLEND,  transparent (the game draws the model opaque,
//   a factor alpha below 1    said as for an OBJ)
// COLOR_0 is linear in glTF: it is turned into sRGB bytes (a table, no pow),
// what an OBJ export of the same colors writes. A vertex whose COLOR_0 is
// exactly white has no vertex color: white multiplies nothing in glTF, and
// Blender writes a white COLOR_0 for every mesh that has a color attribute.
//
// SHAPE KEYS (morph targets). Only for the model itself (RLDMK_ROLE_MAIN) and
// for the poses (RLDMK_ROLE_POSE). The name of target t of a mesh is
// mesh.extras.targetNames[t] (Blender), else the name of its POSITION
// accessor. A name of s_rldGltfKeyNames, in any letter case, is that key;
// targets of one name in several meshes are one key, a mesh without it moves
// by 0. Any other name is said once (pose-unknown-key, warning) and ignored.
// Only the POSITION deltas are read, mapped by the node's world matrix
// without its translation. The default weights of a mesh are not used: the
// base mesh is the neutral pose.
//
// THE INTERFACE FOR THE POSES (rldpack_char.inc, rldpack_anim.inc):
// s_rldGltfMorphs, filled by every read for RLDMK_ROLE_MAIN or
// RLDMK_ROLE_POSE (freed before it, so after an OBJ or a PLY read == 0):
//   present           bit (1u << RLDGLTF_KEY_*) for each key found
//   point[k]          3 doubles per point of the RldMkPly handed to the chain
//                     (the index of ply->xyz, before step 3 welds), in the
//                     axes and units of ply->xyz; NULL = key k not present.
//                     A point the split of textured triangles made gets the
//                     delta of its place in its triangle (barycentric: the
//                     split is linear, so this is exact).
//   source[k]         3 doubles per v of the native model's source (the
//                     index of s_rldObjNative.xyz and cornerV), filled with
//                     or without --native-model
//   unknown[]         the unknown names, as written (the first
//                     RLDGLTF_UNKNOWN_KEPT; unknownCount counts all)
// The frame of the chain (--up, --forward, the factor of --fit) maps a delta
// as a direction: without the shift. RldGltf_FreeMorphs frees it.
// Machine line per name (model and poses): @model shape-key <name> <key|unknown>.
//
// REFUSED (error, no file): a file cgltf cannot parse or whose buffers it
// cannot load (model-gltf-broken, cgltf's reason as the technical line), no
// mesh with triangles (model-gltf-empty), a primitive not of triangles
// (model-gltf-mode), a skin or JOINTS_0 (model-rig), Draco or meshopt
// compression (model-gltf-unsupported). The sizes of the OBJ reader hold
// (obj-big).
//
// Every number is read as cgltf gives it (float) and computed in double in a
// fixed order: the same file gives the same points and bytes.

#define RLDGLTF_FILE_BYTES (256u * 1024u * 1024u)  // a .bin file or a .gltf's buffer file, at most
#define RLDGLTF_DEPTH 64u                          // nodes in nodes, at most
#define RLDGLTF_UNKNOWN_KEPT 32u
#define RLDGLTF_KEY_NAME_MAX 63u

enum
{
	RLDGLTF_KEY_STEER_LEFT = 0,
	RLDGLTF_KEY_STEER_RIGHT,
	RLDGLTF_KEY_REVERSE,
	RLDGLTF_KEY_CRASH,
	RLDGLTF_KEY_JUMP,
	RLDGLTF_KEY_WIN,
	RLDGLTF_KEY_LOSE,
	RLDGLTF_KEY_COUNT
};

static const char *const s_rldGltfKeyNames[RLDGLTF_KEY_COUNT] = {"steer_left", "steer_right", "reverse", "crash", "jump", "win", "lose"};

// The shape keys of the last model read (see the head of this file).
struct RldGltfMorphs
{
	int read;                                   // 1 = the last read for RLDMK_ROLE_MAIN or RLDMK_ROLE_POSE was a glTF, without error
	u32 present;                                // bit (1u << RLDGLTF_KEY_*): a target of at least one mesh
	u32 pointCount;                             // = ply->vertexCount of the RldMkPly handed to the chain
	double *point[RLDGLTF_KEY_COUNT];           // 3 per ply point, NULL = not present
	u32 sourceCount;                            // = the v of the native source (s_rldObjNative.vCount when captured)
	double *source[RLDGLTF_KEY_COUNT];          // 3 per v of the native source, NULL = not present
	u32 unknownCount;                           // unknown names, each name once
	char unknown[RLDGLTF_UNKNOWN_KEPT][RLDGLTF_KEY_NAME_MAX + 1u];
};

static struct RldGltfMorphs s_rldGltfMorphs;

static void RldGltf_FreeMorphs(void)
{
	int k;

	for (k = 0; k < RLDGLTF_KEY_COUNT; k++)
	{
		free(s_rldGltfMorphs.point[k]);
		free(s_rldGltfMorphs.source[k]);
	}
	memset(&s_rldGltfMorphs, 0, sizeof(s_rldGltfMorphs));
}

static int RldGltf_SameNoCase(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a;
		char y = *b;

		x = ((x >= 'A') && (x <= 'Z')) ? (char)(x - 'A' + 'a') : x;
		y = ((y >= 'A') && (y <= 'Z')) ? (char)(y - 'A' + 'a') : y;
		if (x != y)
		{
			return 0;
		}
		if (x == '\0')
		{
			return 1;
		}
	}
}

// RLDGLTF_KEY_* of a target name in any letter case, -1 = not a key.
static int RldGltf_KeyOf(const char *name)
{
	int k;

	for (k = 0; k < RLDGLTF_KEY_COUNT; k++)
	{
		if (RldGltf_SameNoCase(name, s_rldGltfKeyNames[k]))
		{
			return k;
		}
	}
	return -1;
}

//========================================================================================
// COLORS: linear to sRGB
//========================================================================================

// The linear value at which sRGB byte b + 1 begins: the sRGB curve at
// (b + 0.5) / 255 (made once with the formula of IEC 61966-2-1 in double).
static const double s_rldGltfSrgbEdge[255] = {
	0.00015176349177441876, 0.00045529047532325625, 0.0007588174588720937, 0.0010623444424209313, 0.0013658714259697686, 0.0016693984095186062,
	0.0019729253930674436, 0.002276452376616281, 0.0025799793601651187, 0.0028835063437139563, 0.003188300904430532, 0.00350925934958123,
	0.0038483149330964263, 0.004205748030104947, 0.00458183274052838, 0.004976837250274023, 0.005391024159806381, 0.005824650784040898,
	0.006277969426914108, 0.006751227633498623, 0.007244668422128921, 0.00775853049866786, 0.00829304845476233, 0.008848452951698497,
	0.00942497089126609, 0.010022825574869039, 0.010642236851973576, 0.011283421258858298, 0.01194659214852213, 0.012631959812511863,
	0.013339731595349034, 0.01407011200216447, 0.014823302800086416, 0.015599503113873273, 0.016398909516233677, 0.017221716113234104,
	0.018068114625156378, 0.018938294463134074, 0.019832442801866853, 0.02075074464868551, 0.021693382909216234, 0.022660538449872064,
	0.023652390157379497, 0.024669114995532006, 0.025710888059345766, 0.026777882626779784, 0.02787027020816926, 0.028988220593509972,
	0.030131901897720907, 0.03130148060400286, 0.032497121605402225, 0.033718988244681086, 0.03496724235258795, 0.03624204428461639,
	0.03754355295633311, 0.03887192587735158, 0.040227319184021844, 0.04160988767090289, 0.04301978482107941, 0.04445716283538092, 0.04592217266055746,
	0.04741496401646282, 0.04893568542229298, 0.05048448422192488, 0.0520615066083972, 0.053666897647573375, 0.05530080130102386, 0.05696336044816294,
	0.05865471690767354, 0.06037501145825081, 0.062124383858694746, 0.06390297286737924, 0.0657109162611246, 0.06754835085349804, 0.06941541251256611,
	0.07131223617812144, 0.07323895587840543, 0.07519570474634667, 0.07718261503533434, 0.07919981813454503, 0.08124744458384041, 0.08332562408825164,
	0.08543448553206703, 0.08757415699253683, 0.08974476575321062, 0.09194643831691977, 0.09417930041841839, 0.09644347703669504, 0.09873909240696693,
	0.1010662700323678, 0.10342513269534023, 0.1058158024687427, 0.10823840072668099, 0.11069304815507364, 0.11317986476196008, 0.1156989698875601,
	0.11825048221409341, 0.12083451977536606, 0.12345119996613248, 0.12610063955123937, 0.1287829546745594, 0.13149826086772048, 0.1342466730586372,
	0.13702830557985107, 0.13984327217668513, 0.14269168601521828, 0.1455736596900856, 0.1484893052321087, 0.15143873411576272, 0.1544220572664832,
	0.1574393850678189, 0.1604908273684337, 0.16357649348896341, 0.1666964922287304, 0.16985093187232053, 0.17303992019602688, 0.1762635644741625,
	0.17952197148524762, 0.18281524751807332, 0.18614349837764563, 0.1895068293910138, 0.19290534541298454, 0.19633915083172693, 0.1998083495742689,
	0.2033130451118907, 0.206853340465415, 0.21042933821039977, 0.21404114048223255, 0.21768884898113222, 0.2213725649770588, 0.2250923893145328,
	0.22884842241736916, 0.23264076429332461, 0.23646951453866302, 0.24033477234264017, 0.2442366364919083, 0.24817520537484558, 0.2521505769858089,
	0.2561628489293138, 0.26021211842414343, 0.2642984823073866, 0.2684220370384083, 0.27258287870275355, 0.27678110301598524, 0.2810168053274597,
	0.28529008062403893, 0.28960102353374223, 0.2939497283293396, 0.2983362889318845, 0.30276079891419333, 0.3072233515042663, 0.31172403958865513,
	0.3162629557157785, 0.3208401920991837, 0.32545584062075916, 0.33010999283389664, 0.33480273996660304, 0.33953417292456833, 0.34430438229418264,
	0.3491134583455109, 0.35396149103522073, 0.35884857000946707, 0.3637747846067349, 0.3687402238606382, 0.3737449765026789, 0.3787891309649659,
	0.3838727753828926, 0.3889959975977785, 0.3941588851594697, 0.3993615253289054, 0.4046040050806454, 0.4098864111053629, 0.41520882981230195,
	0.4205713473317016, 0.425974049517184, 0.4314170219481122, 0.43690034993191296, 0.4424241185063697, 0.44798841244188325, 0.4535933162437017,
	0.45923891415412094, 0.4649252901546552, 0.4706525279681792, 0.4764207110610409, 0.4822299226451468, 0.4880802456800205, 0.49397176287483296,
	0.49990455669040795, 0.5058787093411998, 0.5118943027972472, 0.5179514187861014, 0.5240501387947288, 0.530190544071392, 0.5363727156275037,
	0.5425967342394598, 0.5488626804504493, 0.5551706345722394, 0.5615206766869424, 0.5679128866487574, 0.5743473440856917, 0.5808241284012621,
	0.5873433187761736, 0.5939049941699807, 0.6005092333227251, 0.6071561147565558, 0.6138457167773311, 0.620578117476199, 0.627353394731159,
	0.6341716262086091, 0.6410328893648692, 0.647937261447692, 0.6548848194977529, 0.6618756403501225, 0.6689098006357259, 0.6759873767827809,
	0.6831084450182222, 0.6902730813691093, 0.6974813616640164, 0.7047333615344107, 0.7120291564160104, 0.7193688215501313, 0.7267524319850172,
	0.7341800625771542, 0.7416517879925734, 0.749167682708136, 0.7567278210128072, 0.7643322770089146, 0.7719811246133931, 0.7796744375590167,
	0.7874122893956174, 0.7951947534912903, 0.8030219030335869, 0.8108938110306934, 0.8188105503125999, 0.8267721935322541, 0.834778813166706,
	0.8428304815182371, 0.8509272707154808, 0.8590692527145302, 0.8672564993000342, 0.8754890820862818, 0.8837670725182769, 0.892090541872801,
	0.9004595612594655, 0.9088742016217518, 0.9173345337380439, 0.9258406282226491, 0.9343925555268067, 0.9429903859396902, 0.9516341895893968,
	0.9603240364439274, 0.969059996312159, 0.9778421388448044, 0.986670533535366, 0.9955452497210776};

// A linear color value as the sRGB byte / 255 nearest to it; out of 0..1
// clamped and counted.
static double RldGltf_Srgb(double linear, u32 *clamped)
{
	u32 low = 0;
	u32 high = 255;

	if (!(linear >= 0.0) || (linear > 1.0))
	{
		(*clamped)++;
		return (linear > 1.0) ? 1.0 : 0.0;
	}
	// The first edge above the value: its byte.
	while (low < high)
	{
		const u32 middle = (low + high) / 2u;

		if (linear < s_rldGltfSrgbEdge[middle])
		{
			high = middle;
		}
		else
		{
			low = middle + 1u;
		}
	}
	return (double)low / 255.0;
}

//========================================================================================
// FILES: the buffers of a .gltf, and the images in the file
//========================================================================================

// cgltf reads a .gltf's buffer files through the OBJ reader's files: the
// disk, or the self-test's files in memory.
static cgltf_result RldGltf_FileRead(const struct cgltf_memory_options *memory, const struct cgltf_file_options *file, const char *path, cgltf_size *size,
                                     void **data)
{
	u8 *bytes = NULL;
	size_t length = 0;
	const int read = RldObj_ReadFile(path, RLDGLTF_FILE_BYTES, &bytes, &length);

	(void)memory;
	(void)file;
	if (read == RLDOBJ_READ_MEMORY)
	{
		return cgltf_result_out_of_memory;
	}
	if (read != RLDOBJ_READ_OK)
	{
		return (read == RLDOBJ_READ_LARGE) ? cgltf_result_io_error : cgltf_result_file_not_found;
	}
	*size = length;
	*data = bytes;
	return cgltf_result_success;
}

static void RldGltf_FileRelease(const struct cgltf_memory_options *memory, const struct cgltf_file_options *file, void *data)
{
	(void)memory;
	(void)file;
	free(data);
}

// The images of the self-test's files in memory: s_rldObjFiles is set to
// s_rldGltfFiles, its own files first, the images after them (kept until the
// next glTF is read).
static struct RldObjMemFile *s_rldGltfFiles;
static u32 s_rldGltfFileCount;
static u32 s_rldGltfFileCapacity;
static const struct RldObjMemFile *s_rldGltfFilesBefore;
static u32 s_rldGltfFileCountBefore;
static u32 s_rldGltfImagesFrom;

static void RldGltf_FreeFiles(void)
{
	u32 i;

	for (i = s_rldGltfImagesFrom; i < s_rldGltfFileCount; i++)
	{
		free((void *)s_rldGltfFiles[i].data);
		free((void *)s_rldGltfFiles[i].path);
	}
	s_rldGltfFileCount = s_rldGltfImagesFrom;
}

// The self-test's files again, before a model is read.
static int RldGltf_StartFiles(void)
{
	if (s_rldObjFiles == NULL)
	{
		return 1;
	}
	RldGltf_FreeFiles();
	if (s_rldObjFiles != s_rldGltfFiles)
	{
		s_rldGltfFilesBefore = s_rldObjFiles;
		s_rldGltfFileCountBefore = s_rldObjFileCount;
	}
	if (s_rldGltfFileCapacity < (s_rldGltfFileCountBefore + 16u))
	{
		struct RldObjMemFile *bigger = (struct RldObjMemFile *)realloc(s_rldGltfFiles, (s_rldGltfFileCountBefore + 16u) * sizeof(*bigger));

		if (bigger == NULL)
		{
			return 0;
		}
		s_rldGltfFiles = bigger;
		s_rldGltfFileCapacity = s_rldGltfFileCountBefore + 16u;
	}
	if (s_rldGltfFileCountBefore != 0u)
	{
		memmove(s_rldGltfFiles, s_rldGltfFilesBefore, s_rldGltfFileCountBefore * sizeof(*s_rldGltfFiles));
	}
	s_rldGltfFileCount = s_rldGltfFileCountBefore;
	s_rldGltfImagesFrom = s_rldGltfFileCountBefore;
	s_rldObjFiles = s_rldGltfFiles;
	s_rldObjFileCount = s_rldGltfFileCount;
	return 1;
}

// An image in memory for the self-test, at `path`. 0 = out of memory.
static int RldGltf_MemoryImage(const char *path, const u8 *bytes, size_t size)
{
	u32 i;
	u8 *copy;
	char *name;

	for (i = 0; i < s_rldGltfFileCount; i++)
	{
		if (strcmp(s_rldGltfFiles[i].path, path) == 0)
		{
			return 1;
		}
	}
	if (s_rldGltfFileCount >= s_rldGltfFileCapacity)
	{
		struct RldObjMemFile *bigger = (struct RldObjMemFile *)realloc(s_rldGltfFiles, (s_rldGltfFileCapacity + 16u) * sizeof(*bigger));

		if (bigger == NULL)
		{
			return 0;
		}
		s_rldGltfFiles = bigger;
		s_rldGltfFileCapacity += 16u;
		s_rldObjFiles = s_rldGltfFiles;
	}
	copy = (u8 *)malloc(size + 1u);
	name = RldObj_Copy(path, strlen(path), RLD_PATH_MAX);
	if ((copy == NULL) || (name == NULL))
	{
		free(copy);
		free(name);
		return 0;
	}
	if (size != 0u)
	{
		memcpy(copy, bytes, size);
	}
	s_rldGltfFiles[s_rldGltfFileCount].path = name;
	s_rldGltfFiles[s_rldGltfFileCount].data = copy;
	s_rldGltfFiles[s_rldGltfFileCount].size = size;
	s_rldGltfFileCount++;
	s_rldObjFileCount = s_rldGltfFileCount;
	return 1;
}

// The folder for temporary files with its separator, or 0.
static int RldGltf_TempFolder(char *out, size_t outSize)
{
	const char *folder = getenv("TEMP");
	size_t length;
	int written;

	if ((folder == NULL) || (folder[0] == '\0'))
	{
		folder = getenv("TMP");
	}
	if ((folder == NULL) || (folder[0] == '\0'))
	{
		return 0;
	}
	length = strlen(folder);
	written = snprintf(out, outSize, "%s%s", folder, Rld_IsSeparator(folder[length - 1u]) ? "" : ((strchr(folder, '\\') != NULL) ? "\\" : "/"));
	return (written > 0) && ((size_t)written < outSize);
}

// `bytes` at `path`, unless the file holds them already. Written as
// <path>.part and renamed, so no half file stays under the name. 0 = not written.
static int RldGltf_WriteTemp(const char *path, const u8 *bytes, size_t size)
{
	char part[RLD_PATH_MAX + 8];
	u8 *there = NULL;
	size_t thereSize = 0;
	FILE *file;
	int same;

	if (RldObj_ReadFile(path, RLDOBJ_TEXTURE_BYTES, &there, &thereSize) == RLDOBJ_READ_OK)
	{
		same = (thereSize == size) && ((size == 0u) || (memcmp(there, bytes, size) == 0));
		free(there);
		if (same)
		{
			return 1;
		}
	}
	snprintf(part, sizeof(part), "%s.part", path);
	file = fopen(part, "wb");
	if (file == NULL)
	{
		return 0;
	}
	same = (fwrite(bytes, 1, size, file) == size);
	same = (fclose(file) == 0) && same;
	if (!same)
	{
		remove(part);
		return 0;
	}
	remove(path);
	if (rename(part, path) != 0)
	{
		remove(part);
		return 0;
	}
	return 1;
}

//========================================================================================
// THE READ
//========================================================================================

// Open addressing over indices of items; RLDMK_NONE = empty.
struct RldGltfTable
{
	u32 *slots;
	u32 slotCount;  // a power of two
};

struct RldGltfRead
{
	struct RldObjModel m;
	const char *path;
	cgltf_data *data;
	int morphs;                  // the shape keys are read (RLDMK_ROLE_MAIN, RLDMK_ROLE_POSE)
	double *vDelta;              // 3 x RLDGLTF_KEY_COUNT per v; NULL = no key yet
	u32 present;                 // bit per key found
	u32 *vnKey;                  // 3 per vn: its normal as the bits of 3 floats
	u32 vnCapacity;
	u32 *vnNode;                 // per vn: its node
	u32 vnNodeCapacity;
	u32 vtSlotCount;
	u32 *vtSlots;
	u32 *materialName;           // per material of the file: into m.names, RLDMK_NONE = not used yet
	u32 noMaterial;              // faces without a material
	u32 clamped;                 // color values out of 0..1
	u32 rotated;                 // texture transforms with a rotation (left out)
	u32 otherTexCoord;           // materials whose texture uses another TEXCOORD than 0
	u32 transparent;             // materials in use that are transparent
	char said[RLDGLTF_UNKNOWN_KEPT * 2u][RLDGLTF_KEY_NAME_MAX + 1u];  // target names already listed
	u32 saidCount;
};

static void RldGltf_Broken(const char *file, const char *why)
{
	RldMk_Say("error", "model-gltf-broken", why, "%s cannot be read as glTF 2.0 (%s). Export it again from Blender (File > Export > glTF 2.0, .glb).", file,
	          why);
}

static const char *RldGltf_ResultWord(cgltf_result result)
{
	switch (result)
	{
	case cgltf_result_data_too_short:
		return "data too short";
	case cgltf_result_unknown_format:
		return "unknown format";
	case cgltf_result_invalid_json:
		return "invalid JSON";
	case cgltf_result_invalid_gltf:
		return "invalid glTF";
	case cgltf_result_invalid_options:
		return "invalid options";
	case cgltf_result_file_not_found:
		return "a buffer file was not found";
	case cgltf_result_io_error:
		return "a buffer file cannot be read";
	case cgltf_result_out_of_memory:
		return "out of memory";
	case cgltf_result_legacy_gltf:
		return "glTF 1.0";
	default:
		return "unknown";
	}
}

// Room for `need` items in an array of `size`-byte items. 0 = out of memory.
static int RldGltf_Room(void **items, u32 need, u32 *capacity, size_t size)
{
	u32 grown = (*capacity != 0u) ? *capacity : 1024u;
	void *bigger;

	if (need <= *capacity)
	{
		return 1;
	}
	while (grown < need)
	{
		if (grown > 0x7FFFFFFFu)
		{
			return 0;
		}
		grown *= 2u;
	}
	if ((size_t)grown > (((size_t)-1) / size))
	{
		return 0;
	}
	bigger = realloc(*items, (size_t)grown * size);
	if (bigger == NULL)
	{
		return 0;
	}
	*items = bigger;
	*capacity = grown;
	return 1;
}

static int RldGltf_TableStart(struct RldGltfTable *t, u32 items)
{
	u32 count = 1024u;

	while ((count < (2u * items)) && (count < 0x40000000u))
	{
		count *= 2u;
	}
	if (count > t->slotCount)
	{
		u32 *bigger = (u32 *)realloc(t->slots, (size_t)count * sizeof(u32));

		if (bigger == NULL)
		{
			return 0;
		}
		t->slots = bigger;
		t->slotCount = count;
	}
	memset(t->slots, 0xFF, (size_t)t->slotCount * sizeof(u32));
	return 1;
}

static u32 RldGltf_Hash(const void *bytes, size_t size)
{
	const u8 *b = (const u8 *)bytes;
	u32 hash = 2166136261u;
	size_t i;

	for (i = 0; i < size; i++)
	{
		hash = (hash ^ b[i]) * 16777619u;
	}
	return hash ^ (hash >> 15);
}

// A name said once: as @model shape-key, and as pose-unknown-key when it is
// not a key. Returns its key, -1 = none.
static int RldGltf_SayName(struct RldGltfRead *r, const char *name)
{
	const int key = RldGltf_KeyOf(name);
	char shown[RLDGLTF_KEY_NAME_MAX + 1u];
	u32 i;

	snprintf(shown, sizeof(shown), "%s", name);
	for (i = 0; i < r->saidCount; i++)
	{
		if (strcmp(r->said[i], shown) == 0)
		{
			return key;
		}
	}
	if (r->saidCount < (u32)(sizeof(r->said) / sizeof(r->said[0])))
	{
		memcpy(r->said[r->saidCount++], shown, sizeof(shown));
	}
	RldMk_EmitModel("shape-key", shown, (key >= 0) ? s_rldGltfKeyNames[key] : "unknown", NULL, NULL);
	if (key < 0)
	{
		if (s_rldGltfMorphs.unknownCount < RLDGLTF_UNKNOWN_KEPT)
		{
			memcpy(s_rldGltfMorphs.unknown[s_rldGltfMorphs.unknownCount], shown, sizeof(shown));
		}
		s_rldGltfMorphs.unknownCount++;
		RldMk_Say("warning", "pose-unknown-key", shown,
		          "The shape key \"%s\" is not one of steer_left, steer_right, reverse, crash, jump, win and lose; it is ignored.", shown);
	}
	return key;
}

// The image of a texture as a path for map_Kd: a file next to the model as
// its URI names it, or the image in the file written once into the folder
// for temporary files (the self-test: in memory next to the model). 0 = no
// image to name (said).
static int RldGltf_ImagePath(struct RldGltfRead *r, const cgltf_image *image, char *out, size_t outSize)
{
	static const char *const hex = "0123456789abcdef";
	const u8 *bytes = NULL;
	void *decoded = NULL;
	size_t size = 0;
	const char *type = ".bin";
	char folder[RLD_PATH_MAX];
	char name[64];
	u8 digest[32];
	u32 k;
	int ok;

	if ((image->buffer_view == NULL) && (image->uri != NULL) && (strncmp(image->uri, "data:", 5) != 0))
	{
		char *uri = RldObj_Copy(image->uri, strlen(image->uri), RLD_PATH_MAX - 1u);

		if (uri == NULL)
		{
			RldObj_Memory();
			return 0;
		}
		cgltf_decode_uri(uri);
		snprintf(out, outSize, "%s", uri);
		free(uri);
		return 1;
	}

	if (image->buffer_view != NULL)
	{
		bytes = cgltf_buffer_view_data(image->buffer_view);
		size = image->buffer_view->size;
	}
	else if (image->uri != NULL)
	{
		const char *comma = strchr(image->uri, ',');
		cgltf_options options;
		size_t length;

		memset(&options, 0, sizeof(options));
		if ((comma != NULL) && ((comma - image->uri) >= 7) && (strncmp(comma - 7, ";base64", 7) == 0))
		{
			length = strlen(comma + 1);
			while ((length != 0u) && (comma[length] == '='))
			{
				length--;
			}
			size = (length / 4u) * 3u + (((length % 4u) == 3u) ? 2u : (((length % 4u) == 2u) ? 1u : 0u));
			if ((size == 0u) || (cgltf_load_buffer_base64(&options, size, comma + 1, &decoded) != cgltf_result_success))
			{
				decoded = NULL;
			}
			bytes = (const u8 *)decoded;
		}
	}
	if ((bytes == NULL) || (size == 0u))
	{
		free(decoded);
		RldMk_Say("warning", "tex-unreadable", (image->name != NULL) ? image->name : NULL,
		          "An image of the glTF file has no data that can be read; its faces keep the vertex or material color.");
		return 0;
	}

	if ((size >= 8u) && (bytes[0] == 0x89u) && (memcmp(&bytes[1], "PNG", 3) == 0))
	{
		type = ".png";
	}
	else if ((size >= 3u) && (bytes[0] == 0xFFu) && (bytes[1] == 0xD8u))
	{
		type = ".jpg";
	}
	Sha256(bytes, size, digest);
	memcpy(name, "rldpack-gltf-", 13u);
	for (k = 0; k < 8u; k++)
	{
		name[13u + 2u * k] = hex[digest[k] >> 4];
		name[14u + 2u * k] = hex[digest[k] & 15u];
	}
	snprintf(&name[29], sizeof(name) - 29u, "%s", type);

	if (s_rldObjFiles != NULL)
	{
		// The self-test: next to the model, in memory.
		RldObj_Folder(r->path, folder, sizeof(folder));
		snprintf(out, outSize, "%s", name);
		RldObj_Join(folder, name, folder, sizeof(folder));
		ok = RldGltf_MemoryImage(folder, bytes, size);
		free(decoded);
		if (!ok)
		{
			RldObj_Memory();
		}
		return ok;
	}
	if (!RldGltf_TempFolder(folder, sizeof(folder)) || !RldObj_Join(folder, name, out, outSize) || !RldGltf_WriteTemp(out, bytes, size))
	{
		free(decoded);
		snprintf(out, outSize, "%s", name);
		RldMk_Say("warning", "tex-unreadable", name,
		          "An image inside the glTF file cannot be copied to the folder for temporary files; its faces keep the vertex or material color.");
		return 0;
	}
	free(decoded);
	return 1;
}

// The name in m.names of a material of the file, made on its first use. RLDMK_NONE = out of memory (said).
static u32 RldGltf_Material(struct RldGltfRead *r, const cgltf_material *material)
{
	struct RldObjModel *m = &r->m;
	const u32 index = (u32)cgltf_material_index(r->data, material);
	struct RldObjMaterial *to;
	char name[RLDOBJ_NAME_MAX + 1u];
	u32 at = RLDMK_NONE;
	u32 again = RLDMK_NONE;
	int k;

	if (r->materialName[index] != RLDMK_NONE)
	{
		return r->materialName[index];
	}

	// Its name, or its number; a name already taken gets its number.
	if ((material->name != NULL) && (material->name[0] != '\0'))
	{
		snprintf(name, sizeof(name), "%s", material->name);
	}
	else
	{
		snprintf(name, sizeof(name), "material %u", index);
	}
	if (RldObj_Name(&m->names, name, strlen(name), 0, &again) == 1)
	{
		const size_t used = strlen(name);

		snprintf(&name[(used > (RLDOBJ_NAME_MAX - 12u)) ? (RLDOBJ_NAME_MAX - 12u) : used], 13u, " (%u)", index);
	}
	if ((RldObj_Name(&m->names, name, strlen(name), 1, &at) != 1) || (RldObj_Name(&m->materialNames, name, strlen(name), 1, &again) != 1) ||
	    (at != again) || !RldGltf_Room((void **)&m->materials, m->materialNames.count, &m->materialCapacity, sizeof(*m->materials)))
	{
		RldObj_Memory();
		return RLDMK_NONE;
	}
	to = &m->materials[at];
	memset(to, 0, sizeof(*to));
	to->texture = RLDMK_NONE;
	to->scale[0] = 1.0;
	to->scale[1] = 1.0;
	to->library = 0;

	{
		const cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;
		const cgltf_texture_view *view = &pbr->base_color_texture;
		const int textured = material->has_pbr_metallic_roughness && (view->texture != NULL);
		const cgltf_float *factor = material->has_pbr_metallic_roughness ? pbr->base_color_factor : NULL;
		const int white = (factor == NULL) || ((factor[0] == 1.0f) && (factor[1] == 1.0f) && (factor[2] == 1.0f));

		for (k = 0; k < 3; k++)
		{
			const double value = (factor != NULL) ? (double)factor[k] : 1.0;
			const double light = (double)material->emissive_factor[k];

			to->kd[k] = (value < 0.0) ? 0.0 : ((value > 1.0) ? 1.0 : value);
			to->ke[k] = (light < 0.0) ? 0.0 : ((light > 1.0) ? 1.0 : light);
		}
		to->hasKd = !(textured && white);
		if (to->ke[0] > 0.0 || to->ke[1] > 0.0 || to->ke[2] > 0.0)
		{
			for (k = 0; k < 3; k++)
			{
				to->kd[k] = (!to->hasKd || (to->ke[k] > to->kd[k])) ? to->ke[k] : to->kd[k];
			}
			to->hasKd = 1;
		}
		to->transparent = (material->alpha_mode != cgltf_alpha_mode_opaque) || ((factor != NULL) && (factor[3] < 1.0f));
		r->transparent += to->transparent ? 1u : 0u;

		if (textured)
		{
			const cgltf_texture *texture = view->texture;
			const cgltf_image *image = (texture->image != NULL) ? texture->image : texture->webp_image;
			char map[RLD_PATH_MAX];

			if (view->texcoord != 0)
			{
				r->otherTexCoord++;
			}
			if (view->has_transform)
			{
				// glTF: uv' = offset + scale x uv with v from the top; the
				// OBJ's v from the bottom: offset 1 - o - s, scale s.
				to->offset[0] = (double)view->transform.offset[0];
				to->scale[0] = (double)view->transform.scale[0];
				to->offset[1] = 1.0 - (double)view->transform.offset[1] - (double)view->transform.scale[1];
				to->scale[1] = (double)view->transform.scale[1];
				r->rotated += (view->transform.rotation != 0.0f) ? 1u : 0u;
			}
			if ((texture->sampler != NULL) && (texture->sampler->wrap_s == cgltf_wrap_mode_clamp_to_edge) &&
			    (texture->sampler->wrap_t == cgltf_wrap_mode_clamp_to_edge))
			{
				to->clampOn = 1;
			}
			if (image == NULL)
			{
				RldMk_Say("warning", "tex-unsupported", name,
				          "The texture of the material \"%s\" has no PNG or JPEG image (an extension rldpack does not read); its faces keep the vertex or "
				          "material color.",
				          name);
			}
			else if (RldGltf_ImagePath(r, image, map, sizeof(map)))
			{
				to->map = RldObj_Copy(map, strlen(map), RLD_PATH_MAX);
				if (to->map == NULL)
				{
					RldObj_Memory();
					return RLDMK_NONE;
				}
			}
		}
	}

	r->materialName[index] = at;
	return at;
}

// The deltas of a v that moves with no key.
static const double s_rldGltfZero[RLDGLTF_KEY_COUNT * 3] = {0.0};

// One v for (node, position, color, deltas): found or added. RLDMK_NONE = out of memory or too many (said).
static u32 RldGltf_Vertex(struct RldGltfRead *r, struct RldGltfTable *t, const double xyz[3], const double rgb[3], int colored, const double *delta)
{
	struct RldObjModel *m = &r->m;
	const size_t deltaBytes = (size_t)RLDGLTF_KEY_COUNT * 3u * sizeof(double);
	u32 hash = RldGltf_Hash(xyz, 3u * sizeof(double));
	u32 slot;
	u32 v;

	hash = (hash * 31u) ^ RldGltf_Hash(rgb, 3u * sizeof(double)) ^ (u32)colored;
	if (delta != NULL)
	{
		hash = (hash * 31u) ^ RldGltf_Hash(delta, deltaBytes);
	}
	slot = hash & (t->slotCount - 1u);
	while ((v = t->slots[slot]) != RLDMK_NONE)
	{
		if ((memcmp(&m->xyz[3u * v], xyz, 3u * sizeof(double)) == 0) && (m->vColored[v] == (u8)colored) &&
		    (memcmp(&m->vrgb[3u * v], rgb, 3u * sizeof(double)) == 0) &&
		    ((r->vDelta == NULL) || (memcmp(&r->vDelta[(size_t)v * RLDGLTF_KEY_COUNT * 3u], (delta != NULL) ? delta : s_rldGltfZero, deltaBytes) == 0)))
		{
			return v;
		}
		slot = (slot + 1u) & (t->slotCount - 1u);
	}

	v = m->vCount;
	if (v >= RLDMK_PLY_MAX)
	{
		RldObj_Big("vertices", RLDMK_PLY_MAX);
		return RLDMK_NONE;
	}
	if (v >= m->vCapacity)
	{
		// xyz, vrgb and vColored hold vCapacity v each, as in the OBJ reader
		// (the split grows them the same way); the deltas grow with them.
		const u32 capacity = (m->vCapacity != 0u) ? (2u * m->vCapacity) : 1024u;
		double *xyzMore = (double *)realloc(m->xyz, (size_t)capacity * 3u * sizeof(double));
		double *vrgb;
		u8 *vColored;

		if (xyzMore != NULL)
		{
			m->xyz = xyzMore;
		}
		vrgb = (double *)realloc(m->vrgb, (size_t)capacity * 3u * sizeof(double));
		if (vrgb != NULL)
		{
			m->vrgb = vrgb;
		}
		vColored = (u8 *)realloc(m->vColored, (size_t)capacity);
		if (vColored != NULL)
		{
			m->vColored = vColored;
		}
		if ((xyzMore == NULL) || (vrgb == NULL) || (vColored == NULL))
		{
			RldObj_Memory();
			return RLDMK_NONE;
		}
		if (r->vDelta != NULL)
		{
			double *bigger = (double *)realloc(r->vDelta, (size_t)capacity * deltaBytes);

			if (bigger == NULL)
			{
				RldObj_Memory();
				return RLDMK_NONE;
			}
			r->vDelta = bigger;
		}
		m->vCapacity = capacity;
	}
	memcpy(&m->xyz[3u * v], xyz, 3u * sizeof(double));
	memcpy(&m->vrgb[3u * v], rgb, 3u * sizeof(double));
	m->vColored[v] = (u8)colored;
	m->colored += colored ? 1u : 0u;
	if (r->vDelta != NULL)
	{
		memcpy(&r->vDelta[(size_t)v * RLDGLTF_KEY_COUNT * 3u], (delta != NULL) ? delta : s_rldGltfZero, deltaBytes);
	}
	m->vCount++;
	t->slots[slot] = v;
	return v;
}

// One vn per normal of a node: found or added. RLDMK_NONE = out of memory (said).
static u32 RldGltf_Normal(struct RldGltfRead *r, struct RldGltfTable *t, const cgltf_float n[3], u32 node)
{
	struct RldObjModel *m = &r->m;
	u32 bits[3];
	u32 slot;
	u32 vn;

	memcpy(bits, n, sizeof(bits));
	slot = RldGltf_Hash(bits, sizeof(bits)) & (t->slotCount - 1u);
	while ((vn = t->slots[slot]) != RLDMK_NONE)
	{
		if ((memcmp(&r->vnKey[3u * vn], bits, sizeof(bits)) == 0) && (r->vnNode[vn] == node))
		{
			return vn;
		}
		slot = (slot + 1u) & (t->slotCount - 1u);
	}
	vn = m->vnCount;
	if (vn >= RLDMK_PLY_MAX)
	{
		RldObj_Big("normals", RLDMK_PLY_MAX);
		return RLDMK_NONE;
	}
	if (!RldGltf_Room((void **)&r->vnKey, 3u * (vn + 1u), &r->vnCapacity, sizeof(u32)) ||
	    !RldGltf_Room((void **)&r->vnNode, vn + 1u, &r->vnNodeCapacity, sizeof(u32)))
	{
		RldObj_Memory();
		return RLDMK_NONE;
	}
	memcpy(&r->vnKey[3u * vn], bits, sizeof(bits));
	r->vnNode[vn] = node;
	m->vnCount++;
	t->slots[slot] = vn;
	return vn;
}

// One vt per texture coordinate (u, 1 - v): found or added. RLDMK_NONE = out of memory (said).
static u32 RldGltf_TexCoord(struct RldGltfRead *r, const double uv[2])
{
	struct RldObjModel *m = &r->m;
	u32 slot;
	u32 vt;

	if ((2u * m->vtCount + 2u) > r->vtSlotCount)
	{
		// Grown: the table again over all vt.
		const u32 count = (r->vtSlotCount != 0u) ? (2u * r->vtSlotCount) : 4096u;
		u32 *slots = (u32 *)malloc((size_t)count * sizeof(u32));
		u32 i;

		if (slots == NULL)
		{
			RldObj_Memory();
			return RLDMK_NONE;
		}
		memset(slots, 0xFF, (size_t)count * sizeof(u32));
		for (i = 0; i < m->vtCount; i++)
		{
			slot = RldGltf_Hash(&m->uv[2u * i], 2u * sizeof(double)) & (count - 1u);
			while (slots[slot] != RLDMK_NONE)
			{
				slot = (slot + 1u) & (count - 1u);
			}
			slots[slot] = i;
		}
		free(r->vtSlots);
		r->vtSlots = slots;
		r->vtSlotCount = count;
	}
	slot = RldGltf_Hash(uv, 2u * sizeof(double)) & (r->vtSlotCount - 1u);
	while ((vt = r->vtSlots[slot]) != RLDMK_NONE)
	{
		if (memcmp(&m->uv[2u * vt], uv, 2u * sizeof(double)) == 0)
		{
			return vt;
		}
		slot = (slot + 1u) & (r->vtSlotCount - 1u);
	}
	vt = m->vtCount;
	if (vt >= RLDMK_PLY_MAX)
	{
		RldObj_Big("texture coordinates", RLDMK_PLY_MAX);
		return RLDMK_NONE;
	}
	if (!RldGltf_Room((void **)&m->uv, vt + 1u, &m->vtCapacity, 2u * sizeof(double)))
	{
		RldObj_Memory();
		return RLDMK_NONE;
	}
	memcpy(&m->uv[2u * vt], uv, 2u * sizeof(double));
	m->vtCount++;
	r->vtSlots[slot] = vt;
	return vt;
}

// An accessor as floats, `components` per element (sparse data applied,
// normalized integers converted). NULL = out of memory or unreadable (said).
static float *RldGltf_Floats(const struct RldGltfRead *r, const cgltf_accessor *accessor, u32 components, const char *what)
{
	const size_t count = accessor->count * (size_t)components;
	char why[96];
	float *out;

	if ((cgltf_num_components(accessor->type) != components) || (accessor->count > RLDMK_PLY_MAX))
	{
		snprintf(why, sizeof(why), "%s has the wrong type", what);
		RldGltf_Broken(Rld_LastComponent(r->path), why);
		return NULL;
	}
	out = (float *)malloc(count * sizeof(float) + sizeof(float));
	if (out == NULL)
	{
		RldObj_Memory();
		return NULL;
	}
	if ((count != 0u) && (cgltf_accessor_unpack_floats(accessor, out, count) != count))
	{
		free(out);
		snprintf(why, sizeof(why), "%s cannot be read", what);
		RldGltf_Broken(Rld_LastComponent(r->path), why);
		return NULL;
	}
	return out;
}

// Room for `corners` more corners and `faces` more faces. 0 = said.
static int RldGltf_FaceRoom(struct RldObjModel *m, u32 corners, u32 faces)
{
	const u32 needCorners = m->cornerCount + corners;
	const u32 needFaces = m->faceCount + faces;

	if (needCorners > m->cornerCapacity)
	{
		u32 *v = (u32 *)realloc(m->cornerV, (size_t)needCorners * sizeof(u32));
		u32 *t;
		u32 *n;

		if (v != NULL)
		{
			m->cornerV = v;
		}
		t = (u32 *)realloc(m->cornerT, (size_t)needCorners * sizeof(u32));
		if (t != NULL)
		{
			m->cornerT = t;
		}
		n = (u32 *)realloc(m->cornerN, (size_t)needCorners * sizeof(u32));
		if (n != NULL)
		{
			m->cornerN = n;
		}
		if ((v == NULL) || (t == NULL) || (n == NULL))
		{
			RldObj_Memory();
			return 0;
		}
		m->cornerCapacity = needCorners;
	}
	if (needFaces > m->faceCapacity)
	{
		u32 *first = (u32 *)realloc(m->faceFirst, (size_t)needFaces * sizeof(u32));
		u32 *count;
		u32 *name;

		if (first != NULL)
		{
			m->faceFirst = first;
		}
		count = (u32 *)realloc(m->faceCorners, (size_t)needFaces * sizeof(u32));
		if (count != NULL)
		{
			m->faceCorners = count;
		}
		name = (u32 *)realloc(m->faceName, (size_t)needFaces * sizeof(u32));
		if (name != NULL)
		{
			m->faceName = name;
		}
		if ((first == NULL) || (count == NULL) || (name == NULL))
		{
			RldObj_Memory();
			return 0;
		}
		m->faceCapacity = needFaces;
	}
	return 1;
}

// The faces of one primitive under the world matrix w (column-major), into the
// model. keyOf: per target of the mesh its key, -1 = none. 0 = said.
static int RldGltf_Primitive(struct RldGltfRead *r, const cgltf_mesh *mesh, const cgltf_primitive *p, const double w[16], int mirrored, const int *keyOf,
                             u32 node, struct RldGltfTable *vTable, struct RldGltfTable *vnTable, u32 *faces)
{
	struct RldObjModel *m = &r->m;
	const char *file = Rld_LastComponent(r->path);
	const cgltf_accessor *position = NULL;
	const cgltf_accessor *normal = NULL;
	const cgltf_accessor *texcoord = NULL;
	const cgltf_accessor *color = NULL;
	float *xyz = NULL;
	float *nrm = NULL;
	float *uv = NULL;
	float *rgb = NULL;
	float *target[RLDGLTF_KEY_COUNT];
	u32 *vOf = NULL;
	u32 *vtOf = NULL;
	u32 *vnOf = NULL;
	u32 colorComponents = 3;
	u32 wantTexcoord = 0;
	u32 name = RLDMK_NONE;
	u32 count;
	u32 tris = 0;
	u32 i;
	int ok = 1;
	int k;

	memset(target, 0, sizeof(target));
	if (p->has_draco_mesh_compression)
	{
		RldMk_Say("error", "model-gltf-unsupported", "KHR_draco_mesh_compression",
		          "%s has compressed meshes (Draco), which rldpack does not read. Export it from Blender without compression.", file);
		return 0;
	}
	if (p->type != cgltf_primitive_type_triangles)
	{
		static const char *const modes[] = {"invalid", "points", "lines", "line loops", "line strips", "triangles", "triangle strips", "triangle fans"};
		const char *mode = ((u32)p->type < 8u) ? modes[p->type] : "unknown";

		RldMk_Say("error", "model-gltf-mode", mode,
		          "The mesh \"%s\" of %s is made of %s, not of triangles; rldpack reads triangles only. Export the model again from Blender.",
		          (mesh->name != NULL) ? mesh->name : "-", file, mode);
		return 0;
	}
	if (p->material != NULL)
	{
		const cgltf_texture_view *view = &p->material->pbr_metallic_roughness.base_color_texture;

		name = RldGltf_Material(r, p->material);
		if (name == RLDMK_NONE)
		{
			return 0;
		}
		wantTexcoord = (view->texture != NULL) ? (u32)view->texcoord : 0u;
	}
	else
	{
		r->noMaterial++;
	}

	for (i = 0; i < (u32)p->attributes_count; i++)
	{
		const cgltf_attribute *a = &p->attributes[i];

		if ((a->type == cgltf_attribute_type_joints) || (a->type == cgltf_attribute_type_weights))
		{
			RldMk_Say("error", "model-rig", a->name, "Rigs are not supported yet, please use shape keys");
			return 0;
		}
		if ((a->type == cgltf_attribute_type_position) && (a->index == 0))
		{
			position = a->data;
		}
		else if ((a->type == cgltf_attribute_type_normal) && (a->index == 0))
		{
			normal = a->data;
		}
		else if ((a->type == cgltf_attribute_type_texcoord) && ((u32)a->index == wantTexcoord))
		{
			texcoord = a->data;
		}
		else if ((a->type == cgltf_attribute_type_color) && (a->index == 0))
		{
			color = a->data;
		}
	}
	if (position == NULL)
	{
		RldGltf_Broken(file, "a primitive has no POSITION");
		return 0;
	}
	count = (u32)position->count;
	if (((normal != NULL) && (normal->count != position->count)) || ((texcoord != NULL) && (texcoord->count != position->count)) ||
	    ((color != NULL) && (color->count != position->count)))
	{
		RldGltf_Broken(file, "the attributes of a primitive differ in count");
		return 0;
	}
	if (color != NULL)
	{
		colorComponents = (u32)cgltf_num_components(color->type);
		if ((colorComponents != 3u) && (colorComponents != 4u))
		{
			RldGltf_Broken(file, "COLOR_0 is not VEC3 or VEC4");
			return 0;
		}
	}

	xyz = RldGltf_Floats(r, position, 3u, "POSITION");
	ok = (xyz != NULL);
	if (ok && (normal != NULL))
	{
		ok = ((nrm = RldGltf_Floats(r, normal, 3u, "NORMAL")) != NULL);
	}
	if (ok && (texcoord != NULL))
	{
		ok = ((uv = RldGltf_Floats(r, texcoord, 2u, "TEXCOORD")) != NULL);
	}
	if (ok && (color != NULL))
	{
		ok = ((rgb = RldGltf_Floats(r, color, colorComponents, "COLOR_0")) != NULL);
	}
	// The deltas of the targets that are keys (POSITION only); the first target of a key counts.
	for (i = 0; ok && (keyOf != NULL) && (i < (u32)p->targets_count) && (i < (u32)mesh->primitives[0].targets_count); i++)
	{
		const cgltf_morph_target *t = &p->targets[i];
		u32 a;

		if ((keyOf[i] < 0) || (target[keyOf[i]] != NULL))
		{
			continue;
		}
		for (a = 0; a < (u32)t->attributes_count; a++)
		{
			if ((t->attributes[a].type == cgltf_attribute_type_position) && (t->attributes[a].data != NULL))
			{
				if (t->attributes[a].data->count != position->count)
				{
					RldGltf_Broken(file, "a shape key has another count than its mesh");
					ok = 0;
					break;
				}
				target[keyOf[i]] = RldGltf_Floats(r, t->attributes[a].data, 3u, "a shape key");
				ok = (target[keyOf[i]] != NULL);
				break;
			}
		}
	}

	if (ok)
	{
		vOf = (u32 *)malloc((size_t)count * sizeof(u32) + 4u);
		vtOf = (u32 *)malloc((size_t)count * sizeof(u32) + 4u);
		vnOf = (u32 *)malloc((size_t)count * sizeof(u32) + 4u);
		ok = (vOf != NULL) && (vtOf != NULL) && (vnOf != NULL);
		if (!ok)
		{
			RldObj_Memory();
		}
	}
	for (k = 0; ok && (k < RLDGLTF_KEY_COUNT); k++)
	{
		if ((target[k] != NULL) && (r->vDelta == NULL))
		{
			// The first key: every v made so far moves by 0.
			r->vDelta = (double *)calloc((size_t)m->vCapacity * RLDGLTF_KEY_COUNT * 3u + 1u, sizeof(double));
			if (r->vDelta == NULL)
			{
				RldObj_Memory();
				ok = 0;
			}
		}
		if (target[k] != NULL)
		{
			r->present |= 1u << k;
		}
	}

	// The vertices: placed by the node, their v, vt and vn.
	for (i = 0; ok && (i < count); i++)
	{
		double place[3];
		double color3[3] = {0.0, 0.0, 0.0};
		double delta[RLDGLTF_KEY_COUNT * 3];
		int colored = 0;
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			place[axis] = w[axis] * (double)xyz[3u * i] + w[4 + axis] * (double)xyz[3u * i + 1u] + w[8 + axis] * (double)xyz[3u * i + 2u] + w[12 + axis];
		}
		if (rgb != NULL)
		{
			const float *c = &rgb[colorComponents * i];

			colored = !((c[0] == 1.0f) && (c[1] == 1.0f) && (c[2] == 1.0f));
			if (colored)
			{
				for (axis = 0; axis < 3; axis++)
				{
					color3[axis] = RldGltf_Srgb((double)c[axis], &r->clamped);
				}
			}
		}
		if (r->vDelta != NULL)
		{
			memset(delta, 0, sizeof(delta));
			for (k = 0; k < RLDGLTF_KEY_COUNT; k++)
			{
				if (target[k] != NULL)
				{
					const float *d = &target[k][3u * i];

					for (axis = 0; axis < 3; axis++)
					{
						delta[3 * k + axis] = w[axis] * (double)d[0] + w[4 + axis] * (double)d[1] + w[8 + axis] * (double)d[2];
					}
				}
			}
		}
		vOf[i] = RldGltf_Vertex(r, vTable, place, color3, colored, (r->vDelta != NULL) ? delta : NULL);
		ok = (vOf[i] != RLDMK_NONE);
		vtOf[i] = RLDMK_NONE;
		vnOf[i] = RLDMK_NONE;
		if (ok && (uv != NULL))
		{
			const double at[2] = {(double)uv[2u * i], 1.0 - (double)uv[2u * i + 1u]};

			vtOf[i] = RldGltf_TexCoord(r, at);
			ok = (vtOf[i] != RLDMK_NONE);
		}
		if (ok && (nrm != NULL))
		{
			vnOf[i] = RldGltf_Normal(r, vnTable, &nrm[3u * i], node);
			ok = (vnOf[i] != RLDMK_NONE);
		}
	}

	// The triangles, turned when the node mirrors.
	if (ok)
	{
		const cgltf_size corners = (p->indices != NULL) ? p->indices->count : (cgltf_size)count;

		if ((p->indices != NULL) && p->indices->is_sparse)
		{
			RldGltf_Broken(file, "sparse indices");
			ok = 0;
		}
		else if ((corners % 3u) != 0u)
		{
			RldGltf_Broken(file, "a triangle list does not end with a whole triangle");
			ok = 0;
		}
		else if (((u64)m->cornerCount + (u64)corners) > RLDOBJ_CORNERS_MAX)
		{
			RldObj_Big("face corners", RLDOBJ_CORNERS_MAX);
			ok = 0;
		}
		else if (((u64)m->faceCount + (u64)(corners / 3u)) > RLDMK_PLY_MAX)
		{
			RldObj_Big("faces", RLDMK_PLY_MAX);
			ok = 0;
		}
		tris = (u32)(corners / 3u);
		ok = ok && RldGltf_FaceRoom(m, 3u * tris, tris);
	}
	for (i = 0; ok && (i < tris); i++)
	{
		u32 corner[3];
		int c;

		for (c = 0; c < 3; c++)
		{
			const cgltf_size at = (cgltf_size)3u * i + (cgltf_size)(mirrored ? ((3 - c) % 3) : c);

			corner[c] = (p->indices != NULL) ? (u32)cgltf_accessor_read_index(p->indices, at) : (u32)at;
			if (corner[c] >= count)
			{
				RldGltf_Broken(file, "an index points past its vertices");
				ok = 0;
				break;
			}
		}
		if (!ok)
		{
			break;
		}
		m->faceFirst[m->faceCount] = m->cornerCount;
		m->faceCorners[m->faceCount] = 3u;
		m->faceName[m->faceCount] = name;
		m->faceCount++;
		for (c = 0; c < 3; c++)
		{
			m->cornerV[m->cornerCount] = vOf[corner[c]];
			m->cornerT[m->cornerCount] = vtOf[corner[c]];
			m->cornerN[m->cornerCount] = vnOf[corner[c]];
			m->cornerCount++;
		}
		(*faces)++;
	}

	free(xyz);
	free(nrm);
	free(uv);
	free(rgb);
	for (k = 0; k < RLDGLTF_KEY_COUNT; k++)
	{
		free(target[k]);
	}
	free(vOf);
	free(vtOf);
	free(vnOf);
	return ok;
}

// The local matrix of a node in double, column-major.
static void RldGltf_Local(const cgltf_node *node, double out[16])
{
	int i;

	if (node->has_matrix)
	{
		for (i = 0; i < 16; i++)
		{
			out[i] = (double)node->matrix[i];
		}
		return;
	}
	{
		const double x = node->has_rotation ? (double)node->rotation[0] : 0.0;
		const double y = node->has_rotation ? (double)node->rotation[1] : 0.0;
		const double z = node->has_rotation ? (double)node->rotation[2] : 0.0;
		const double q = node->has_rotation ? (double)node->rotation[3] : 1.0;
		const double sx = node->has_scale ? (double)node->scale[0] : 1.0;
		const double sy = node->has_scale ? (double)node->scale[1] : 1.0;
		const double sz = node->has_scale ? (double)node->scale[2] : 1.0;

		out[0] = (1.0 - 2.0 * (y * y + z * z)) * sx;
		out[1] = (2.0 * (x * y + z * q)) * sx;
		out[2] = (2.0 * (x * z - y * q)) * sx;
		out[3] = 0.0;
		out[4] = (2.0 * (x * y - z * q)) * sy;
		out[5] = (1.0 - 2.0 * (x * x + z * z)) * sy;
		out[6] = (2.0 * (y * z + x * q)) * sy;
		out[7] = 0.0;
		out[8] = (2.0 * (x * z + y * q)) * sz;
		out[9] = (2.0 * (y * z - x * q)) * sz;
		out[10] = (1.0 - 2.0 * (x * x + y * y)) * sz;
		out[11] = 0.0;
		out[12] = node->has_translation ? (double)node->translation[0] : 0.0;
		out[13] = node->has_translation ? (double)node->translation[1] : 0.0;
		out[14] = node->has_translation ? (double)node->translation[2] : 0.0;
		out[15] = 1.0;
	}
}

// out = a x b (column-major).
static void RldGltf_Multiply(const double a[16], const double b[16], double out[16])
{
	int column;
	int row;

	for (column = 0; column < 4; column++)
	{
		for (row = 0; row < 4; row++)
		{
			out[4 * column + row] = a[row] * b[4 * column] + a[4 + row] * b[4 * column + 1] + a[8 + row] * b[4 * column + 2] + a[12 + row] * b[4 * column + 3];
		}
	}
}

// A node with a mesh: its group, its targets' keys, its primitives. 0 = said.
static int RldGltf_Node(struct RldGltfRead *r, const cgltf_node *node, const double w[16], struct RldGltfTable *vTable, struct RldGltfTable *vnTable)
{
	struct RldObjModel *m = &r->m;
	const cgltf_mesh *mesh = node->mesh;
	const double det = w[0] * (w[5] * w[10] - w[9] * w[6]) - w[4] * (w[1] * w[10] - w[9] * w[2]) + w[8] * (w[1] * w[6] - w[5] * w[2]);
	const u32 index = (u32)cgltf_node_index(r->data, node);
	int *keys = NULL;
	u32 targets = 0;
	u32 faces = 0;
	u32 vertices = 0;
	u32 i;
	int ok = 1;

	if (node->skin != NULL)
	{
		RldMk_Say("error", "model-rig", (node->name != NULL) ? node->name : NULL, "Rigs are not supported yet, please use shape keys");
		return 0;
	}
	for (i = 0; i < (u32)mesh->primitives_count; i++)
	{
		const cgltf_attribute *a = mesh->primitives[i].attributes;
		u32 n;

		for (n = 0; n < (u32)mesh->primitives[i].attributes_count; n++)
		{
			if ((a[n].type == cgltf_attribute_type_position) && (a[n].data != NULL) && (a[n].data->count <= RLDMK_PLY_MAX))
			{
				vertices += (u32)a[n].data->count;
			}
		}
	}
	if (!RldGltf_TableStart(vTable, vertices) || !RldGltf_TableStart(vnTable, vertices))
	{
		RldObj_Memory();
		return 0;
	}

	// The names of its targets: a key each, or said once.
	if ((mesh->primitives_count != 0u) && r->morphs && (mesh->primitives[0].targets_count != 0u))
	{
		targets = (u32)mesh->primitives[0].targets_count;
		keys = (int *)malloc((size_t)targets * sizeof(int));
		if (keys == NULL)
		{
			RldObj_Memory();
			return 0;
		}
		for (i = 0; i < targets; i++)
		{
			const char *name = NULL;
			char numbered[32];
			u32 a;

			if ((i < (u32)mesh->target_names_count) && (mesh->target_names[i] != NULL))
			{
				name = mesh->target_names[i];
			}
			for (a = 0; (name == NULL) && (a < (u32)mesh->primitives[0].targets[i].attributes_count); a++)
			{
				const cgltf_attribute *at = &mesh->primitives[0].targets[i].attributes[a];

				if ((at->type == cgltf_attribute_type_position) && (at->data != NULL) && (at->data->name != NULL))
				{
					name = at->data->name;
				}
			}
			if (name == NULL)
			{
				snprintf(numbered, sizeof(numbered), "target %u", i);
				name = numbered;
			}
			keys[i] = RldGltf_SayName(r, name);
		}
	}

	if (m->groupCount < RLDOBJ_GROUPS_LISTED)
	{
		const char *name = ((node->name != NULL) && (node->name[0] != '\0')) ? node->name : ((mesh->name != NULL) ? mesh->name : "");

		ok = RldGltf_Room((void **)&m->groups, m->groupCount + 1u, &m->groupCapacity, sizeof(*m->groups));
		if (ok)
		{
			m->groups[m->groupCount].name = RldObj_Copy(name, strlen(name), RLDOBJ_NAME_MAX);
			m->groups[m->groupCount].faces = 0;
			ok = (m->groups[m->groupCount].name != NULL);
			m->groupCount += ok ? 1u : 0u;
		}
		if (!ok)
		{
			RldObj_Memory();
		}
	}
	else
	{
		m->groupsMore++;
	}

	for (i = 0; ok && (i < (u32)mesh->primitives_count); i++)
	{
		ok = RldGltf_Primitive(r, mesh, &mesh->primitives[i], w, det < 0.0, keys, index, vTable, vnTable, &faces);
	}
	if (ok && (m->groupsMore == 0u) && (m->groupCount != 0u))
	{
		m->groups[m->groupCount - 1u].faces = faces;
	}
	free(keys);
	return ok;
}

// Every node of the scene with a mesh, depth first in the file's order. 0 = said.
static int RldGltf_Scene(struct RldGltfRead *r)
{
	static const double identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const cgltf_data *data = r->data;
	const cgltf_scene *scene = (data->scene != NULL) ? data->scene : ((data->scenes_count != 0u) ? &data->scenes[0] : NULL);
	struct RldGltfTable vTable;
	struct RldGltfTable vnTable;
	const cgltf_node *stack[RLDGLTF_DEPTH];
	u32 child[RLDGLTF_DEPTH];
	double world[RLDGLTF_DEPTH][16];
	const u32 roots = (scene != NULL) ? (u32)scene->nodes_count : (u32)data->nodes_count;
	u32 root;
	int ok = 1;

	memset(&vTable, 0, sizeof(vTable));
	memset(&vnTable, 0, sizeof(vnTable));
	for (root = 0; ok && (root < roots); root++)
	{
		const cgltf_node *top = (scene != NULL) ? scene->nodes[root] : &data->nodes[root];
		u32 depth = 0;
		double local[16];

		if ((top == NULL) || ((scene == NULL) && (top->parent != NULL)))
		{
			continue;
		}
		stack[0] = top;
		child[0] = 0;
		RldGltf_Local(top, local);
		RldGltf_Multiply(identity, local, world[0]);
		if (top->mesh != NULL)
		{
			ok = RldGltf_Node(r, top, world[0], &vTable, &vnTable);
		}
		while (ok)
		{
			const cgltf_node *node = stack[depth];

			if (child[depth] >= (u32)node->children_count)
			{
				if (depth == 0u)
				{
					break;
				}
				depth--;
				continue;
			}
			if ((depth + 1u) >= RLDGLTF_DEPTH)
			{
				RldGltf_Broken(Rld_LastComponent(r->path), "nodes nested too deep");
				ok = 0;
				break;
			}
			stack[depth + 1u] = node->children[child[depth]++];
			child[depth + 1u] = 0;
			depth++;
			RldGltf_Local(stack[depth], local);
			RldGltf_Multiply(world[depth - 1u], local, world[depth]);
			if (stack[depth]->mesh != NULL)
			{
				ok = RldGltf_Node(r, stack[depth], world[depth], &vTable, &vnTable);
			}
		}
	}
	free(vTable.slots);
	free(vnTable.slots);
	return ok;
}

//========================================================================================
// THE STEPS AFTER THE READ: those of the OBJ reader
//========================================================================================

// The deltas of every ply point. The faces of the ply come in the order of
// the model's triangles; the pieces of a split triangle follow each other and
// share their faceSource, so the runs of faceSource are the triangles in
// order. A point at a corner takes that corner's delta, any other the
// barycentric mix of the three. 0 = out of memory (said).
static int RldGltf_PointDeltas(const struct RldMkPly *ply, const double *xyz, const u32 *triV, u32 triCount, const double *vDelta, u32 present)
{
	u8 *done = (u8 *)calloc((size_t)ply->vertexCount + 1u, 1u);
	u32 run = RLDMK_NONE;
	u32 last = RLDMK_NONE;
	u32 f;
	int k;

	if (done == NULL)
	{
		RldObj_Memory();
		return 0;
	}
	for (k = 0; k < RLDGLTF_KEY_COUNT; k++)
	{
		if ((present & (1u << k)) != 0u)
		{
			s_rldGltfMorphs.point[k] = (double *)calloc((size_t)ply->vertexCount * 3u + 1u, sizeof(double));
			if (s_rldGltfMorphs.point[k] == NULL)
			{
				free(done);
				RldObj_Memory();
				return 0;
			}
		}
	}
	for (f = 0; f < ply->faceCount; f++)
	{
		const u32 source = (ply->faceSource != NULL) ? ply->faceSource[f] : f;
		const u32 *v;
		u32 c;

		if ((f == 0u) || (source != last))
		{
			run = (run == RLDMK_NONE) ? 0u : (run + 1u);
			last = source;
		}
		if (run >= triCount)
		{
			break;
		}
		v = &triV[3u * run];
		for (c = 0; (c < ply->cornerCount[f]) && (c < 4u); c++)
		{
			const u32 point = ply->corners[4u * f + c];
			const double *at = &ply->xyz[3u * point];
			double weight[3] = {1.0, 0.0, 0.0};
			int corner;
			int axis;

			if (done[point])
			{
				continue;
			}
			done[point] = 1u;
			for (corner = 0; corner < 3; corner++)
			{
				if (memcmp(&xyz[3u * v[corner]], at, 3u * sizeof(double)) == 0)
				{
					break;
				}
			}
			if (corner < 3)
			{
				weight[0] = 0.0;
				weight[corner] = 1.0;
			}
			else
			{
				double e0[3];
				double e1[3];
				double d[3];
				double d00 = 0.0;
				double d01 = 0.0;
				double d11 = 0.0;
				double d20 = 0.0;
				double d21 = 0.0;
				double below;

				for (axis = 0; axis < 3; axis++)
				{
					e0[axis] = xyz[3u * v[1] + (u32)axis] - xyz[3u * v[0] + (u32)axis];
					e1[axis] = xyz[3u * v[2] + (u32)axis] - xyz[3u * v[0] + (u32)axis];
					d[axis] = at[axis] - xyz[3u * v[0] + (u32)axis];
					d00 += e0[axis] * e0[axis];
					d01 += e0[axis] * e1[axis];
					d11 += e1[axis] * e1[axis];
					d20 += d[axis] * e0[axis];
					d21 += d[axis] * e1[axis];
				}
				below = d00 * d11 - d01 * d01;
				if (below != 0.0)
				{
					weight[1] = (d11 * d20 - d01 * d21) / below;
					weight[2] = (d00 * d21 - d01 * d20) / below;
					weight[0] = 1.0 - weight[1] - weight[2];
				}
			}
			for (k = 0; k < RLDGLTF_KEY_COUNT; k++)
			{
				if (s_rldGltfMorphs.point[k] == NULL)
				{
					continue;
				}
				for (axis = 0; axis < 3; axis++)
				{
					s_rldGltfMorphs.point[k][3u * point + (u32)axis] = weight[0] * vDelta[((size_t)v[0] * RLDGLTF_KEY_COUNT + (u32)k) * 3u + (u32)axis] +
					                                                    weight[1] * vDelta[((size_t)v[1] * RLDGLTF_KEY_COUNT + (u32)k) * 3u + (u32)axis] +
					                                                    weight[2] * vDelta[((size_t)v[2] * RLDGLTF_KEY_COUNT + (u32)k) * 3u + (u32)axis];
				}
			}
		}
	}
	free(done);
	return 1;
}

// What RldObj_Parse does after its reading (rldpack_obj.inc, STEP 1 FOR OBJ),
// for the model made here: every name is a defined material in use, the one
// "MTL file" is the glTF file itself. The two are kept in step. 0 = said.
static int RldGltf_Finish(struct RldGltfRead *r, struct RldMkPly *ply, struct RldMkModelInfo *info)
{
	static char libraryPath[RLDOBJ_LIBRARIES][RLD_PATH_MAX];
	struct RldObjModel *m = &r->m;
	const u32 triCount = m->faceCount;
	const u32 vCount = m->vCount;
	u8 *cornerRgb = NULL;
	u8 *cornerSource = NULL;
	u8 *nameUsed = NULL;
	u32 *triV = NULL;
	size_t kept = 0;
	u32 i;
	int ok;

	snprintf(libraryPath[0], sizeof(libraryPath[0]), "%s", r->path);
	m->nameMaterial = (u32 *)malloc((size_t)m->names.count * sizeof(u32) + 4u);
	nameUsed = (u8 *)malloc((size_t)m->names.count + 1u);
	cornerRgb = (u8 *)malloc((size_t)m->cornerCount * 3u + 1u);
	cornerSource = (u8 *)malloc((size_t)m->cornerCount + 1u);
	triV = (u32 *)malloc((size_t)m->cornerCount * sizeof(u32) + 4u);
	ok = (m->nameMaterial != NULL) && (nameUsed != NULL) && (cornerRgb != NULL) && (cornerSource != NULL) && (triV != NULL);
	if (!ok)
	{
		RldObj_Memory();
	}
	for (i = 0; ok && (i < m->names.count); i++)
	{
		m->nameMaterial[i] = i;
		nameUsed[i] = 1u;
	}
	if (ok)
	{
		info->materials = m->names.count;
		memcpy(triV, m->cornerV, (size_t)m->cornerCount * sizeof(u32));
		RldObj_UniformColors(m, nameUsed);
		if (m->uniform)
		{
			memset(m->vColored, 0, m->vCount);
		}
		RldObj_VertexColors(m);
		RldObj_BaseColors(m, cornerRgb, cornerSource);
		ok = RldObj_Textures(m, r->path, libraryPath, nameUsed, cornerRgb, cornerSource, info, &kept);
	}

	if (ok && s_rldObjNativeWant && (info->role == RLDMK_ROLE_MAIN))
	{
		ok = RldObj_CaptureNative(m, &s_rldObjNative);
	}
	if (info->role == RLDMK_ROLE_CHAR_WHEEL)
	{
		ok = ok && RldObj_CaptureNative(m, &s_rldObjWheel);
		free(cornerRgb);
		free(cornerSource);
		free(nameUsed);
		free(triV);
		return ok;
	}

	if (ok && (kept != 0u))
	{
		ok = RldObj_Detail(m);
		if (ok)
		{
			free(cornerRgb);
			free(cornerSource);
			cornerRgb = (u8 *)malloc((size_t)m->cornerCount * 3u + 1u);
			cornerSource = (u8 *)malloc((size_t)m->cornerCount + 1u);
			ok = (cornerRgb != NULL) && (cornerSource != NULL);
			if (!ok)
			{
				RldObj_Memory();
			}
		}
		if (ok)
		{
			RldObj_BaseColors(m, cornerRgb, cornerSource);
		}
		for (i = 0; ok && (i < m->textureCount); i++)
		{
			if (m->textures[i].kept)
			{
				RldObj_TextureDone(m, i, (m->textures[i].shown != NULL) ? m->textures[i].shown : m->textures[i].path, cornerRgb, cornerSource, info);
			}
		}
	}
	ok = ok && RldObj_Points(m, cornerRgb, ply);

	// The shape keys: per v of the source, per point of the ply.
	if (ok && r->morphs)
	{
		int k;

		s_rldGltfMorphs.present = r->present;
		s_rldGltfMorphs.sourceCount = vCount;
		s_rldGltfMorphs.pointCount = ply->vertexCount;
		for (k = 0; ok && (k < RLDGLTF_KEY_COUNT); k++)
		{
			if ((r->present & (1u << k)) == 0u)
			{
				continue;
			}
			s_rldGltfMorphs.source[k] = (double *)malloc((size_t)vCount * 3u * sizeof(double) + sizeof(double));
			ok = (s_rldGltfMorphs.source[k] != NULL);
			for (i = 0; ok && (i < vCount); i++)
			{
				memcpy(&s_rldGltfMorphs.source[k][3u * i], &r->vDelta[((size_t)i * RLDGLTF_KEY_COUNT + (u32)k) * 3u], 3u * sizeof(double));
			}
			if (!ok)
			{
				RldObj_Memory();
			}
		}
		if (ok && (r->present != 0u))
		{
			ok = RldGltf_PointDeltas(ply, m->xyz, triV, triCount, r->vDelta, r->present);
		}
	}

	if (ok)
	{
		int kdIgnored = 0;

		for (i = 0; i < m->cornerCount; i++)
		{
			info->colors |= cornerSource[i];
		}
		for (i = 0; i < m->names.count; i++)
		{
			kdIgnored |= m->materials[i].hasKd;
		}
		if ((m->colored != 0u) && kdIgnored && !m->uniform)
		{
			RldMk_Say("info", "obj-vertex-colors", NULL, "The model has vertex colors (COLOR_0): they are used, and the materials' base colors are ignored.");
		}
		if (r->transparent != 0u)
		{
			RldMk_Say("info", "obj-transparency", NULL,
			          "%u material%s in use %s transparent (alphaMode MASK or BLEND, or an alpha below 1); the game draws the model opaque.", r->transparent,
			          (r->transparent == 1u) ? "" : "s", (r->transparent == 1u) ? "is" : "are");
		}
		if (m->modulated != 0u)
		{
			RldMk_Say("info", "obj-vertex-modulation", s_rldObjVertexWords[s_rldObjOptions.vertexColors],
			          "On %u textured corners the vertex color modulates the texture as on the PS1: 0x80 shows the texture as it is (--vertex-colors color; "
			          "Reload Studio: Vertex colors: Plain color).",
			          m->modulated);
		}
		if (r->clamped != 0u)
		{
			RldMk_Say("warning", "obj-color-range", NULL, "%u color values of the model were outside 0..1 and were clamped.", r->clamped);
		}
		if (r->rotated != 0u)
		{
			RldMk_Say("warning", "gltf-texture-rotation", NULL, "%u texture transforms rotate the texture; the rotation is left out.", r->rotated);
		}
		if (info->noUv != 0u)
		{
			RldMk_Say("warning", "tex-no-uv", NULL, "%u corners of textured faces have no texture coordinates (TEXCOORD); they keep the vertex or material color.",
			          info->noUv);
		}
		if (info->colors == RLDMK_COLORS_GREY)
		{
			RldMk_Say("warning", "obj-no-colors", NULL, "The model has no vertex colors, no material colors and no textures, so it is drawn in one grey.");
		}
		for (i = 0; i < m->groupCount; i++)
		{
			char faces[16];

			snprintf(faces, sizeof(faces), "%u", m->groups[i].faces);
			RldMk_EmitModel("group", faces, (m->groups[i].name[0] != '\0') ? m->groups[i].name : "-", NULL, NULL);
		}
		if (m->groupsMore != 0u)
		{
			RldMk_Say("info", "obj-groups", NULL, "%u more meshes are not listed.", m->groupsMore);
		}
		info->groups = m->groupCount + m->groupsMore;
		info->points = ply->vertexCount;
		// Every corner has its color by the rule, grey included (as for an OBJ).
		ply->hasColors = 1;
	}

	free(cornerRgb);
	free(cornerSource);
	free(nameUsed);
	free(triV);
	return ok;
}

//========================================================================================
// STEP 1 FOR glTF
//========================================================================================

// A GLB (its magic, any version: cgltf says what is wrong with an old one) or
// a .gltf: JSON with "asset" near its start.
static int RldGltf_Detect(const u8 *data, size_t size)
{
	const size_t window = (size < RLDOBJ_DETECT_BYTES) ? size : RLDOBJ_DETECT_BYTES;
	size_t at = 0;
	size_t i;

	if ((size >= 4u) && (memcmp(data, "glTF", 4) == 0))
	{
		return 1;
	}
	if ((size >= 3u) && (data[0] == 0xEFu) && (data[1] == 0xBBu) && (data[2] == 0xBFu))
	{
		at = 3;
	}
	while ((at < window) && RldMk_IsSpace(data[at]))
	{
		at++;
	}
	if ((at >= window) || (data[at] != '{'))
	{
		return 0;
	}
	for (i = at; (i + 7u) <= window; i++)
	{
		if (memcmp(&data[i], "\"asset\"", 7) == 0)
		{
			return 1;
		}
	}
	return 0;
}

// The glTF file `data` at `path` (buffer files and images relative to it) ->
// *ply, the facts into *info, the shape keys into s_rldGltfMorphs (RLDMK_ROLE_MAIN,
// RLDMK_ROLE_POSE). Says every error itself and returns 0 then.
static int RldGltf_Parse(const char *path, const u8 *data, size_t size, struct RldMkPly *ply, struct RldMkModelInfo *info)
{
	const char *file = Rld_LastComponent(path);
	struct RldGltfRead r;
	cgltf_options options;
	cgltf_result result;
	u32 i;
	int ok = 1;

	memset(&r, 0, sizeof(r));
	memset(ply, 0, sizeof(*ply));
	memset(&options, 0, sizeof(options));
	r.path = path;
	r.morphs = (info->role == RLDMK_ROLE_MAIN) || (info->role == RLDMK_ROLE_POSE);
	r.m.group = RLDMK_NONE;
	options.file.read = RldGltf_FileRead;
	options.file.release = RldGltf_FileRelease;

	if (!RldGltf_StartFiles())
	{
		RldObj_Memory();
		return 0;
	}
	result = cgltf_parse(&options, data, size, &r.data);
	if (result == cgltf_result_success)
	{
		result = cgltf_load_buffers(&options, r.data, path);
	}
	if (result == cgltf_result_success)
	{
		result = cgltf_validate(r.data);
	}
	if (result != cgltf_result_success)
	{
		RldGltf_Broken(file, RldGltf_ResultWord(result));
		cgltf_free(r.data);
		return 0;
	}

	// Refused before anything is made: a rig, compressed buffers.
	if (r.data->skins_count != 0u)
	{
		char skins[24];

		snprintf(skins, sizeof(skins), "%u skin%s", (u32)r.data->skins_count, (r.data->skins_count == 1u) ? "" : "s");
		RldMk_Say("error", "model-rig", skins, "Rigs are not supported yet, please use shape keys");
		ok = 0;
	}
	for (i = 0; ok && (i < (u32)r.data->buffer_views_count); i++)
	{
		if (r.data->buffer_views[i].has_meshopt_compression)
		{
			RldMk_Say("error", "model-gltf-unsupported", "EXT_meshopt_compression",
			          "%s has compressed buffers (meshopt), which rldpack does not read. Export it from Blender without compression.", file);
			ok = 0;
		}
	}

	if (ok)
	{
		r.materialName = (u32 *)malloc(r.data->materials_count * sizeof(u32) + 4u);
		ok = (r.materialName != NULL);
		if (!ok)
		{
			RldObj_Memory();
		}
		else
		{
			memset(r.materialName, 0xFF, r.data->materials_count * sizeof(u32));
		}
	}
	ok = ok && RldGltf_Scene(&r);
	if (ok && (r.m.faceCount == 0u))
	{
		RldMk_Say("error", "model-gltf-empty", NULL, "%s has no mesh with triangles in its scene. Export the model again from Blender (glTF 2.0, .glb).", file);
		ok = 0;
	}
	if (ok && (r.otherTexCoord != 0u))
	{
		RldMk_Say("info", "gltf-texcoord", NULL, "%u material%s take their texture coordinates from another set than the first (TEXCOORD_0); that set is used.",
		          r.otherTexCoord, (r.otherTexCoord == 1u) ? "" : "s");
	}
	ok = ok && RldGltf_Finish(&r, ply, info);
	if (ok && r.morphs)
	{
		s_rldGltfMorphs.read = 1;
	}

	cgltf_free(r.data);
	free(r.vDelta);
	free(r.vnKey);
	free(r.vnNode);
	free(r.vtSlots);
	free(r.materialName);
	RldObj_FreeModel(&r.m);
	if (!ok)
	{
		if (r.morphs)
		{
			RldGltf_FreeMorphs();
		}
		RldMk_FreePly(ply);
	}
	return ok;
}
