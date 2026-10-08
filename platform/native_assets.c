#include "platform/native_assets.h"

#include <macros.h>

// The .rldtrack format: SHA-256, reader. tools/rldpack.c pulls in the same
// file - there with RLDTRACK_WITH_BUILDER, here without it. The game reads
// and checks, it does not write.
#include <rldtrack.inc>

#include <platform/native_disc_image.h>
#include <platform/native_path.h>

#if defined(_WIN32)
#include <platform/native_win32.h>
#else
#include <dirent.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_ASSETS_PATH_MAX                   1024
#define NATIVE_ASSETS_DIR_NAME                   "assets"
#define NATIVE_ASSETS_BIGFILE_PATH               "BIGFILE.BIG"
#define NATIVE_ASSETS_KART_HWL_PATH              "SOUNDS/KART.HWL"
#define NATIVE_ASSETS_TEST_STR_PATH              "TEST.STR"
#define NATIVE_ASSETS_XNF_PATH                   "XA/ENG.XNF"
#define NATIVE_ASSETS_DISC_PATH                  "ctr-u.bin"

#define NATIVE_ASSETS_XA_TYPE_COUNT              3
#define NATIVE_ASSETS_XA_HEADER_SIZE             0x44
#define NATIVE_ASSETS_XA_NUM_XAS_TOTAL_OFFSET    0x0c
#define NATIVE_ASSETS_XA_NUM_TRACKS_TOTAL_OFFSET 0x10
#define NATIVE_ASSETS_XA_NUM_SONGS_OFFSET        0x2c
#define NATIVE_ASSETS_XA_FIRST_SONG_INDEX_OFFSET 0x38
#define NATIVE_ASSETS_XA_ENTRY_BYTES             4
#define NATIVE_ASSETS_XA_MAX_FILE_NUMBER         256

struct NativeAssetIndexEntry
{
	char *key;
	char *path;
};

global_variable char s_nativeAssetsBaseDir[NATIVE_ASSETS_PATH_MAX] = ".";
global_variable char s_nativeAssetsDir[NATIVE_ASSETS_PATH_MAX] = NATIVE_ASSETS_DIR_NAME;
global_variable int s_nativeAssetsInitialized;
global_variable struct NativeAssetIndexEntry *s_nativeAssetIndex;
global_variable int s_nativeAssetIndexCount;
global_variable int s_nativeAssetIndexCapacity;
global_variable int s_nativeAssetIndexBuilt;

internal int NativeAssets_FileExistsHost(const char *path)
{
	if (path == NULL)
	{
		return 0;
	}

	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		return 0;
	}

	fclose(file);
	return 1;
}

internal int NativeAssets_DirectoryExistsHost(const char *path)
{
#if defined(_WIN32)
	DWORD attributes;

	if (path == NULL)
		return 0;

	attributes = GetFileAttributesA(path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
	DIR *dir = opendir(path);

	if (dir == NULL)
	{
		return 0;
	}

	closedir(dir);
	return 1;
#endif
}

internal int NativeAssets_FindHostChildCaseInsensitive(NativeStr8 parent, NativeStr8 child, char *dst, size_t dstSize)
{
	char parentPath[NATIVE_ASSETS_PATH_MAX];
	char match[NATIVE_ASSETS_PATH_MAX];

	if (!NativePath_NormalizeSlashes(parentPath, sizeof(parentPath), parent))
	{
		return 0;
	}

#if defined(_WIN32)
	{
		char searchPath[NATIVE_ASSETS_PATH_MAX];
		WIN32_FIND_DATAA findData;
		HANDLE findHandle;

		if (!NativePath_Join(searchPath, sizeof(searchPath), NativeStr8_FromCString(parentPath), NATIVE_STR8_LIT("*")))
			return 0;

		findHandle = FindFirstFileA(searchPath, &findData);
		if (findHandle == INVALID_HANDLE_VALUE)
			return 0;

		match[0] = '\0';
		do
		{
			NativeStr8 entryName = NativeStr8_FromCString(findData.cFileName);

			if (NativeStr8_Equals(entryName, child))
			{
				if (!NativeStr8_CopyToCString(match, sizeof(match), entryName))
				{
					FindClose(findHandle);
					return 0;
				}

				break;
			}

			if ((match[0] == '\0') && NativeStr8_EqualsIgnoreCaseAscii(entryName, child))
			{
				if (!NativeStr8_CopyToCString(match, sizeof(match), entryName))
				{
					FindClose(findHandle);
					return 0;
				}
			}
		} while (FindNextFileA(findHandle, &findData) != 0);

		FindClose(findHandle);
	}
#else
	struct dirent *entry;

	DIR *dir = opendir(parentPath);
	if (dir == NULL)
	{
		return 0;
	}

	match[0] = '\0';
	while ((entry = readdir(dir)) != NULL)
	{
		NativeStr8 entryName = NativeStr8_FromCString(entry->d_name);

		if (NativeStr8_Equals(entryName, child))
		{
			if (!NativeStr8_CopyToCString(match, sizeof(match), entryName))
			{
				closedir(dir);
				return 0;
			}

			break;
		}

		if ((match[0] == '\0') && NativeStr8_EqualsIgnoreCaseAscii(entryName, child))
		{
			if (!NativeStr8_CopyToCString(match, sizeof(match), entryName))
			{
				closedir(dir);
				return 0;
			}
		}
	}

	closedir(dir);
#endif

	if (match[0] == '\0')
	{
		return 0;
	}

	return NativePath_Join(dst, dstSize, NativeStr8_FromCString(parentPath), NativeStr8_FromCString(match));
}

internal int NativeAssets_FindAssetsDir(NativeStr8 baseDir, char *dst, size_t dstSize)
{
	if (!NativePath_Join(dst, dstSize, baseDir, NATIVE_STR8_LIT(NATIVE_ASSETS_DIR_NAME)))
	{
		return 0;
	}

	if (NativeAssets_DirectoryExistsHost(dst))
	{
		return 1;
	}

	return NativeAssets_FindHostChildCaseInsensitive(baseDir, NATIVE_STR8_LIT(NATIVE_ASSETS_DIR_NAME), dst, dstSize);
}

internal char *NativeAssets_CopyCString(const char *src)
{
	if (src == NULL)
	{
		return NULL;
	}

	size_t size = strlen(src) + 1u;
	char *copy = (char *)malloc(size);
	if (copy == NULL)
	{
		return NULL;
	}

	memcpy(copy, src, size);
	return copy;
}

internal void NativeAssets_ClearIndex(void)
{
	for (int i = 0; i < s_nativeAssetIndexCount; i++)
	{
		free(s_nativeAssetIndex[i].key);
		free(s_nativeAssetIndex[i].path);
	}

	free(s_nativeAssetIndex);
	s_nativeAssetIndex = NULL;
	s_nativeAssetIndexCount = 0;
	s_nativeAssetIndexCapacity = 0;
	s_nativeAssetIndexBuilt = 0;
}

internal int NativeAssets_IndexReserve(int needed)
{
	if (needed <= s_nativeAssetIndexCapacity)
	{
		return 1;
	}

	int newCapacity = (s_nativeAssetIndexCapacity == 0) ? 256 : s_nativeAssetIndexCapacity * 2;
	while (newCapacity < needed)
	{
		newCapacity *= 2;
	}

	struct NativeAssetIndexEntry *entries = (struct NativeAssetIndexEntry *)realloc(s_nativeAssetIndex, (size_t)newCapacity * sizeof(*s_nativeAssetIndex));
	if (entries == NULL)
	{
		return 0;
	}

	s_nativeAssetIndex = entries;
	s_nativeAssetIndexCapacity = newCapacity;
	return 1;
}

internal int NativeAssets_NormalizeRelativeKey(NativeStr8 relativePath, char *dst, size_t dstSize)
{
	relativePath = NativePath_SkipLeadingSeparators(relativePath);
	if ((dst == NULL) || (dstSize == 0) || (relativePath.len >= dstSize))
	{
		return 0;
	}

	for (size_t i = 0; i < relativePath.len; i++)
	{
		u8 byte = relativePath.ptr[i];
		dst[i] = (char)(NativePath_IsSeparator(byte) ? '/' : NativeStr8_ToLowerAscii(byte));
	}

	dst[relativePath.len] = '\0';
	return 1;
}

internal const char *NativeAssets_FindIndexedPath(NativeStr8 relativePath)
{
	char key[NATIVE_ASSETS_PATH_MAX];

	if (!NativeAssets_NormalizeRelativeKey(relativePath, key, sizeof(key)))
	{
		return NULL;
	}

	for (int i = 0; i < s_nativeAssetIndexCount; i++)
	{
		if (strcmp(s_nativeAssetIndex[i].key, key) == 0)
		{
			return s_nativeAssetIndex[i].path;
		}
	}

	return NULL;
}

internal int NativeAssets_IndexAdd(NativeStr8 relativePath, const char *hostPath)
{
	char key[NATIVE_ASSETS_PATH_MAX];

	if (!NativeAssets_NormalizeRelativeKey(relativePath, key, sizeof(key)))
	{
		return 0;
	}

	if (!NativeAssets_IndexReserve(s_nativeAssetIndexCount + 1))
	{
		return 0;
	}

	char *keyCopy = NativeAssets_CopyCString(key);
	char *pathCopy = NativeAssets_CopyCString(hostPath);
	if ((keyCopy == NULL) || (pathCopy == NULL))
	{
		free(keyCopy);
		free(pathCopy);
		return 0;
	}

	s_nativeAssetIndex[s_nativeAssetIndexCount].key = keyCopy;
	s_nativeAssetIndex[s_nativeAssetIndexCount].path = pathCopy;
	s_nativeAssetIndexCount++;
	return 1;
}

internal void NativeAssets_IndexScanDir(const char *dirPath, NativeStr8 relativeDir, int depth)
{
	if (depth > 16)
	{
		return;
	}

#if defined(_WIN32)
	{
		char searchPath[NATIVE_ASSETS_PATH_MAX];
		WIN32_FIND_DATAA findData;
		HANDLE findHandle;

		if (!NativePath_Join(searchPath, sizeof(searchPath), NativeStr8_FromCString(dirPath), NATIVE_STR8_LIT("*")))
			return;

		findHandle = FindFirstFileA(searchPath, &findData);
		if (findHandle == INVALID_HANDLE_VALUE)
			return;

		do
		{
			NativeStr8 entryName = NativeStr8_FromCString(findData.cFileName);
			char childPath[NATIVE_ASSETS_PATH_MAX];
			char relativePath[NATIVE_ASSETS_PATH_MAX];

			if (NativeStr8_Equals(entryName, NATIVE_STR8_LIT(".")) || NativeStr8_Equals(entryName, NATIVE_STR8_LIT("..")))
				continue;

			if (!NativePath_Join(childPath, sizeof(childPath), NativeStr8_FromCString(dirPath), entryName))
				continue;

			if (relativeDir.len == 0)
			{
				if (!NativePath_NormalizeSlashes(relativePath, sizeof(relativePath), entryName))
					continue;
			}
			else
			{
				if (!NativePath_Join(relativePath, sizeof(relativePath), relativeDir, entryName))
					continue;
			}

			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				NativeAssets_IndexScanDir(childPath, NativeStr8_FromCString(relativePath), depth + 1);
				continue;
			}

			if (NativeAssets_FileExistsHost(childPath))
				NativeAssets_IndexAdd(NativeStr8_FromCString(relativePath), childPath);
		} while (FindNextFileA(findHandle, &findData) != 0);

		FindClose(findHandle);
	}
#else
	struct dirent *entry;

	DIR *dir = opendir(dirPath);
	if (dir == NULL)
	{
		return;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		NativeStr8 entryName = NativeStr8_FromCString(entry->d_name);
		char childPath[NATIVE_ASSETS_PATH_MAX];
		char relativePath[NATIVE_ASSETS_PATH_MAX];

		if (NativeStr8_Equals(entryName, NATIVE_STR8_LIT(".")) || NativeStr8_Equals(entryName, NATIVE_STR8_LIT("..")))
		{
			continue;
		}

		if (!NativePath_Join(childPath, sizeof(childPath), NativeStr8_FromCString(dirPath), entryName))
		{
			continue;
		}

		if (relativeDir.len == 0)
		{
			if (!NativePath_NormalizeSlashes(relativePath, sizeof(relativePath), entryName))
			{
				continue;
			}
		}
		else
		{
			if (!NativePath_Join(relativePath, sizeof(relativePath), relativeDir, entryName))
			{
				continue;
			}
		}

		DIR *childDir = opendir(childPath);
		if (childDir != NULL)
		{
			closedir(childDir);
			NativeAssets_IndexScanDir(childPath, NativeStr8_FromCString(relativePath), depth + 1);
			continue;
		}

		if (NativeAssets_FileExistsHost(childPath))
		{
			NativeAssets_IndexAdd(NativeStr8_FromCString(relativePath), childPath);
		}
	}

	closedir(dir);
#endif
}

internal void NativeAssets_BuildIndex(void)
{
	if (s_nativeAssetIndexBuilt)
	{
		return;
	}

	s_nativeAssetIndexBuilt = 1;

	NativeAssets_IndexScanDir(s_nativeAssetsDir, (NativeStr8){0}, 0);
}

internal int NativeAssets_BaseHasRequiredFile(NativeStr8 baseDir)
{
	char assetsDir[NATIVE_ASSETS_PATH_MAX];
	char path[NATIVE_ASSETS_PATH_MAX];

	if (!NativeAssets_FindAssetsDir(baseDir, assetsDir, sizeof(assetsDir)))
	{
		return 0;
	}

	if (NativeAssets_FindHostChildCaseInsensitive(NativeStr8_FromCString(assetsDir), NATIVE_STR8_LIT(NATIVE_ASSETS_BIGFILE_PATH), path, sizeof(path)))
	{
		return NativeAssets_FileExistsHost(path);
	}

	if (!NativePath_Join(path, sizeof(path), NativeStr8_FromCString(assetsDir), NATIVE_STR8_LIT(NATIVE_ASSETS_BIGFILE_PATH)))
	{
		return 0;
	}

	if (NativeAssets_FileExistsHost(path))
	{
		return 1;
	}

	if (NativeAssets_FindHostChildCaseInsensitive(NativeStr8_FromCString(assetsDir), NATIVE_STR8_LIT(NATIVE_ASSETS_DISC_PATH), path, sizeof(path)))
	{
		return NativeAssets_FileExistsHost(path);
	}

	if (!NativePath_Join(path, sizeof(path), NativeStr8_FromCString(assetsDir), NATIVE_STR8_LIT(NATIVE_ASSETS_DISC_PATH)))
	{
		return 0;
	}

	return NativeAssets_FileExistsHost(path);
}

internal int NativeAssets_SetBaseDir(NativeStr8 baseDir)
{
	char assetsDir[NATIVE_ASSETS_PATH_MAX];

	baseDir = NativePath_TrimTrailingSeparators(baseDir);
	if (!NativePath_NormalizeSlashes(s_nativeAssetsBaseDir, sizeof(s_nativeAssetsBaseDir), baseDir))
	{
		return 0;
	}

	if (!NativeAssets_FindAssetsDir(NativeStr8_FromCString(s_nativeAssetsBaseDir), assetsDir, sizeof(assetsDir)))
	{
		if (!NativePath_Join(assetsDir, sizeof(assetsDir), NativeStr8_FromCString(s_nativeAssetsBaseDir), NATIVE_STR8_LIT(NATIVE_ASSETS_DIR_NAME)))
		{
			return 0;
		}
	}

	if (!NativePath_NormalizeSlashes(s_nativeAssetsDir, sizeof(s_nativeAssetsDir), NativeStr8_FromCString(assetsDir)))
	{
		return 0;
	}

	NativeDiscImage_Init(s_nativeAssetsDir);
	NativeAssets_ClearIndex();
	s_nativeAssetsInitialized = 1;
	return 1;
}

int NativeAssets_Init(const char *executableBasePath)
{
	char parentDir[NATIVE_ASSETS_PATH_MAX];
	char grandparentDir[NATIVE_ASSETS_PATH_MAX];

	if ((executableBasePath == NULL) || (executableBasePath[0] == '\0'))
	{
		executableBasePath = ".";
	}

	NativeStr8 exeDir = NativePath_TrimTrailingSeparators(NativeStr8_FromCString(executableBasePath));

	if (NativeAssets_BaseHasRequiredFile(exeDir))
	{
		return NativeAssets_SetBaseDir(exeDir);
	}

	if (NativePath_Parent(parentDir, sizeof(parentDir), exeDir))
	{
		NativeStr8 parent = NativeStr8_FromCString(parentDir);

		if (NativeAssets_BaseHasRequiredFile(parent))
		{
			return NativeAssets_SetBaseDir(parent);
		}

		if (NativePath_Parent(grandparentDir, sizeof(grandparentDir), parent))
		{
			NativeStr8 grandparent = NativeStr8_FromCString(grandparentDir);

			if (NativeAssets_BaseHasRequiredFile(grandparent))
			{
				return NativeAssets_SetBaseDir(grandparent);
			}
		}
	}

	return NativeAssets_SetBaseDir(exeDir);
}

const char *NativeAssets_GetBaseDir(void)
{
	return s_nativeAssetsBaseDir;
}

const char *NativeAssets_GetAssetDir(void)
{
	return s_nativeAssetsDir;
}

int NativeAssets_BuildPathStr8(NativeStr8 relativePath, char *dst, size_t dstSize)
{
	if (!s_nativeAssetsInitialized && !NativeAssets_Init("."))
	{
		return 0;
	}

	return NativePath_Join(dst, dstSize, NativeStr8_FromCString(s_nativeAssetsDir), relativePath);
}

int NativeAssets_BuildPath(const char *relativePath, char *dst, size_t dstSize)
{
	return NativeAssets_BuildPathStr8(NativeStr8_FromCString(relativePath), dst, dstSize);
}

int NativeAssets_ResolvePathStr8(NativeStr8 relativePath, char *dst, size_t dstSize)
{
	char path[NATIVE_ASSETS_PATH_MAX];

	if (!NativeAssets_BuildPathStr8(relativePath, path, sizeof(path)))
	{
		return 0;
	}

	if (NativeAssets_FileExistsHost(path))
	{
		return NativePath_NormalizeSlashes(dst, dstSize, NativeStr8_FromCString(path));
	}

	NativeAssets_BuildIndex();
	const char *indexedPath = NativeAssets_FindIndexedPath(relativePath);
	if (indexedPath == NULL)
	{
		return 0;
	}

	return NativePath_NormalizeSlashes(dst, dstSize, NativeStr8_FromCString(indexedPath));
}

int NativeAssets_ResolvePath(const char *relativePath, char *dst, size_t dstSize)
{
	return NativeAssets_ResolvePathStr8(NativeStr8_FromCString(relativePath), dst, dstSize);
}

FILE *NativeAssets_OpenHostStr8(NativeStr8 relativePath, const char *mode)
{
	char path[NATIVE_ASSETS_PATH_MAX];

	if ((mode == NULL) || !NativeAssets_BuildPathStr8(relativePath, path, sizeof(path)))
	{
		return NULL;
	}

	FILE *file = fopen(path, mode);
	if (file != NULL)
	{
		return file;
	}

	if (mode[0] != 'r')
	{
		return NULL;
	}

	if (!NativeAssets_ResolvePathStr8(relativePath, path, sizeof(path)))
	{
		return NULL;
	}

	return fopen(path, mode);
}

FILE *NativeAssets_OpenHost(const char *relativePath, const char *mode)
{
	return NativeAssets_OpenHostStr8(NativeStr8_FromCString(relativePath), mode);
}

FILE *NativeAssets_OpenHostBigfile(const char *mode)
{
	return NativeAssets_OpenHostStr8(NATIVE_STR8_LIT(NATIVE_ASSETS_BIGFILE_PATH), mode);
}

internal int NativeAssets_ReadExact(FILE *file, void *dst, size_t size)
{
	return fread(dst, 1, size, file) == size;
}

internal u32 NativeAssets_ReadLE32(const u8 *bytes)
{
	return ((u32)bytes[0]) | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

internal int NativeAssets_ReadHostBytes(const char *path, struct NativeAssetsByteBuffer *bytes)
{
	bytes->data = NULL;
	bytes->size = 0;

	FILE *file = NativeAssets_OpenHost(path, "rb");
	if (file == NULL)
	{
		return 0;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return 0;
	}

	long size = ftell(file);
	if ((size <= 0) || (size > 0x7fffffff))
	{
		fclose(file);
		return 0;
	}

	if (fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}

	bytes->data = (u8 *)malloc((size_t)size);
	if (bytes->data == NULL)
	{
		fclose(file);
		return 0;
	}

	if (!NativeAssets_ReadExact(file, bytes->data, (size_t)size))
	{
		free(bytes->data);
		bytes->data = NULL;
		fclose(file);
		return 0;
	}

	fclose(file);
	bytes->size = (int)size;
	return 1;
}

int NativeAssets_ReadBytes(const char *path, int readMode, struct NativeAssetsByteBuffer *bytes)
{
	bytes->data = NULL;
	bytes->size = 0;

	if (NativeAssets_ReadHostBytes(path, bytes))
	{
		return 1;
	}

	return NativeDiscImage_ReadFileBytes(path, readMode == NATIVE_ASSET_READ_RAW_CD_SECTORS, &bytes->data, &bytes->size);
}

void NativeAssets_FreeBytes(struct NativeAssetsByteBuffer *bytes)
{
	free(bytes->data);
	bytes->data = NULL;
	bytes->size = 0;
}

internal void NativeAssets_PrintHeader(void)
{
	fprintf(stderr, "[CTR Native] Missing or incomplete assets.\n");
	fprintf(stderr, "[CTR Native] Expected NTSC-U retail assets under: %s\n", NativeAssets_GetAssetDir());
}

internal void NativeAssets_PrintFooter(void)
{
	fprintf(stderr, "[CTR Native] Provide either raw NTSC-U disc image %s, or extracted files:\n", NATIVE_ASSETS_DISC_PATH);
	fprintf(stderr, "[CTR Native]   %s, %s, %s, %s, plus XA files referenced by %s\n", NATIVE_ASSETS_BIGFILE_PATH, NATIVE_ASSETS_KART_HWL_PATH,
	        NATIVE_ASSETS_TEST_STR_PATH, NATIVE_ASSETS_XNF_PATH, NATIVE_ASSETS_XNF_PATH);
}

internal int NativeAssets_CheckRequiredFile(const char *path)
{
	char assetPath[NATIVE_ASSETS_PATH_MAX];
	struct NativeDiscImageFile discFile;

	if (!NativeAssets_BuildPath(path, assetPath, sizeof(assetPath)))
	{
		fprintf(stderr, "[CTR Native] asset path too long: %s\n", path);
		return 0;
	}

	if (NativeAssets_ResolvePath(path, assetPath, sizeof(assetPath)))
	{
		return 1;
	}

	if (NativeDiscImage_FindFile(path, &discFile))
	{
		return 1;
	}

	fprintf(stderr, "[CTR Native] missing asset: %s\n", assetPath);
	return 0;
}

internal int NativeAssets_ValidateXA(void)
{
	local_persist const char *xaDirs[NATIVE_ASSETS_XA_TYPE_COUNT] = {
	    "XA/MUSIC",
	    "XA/ENG/EXTRA",
	    "XA/ENG/GAME",
	};
	struct NativeAssetsByteBuffer xnf;
	u8 required[NATIVE_ASSETS_XA_TYPE_COUNT][NATIVE_ASSETS_XA_MAX_FILE_NUMBER];
	u32 missing = 0;
	u32 categoryID;

	memset(required, 0, sizeof(required));

	if (!NativeAssets_ReadBytes(NATIVE_ASSETS_XNF_PATH, NATIVE_ASSET_READ_DATA_FILE, &xnf))
	{
		return 0;
	}

	if ((xnf.size < NATIVE_ASSETS_XA_HEADER_SIZE) || (NativeAssets_ReadLE32(&xnf.data[0]) != 0x464e4958) || (NativeAssets_ReadLE32(&xnf.data[4]) != 102) ||
	    (NativeAssets_ReadLE32(&xnf.data[8]) != NATIVE_ASSETS_XA_TYPE_COUNT))
	{
		NativeAssets_FreeBytes(&xnf);
		fprintf(stderr, "[CTR Native] invalid XA manifest: %s\n", NATIVE_ASSETS_XNF_PATH);
		return 0;
	}

	u32 numXasTotal = NativeAssets_ReadLE32(&xnf.data[NATIVE_ASSETS_XA_NUM_XAS_TOTAL_OFFSET]);
	u32 numTracksTotal = NativeAssets_ReadLE32(&xnf.data[NATIVE_ASSETS_XA_NUM_TRACKS_TOTAL_OFFSET]);
	u32 entryOffset = NATIVE_ASSETS_XA_HEADER_SIZE + numXasTotal * 4u;
	u32 entryEnd = entryOffset + numTracksTotal * NATIVE_ASSETS_XA_ENTRY_BYTES;

	if ((entryEnd < entryOffset) || (entryEnd > (u32)xnf.size))
	{
		NativeAssets_FreeBytes(&xnf);
		fprintf(stderr, "[CTR Native] invalid XA entry table: %s\n", NATIVE_ASSETS_XNF_PATH);
		return 0;
	}

	for (categoryID = 0; categoryID < NATIVE_ASSETS_XA_TYPE_COUNT; categoryID++)
	{
		u32 numSongs = NativeAssets_ReadLE32(&xnf.data[NATIVE_ASSETS_XA_NUM_SONGS_OFFSET + categoryID * 4u]);
		u32 firstSongIndex = NativeAssets_ReadLE32(&xnf.data[NATIVE_ASSETS_XA_FIRST_SONG_INDEX_OFFSET + categoryID * 4u]);

		if ((firstSongIndex > numTracksTotal) || (numSongs > numTracksTotal) || (firstSongIndex + numSongs > numTracksTotal))
		{
			NativeAssets_FreeBytes(&xnf);
			fprintf(stderr, "[CTR Native] invalid XA song range in %s\n", NATIVE_ASSETS_XNF_PATH);
			return 0;
		}

		for (u32 xaID = 0; xaID < numSongs; xaID++)
		{
			const u8 *entry = &xnf.data[entryOffset + (firstSongIndex + xaID) * NATIVE_ASSETS_XA_ENTRY_BYTES];
			required[categoryID][entry[1]] = 1;
		}
	}

	NativeAssets_FreeBytes(&xnf);

	for (categoryID = 0; categoryID < NATIVE_ASSETS_XA_TYPE_COUNT; categoryID++)
	{
		for (u32 fileNumber = 0; fileNumber < NATIVE_ASSETS_XA_MAX_FILE_NUMBER; fileNumber++)
		{
			char relativePath[256];
			char path[NATIVE_ASSETS_PATH_MAX];
			struct NativeDiscImageFile discFile;

			if (!required[categoryID][fileNumber])
			{
				continue;
			}

			int written = snprintf(relativePath, sizeof(relativePath), "%s/S%02u.XA", xaDirs[categoryID], (unsigned int)fileNumber);
			if ((written <= 0) || ((size_t)written >= sizeof(relativePath)) || !NativeAssets_BuildPath(relativePath, path, sizeof(path)))
			{
				fprintf(stderr, "[CTR Native] XA asset path too long: %s/S%02u.XA\n", xaDirs[categoryID], (unsigned int)fileNumber);
				missing++;
				continue;
			}

			if (!NativeAssets_ResolvePath(relativePath, path, sizeof(path)) && !NativeDiscImage_FindFile(relativePath, &discFile))
			{
				fprintf(stderr, "[CTR Native] missing XA asset: %s\n", path);
				missing++;
			}
		}
	}

	return missing == 0;
}

int NativeAssets_Validate(void)
{
	int ok = 1;

	ok &= NativeAssets_CheckRequiredFile(NATIVE_ASSETS_BIGFILE_PATH);
	ok &= NativeAssets_CheckRequiredFile(NATIVE_ASSETS_KART_HWL_PATH);
	ok &= NativeAssets_CheckRequiredFile(NATIVE_ASSETS_TEST_STR_PATH);
	ok &= NativeAssets_CheckRequiredFile(NATIVE_ASSETS_XNF_PATH);

	if (ok)
	{
		ok &= NativeAssets_ValidateXA();
	}

	if (!ok)
	{
		NativeAssets_PrintHeader();
		NativeAssets_PrintFooter();
	}

	return ok;
}

//========================================================================================
// TRACK CONTAINERS
//========================================================================================
//
// The read path for .rldtrack. On by default, --no-tracks turns it off.
//
// What happens here and what does not:
//
//   NOTHING is unpacked. No temp folder, no file next to the exe, no
//   plain text on disk. LEVD and VRMD stay in memory and
//   go from there unchanged into the same load path that retail takes. The
//   container is an envelope, not an installer.
//
//   The list reads only header, directory and META per file. LEVD and VRMD
//   are only touched when a track is really loaded.
//
//   Every reference is checked against the real file size before jumping
//   to it. The reader in include/rldtrack.inc does that, and it does it the
//   same way for both sides.
//
// THE DONOR SLOT, AND WHAT THE RUN-TIME IDS CHANGE ABOUT IT.
//
// In the ENGINE a container track runs on the slot of an existing one.
// Above the loader every track of the list carries its own level ID from
// 65..99 (assigned below in NativeTrackID_Assign): the menu computes with it,
// and only the funnel MainRaceTrack_RequestLoad turns it into the donor slot.
// Two tracks are thus two different levels for menu and log - they can no
// longer both show up as the same "Level 4" above the loader, and inside the
// loader the slot check (NativeTrack_ArmSubfiles) keeps them apart.

void Platform_Log(const char *format, ...);
int Platform_SettingsLocked(void);

#define NATIVE_TRACK_DIR_NAME  "tracks"
#define NATIVE_TRACK_EXTENSION ".rldtrack"
#define NATIVE_TRACK_MAX       64

// Default ON: the folder is read at every start.
//
// The list sits in the main menu behind NITRO-PIT, and a menu entry that is
// empty depending on the command line would be a broken menu entry. --tracks
// is still accepted but has no effect.
//
// --no-tracks leaves the folder closed. The way back exists because reading now
// places memory behind the pack's window, and a measurement without this
// memory needs a way to get there.
int g_cfg_tracks = 1;

// The old way for the extra memory behind the window: a fixed megabyte instead
// of the number stored in the containers. See the scan in main.c.
int g_cfg_tracksFixedMemory = 0;

// THE FOLDER, empty means tracks/ under the base.
//
// For the menu reference: the carousel
// shows nine rows, i.e. the neighbors of the selected track and their count.
// An image is only repeatable if the whole list is fixed, not just
// the selected row. The reference runs therefore point to a separate
// folder with exactly the containers whose SHA1 the reference names.
global_variable char s_nativeTrackFolder[NATIVE_ASSETS_PATH_MAX] = "";

void NativeTrack_SetFolder(const char *folder)
{
	snprintf(s_nativeTrackFolder, sizeof(s_nativeTrackFolder), "%s", (folder != NULL) ? folder : "");
}

global_variable struct NativeTrackEntry s_nativeTracks[NATIVE_TRACK_MAX];
global_variable int s_nativeTrackCount = 0;
global_variable int s_nativeTrackScanned = 0;

// The loaded container. Only one - the game never has more than one track
// at the same time either.
global_variable int s_nativeTrackLoaded = -1;
global_variable u8 *s_nativeTrackLev = NULL;
global_variable u32 s_nativeTrackLevSize = 0;
global_variable u8 *s_nativeTrackVrm = NULL;
global_variable u32 s_nativeTrackVrmSize = 0;

// Which two bigfile numbers the current load replaces with the container.
// -1 means "none", and after reading each one is set back to -1:
// a load fetches every file exactly once, and whatever is read under the
// same number afterwards is something else.
global_variable int s_nativeTrackLevSubfile = -1;
global_variable int s_nativeTrackVrmSubfile = -1;

// The slot the loaded container occupies. Without it EVERY
// following load would get the container data - even one for a completely different
// track, because "loaded" alone does not say WHERE.
//
// -1 means: no slot occupied.
global_variable int s_nativeTrackDonorLevel = -1;

// The SHA-256 of the loaded LEVD, as Rld_ReadChunk proved it at load, and the
// level LOAD_TenStages armed last (NativeSound_ArmForLevel) - together the
// answer of NativeTrack_ActiveLevdSha256.
global_variable u8 s_nativeTrackLevdSha[32];
global_variable int s_nativeTrackLevelNow = -1;

internal void NativeTrack_CopyText(char *dst, size_t dstSize, const char *src)
{
	size_t length = strlen(src);

	if (length >= dstSize)
	{
		length = dstSize - 1u;
	}

	memcpy(dst, src, length);
	dst[length] = '\0';
}

// Tables that every disc track has and a foreign one may or may not have.
//
// MEASURED: all 18 arcade tracks and all 5 battle arenas of the
// NTSC-U image have every one of these nine fields set. Not a single one is
// null in any of the 23. The test data has NONE of them.
//
// THAT IS A FINDING AND NOT A CAUSE. An earlier version blocked here: a
// track without these tables was not loaded, on the grounds that
// levTexLookup feeds pointers that are dereferenced unchecked.
//
// Counted again, that is not true. Of the nine, exactly three are read through a
// level pointer anywhere in the tree:
//
//   levTexLookup   DecalGlobal_Store and DecalGlobal_FindInLEV, BOTH with
//                  a null test in the first line
//   visOVertSrc    a memcpy in MainFrame whose length comes from
//                  numWaterVertices - and that is 0 here
//   visSCVertSrc   the same with numSCVert
//
// The other six - unk3, unk4, unk_Lev_CC, unk_Lev_D0, lowTexArray and
// namedTexArray - are read NOWHERE. A block on them would have stopped a
// track that might run.
//
// So it stays a line in the log. What causes the crash is told by the
// stage counter in the load path, not by this list.
//
// The offsets are those of struct Level in include/namespace_Level.h, counted
// from the payload - i.e. from byte 4 of the file, after the reference to the
// pointer map. A null word is a null word whether the map has already been applied
// or not.
struct NativeTrackLevField
{
	u32 offset;
	const char *name;
};

global_variable const struct NativeTrackLevField s_nativeTrackLevFields[] = {
    {0x1c, "unk3"},         {0x20, "unk4"},          {0x28, "visOVertSrc"},
    {0x3c, "levTexLookup"}, {0x40, "namedTexArray"}, {0xcc, "unk_Lev_CC"},
    {0xd0, "unk_Lev_D0"},   {0xd4, "lowTexArray"},   {0x170, "visSCVertSrc"},
};

#define NATIVE_TRACK_LEV_BODY 4

internal u32 NativeTrack_LevField(const u8 *lev, u32 offset)
{
	const u8 *at = &lev[NATIVE_TRACK_LEV_BODY + offset];

	return (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) | ((u32)at[3] << 24);
}

// Writes the missing names into `note` and returns their count.
internal int NativeTrack_MissingLevTables(struct NativeTrackEntry *entry, const u8 *lev, size_t levSize)
{
	size_t at = 0;
	int found = 0;
	int i;

	entry->note[0] = '\0';

	if (levSize < (NATIVE_TRACK_LEV_BODY + 0x200u))
	{
		NativeTrack_CopyText(entry->note, sizeof(entry->note), "the LEV is shorter than a level header");
		return 1;
	}

	for (i = 0; i < (int)(sizeof(s_nativeTrackLevFields) / sizeof(s_nativeTrackLevFields[0])); i++)
	{
		if (NativeTrack_LevField(lev, s_nativeTrackLevFields[i].offset) != 0)
		{
			continue;
		}

		found++;

		{
			const char *name = s_nativeTrackLevFields[i].name;
			size_t need = strlen(name) + ((at != 0) ? 1u : 0u);

			if ((at + need) < (sizeof(entry->note) - 1u))
			{
				if (at != 0)
				{
					entry->note[at++] = ' ';
				}

				memcpy(&entry->note[at], name, strlen(name));
				at += strlen(name);
				entry->note[at] = '\0';
			}
		}
	}

	return found;
}

// Reads everything that belongs in the list, and does not touch LEVD and VRMD.
internal void NativeTrack_ReadForList(struct NativeTrackEntry *entry)
{
	struct RldReader reader;
	struct RldMeta meta;
	const char *error;
	u8 *metaData;
	size_t metaSize;
	int metaIndex = -1;

	entry->ok = 0;
	entry->problem = NULL;
	entry->refusal = NATIVE_TRACK_REFUSAL_DAMAGED;

	// Rld_Open enforces the format rules - required
	// chunks, placement, duplicates, bounds - and says WHY it refuses. The
	// numbers from the header are in the reader even when it refused.
	error = Rld_Open(&reader, entry->path);
	entry->formatMajor = reader.major;
	entry->formatMinor = reader.minor;
	entry->unknownFlags = reader.unknownFlags;
	if (error != NULL)
	{
		entry->problem = error;
		entry->refusal = (reader.refusal == RLD_REFUSAL_NEWER)   ? NATIVE_TRACK_REFUSAL_NEWER
		                 : (reader.refusal == RLD_REFUSAL_OLDER) ? NATIVE_TRACK_REFUSAL_OLDER
		                                                         : NATIVE_TRACK_REFUSAL_DAMAGED;
		return;
	}

	// The version stamp: the SHA-256 of the LEVD, straight from the directory.
	// The chunk itself stays untouched - the list rule still holds. This way even
	// a container that fails right away at META keeps its stamp
	// in the level ID registry.
	{
		const u8 *levEntry = Rld_FindEntry(&reader, "LEVD", NULL);

		if (levEntry != NULL)
		{
			memcpy(entry->levdSha, &levEntry[RLD_DIR_HASH_OFFSET], sizeof(entry->levdSha));
			entry->levdShaOk = 1;
		}
	}

	if (Rld_FindEntry(&reader, "META", &metaIndex) == NULL)
	{
		entry->problem = "META is missing - required chunk";
		Rld_Close(&reader);
		return;
	}

	metaData = Rld_ReadChunk(&reader, metaIndex, &metaSize, &error);
	if (metaData == NULL)
	{
		entry->problem = error;
		Rld_Close(&reader);
		return;
	}

	error = Rld_ParseMeta(&meta, metaData, metaSize);
	free(metaData);

	if (error != NULL)
	{
		entry->problem = error;
		Rld_Close(&reader);
		return;
	}

	NativeTrack_CopyText(entry->name, sizeof(entry->name), meta.strings[0]);
	NativeTrack_CopyText(entry->author, sizeof(entry->author), meta.strings[1]);
	entry->trackVersion = meta.trackVersion;
	entry->metaVersion = meta.metaVersion;
	entry->modes = meta.modes;
	entry->primBytes = meta.primBytes;
	entry->memTotal = meta.memTotal;

	// What the list promises: the file is a version 4 container, its header
	// and its directory follow the rules, META, LEVD and VRMD are there, and
	// META can be read. Whether LEVD matches its hash and whether META states the
	// memory correctly only shows up at load time - for that the
	// list would have to read LEVD, and the list rule forbids that.
	Rld_Close(&reader);

	entry->ok = 1;
	entry->refusal = NATIVE_TRACK_REFUSAL_NONE;
}

const char *NativeTrack_ModesText(u32 modes)
{
	return Rld_ModesText(modes);
}

// THE OFFER RULE (format 4.1).
//
// One place for the wheel, the cup and every developer path. The keywords
// have at most 12 characters (cup row, space under the wheel row), the lines
// for the preview window as well - FONT_SMALL, 176 pixels. Capital letters, no
// parentheses: the menu font has neither lowercase letters nor ( ).
internal void NativeTrack_RefusalLines(struct NativeTrackRefusalText *text, const char *tag, const char *a, const char *b, const char *c,
                                       const char *d)
{
	text->tag = tag;
	text->lines[0] = a;
	text->lines[1] = b;
	text->lines[2] = c;
	text->lines[3] = d;
	text->lineCount = (d != NULL) ? 4 : ((c != NULL) ? 3 : ((b != NULL) ? 2 : 1));
}

const char *NativeTrack_WhyNoRace(int index, struct NativeTrackRefusalText *text)
{
	return NativeTrack_WhyNotOffered(index, NATIVE_TRACK_MODE_RACE, text);
}

internal u32 NativeTrack_ModeBit(int mode)
{
	if (mode == NATIVE_TRACK_MODE_CRYSTAL)
	{
		return RLD_MODE_CRYSTAL_CHALLENGE;
	}

	return (mode == NATIVE_TRACK_MODE_CTR) ? RLD_MODE_CTR_CHALLENGE : RLD_MODE_RACE;
}

// The rule for one mode: the reader's refusals and the missing
// level ID as before, then "built AND declared" for exactly this bit.
const char *NativeTrack_WhyNotOffered(int index, int mode, struct NativeTrackRefusalText *text)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	const u32 bit = NativeTrack_ModeBit(mode);
	struct NativeTrackRefusalText unused;

	if (text == NULL)
	{
		text = &unused;
	}

	memset(text, 0, sizeof(*text));

	if (entry == NULL)
	{
		NativeTrack_RefusalLines(text, "MISSING", "NOT IN", "THE FOLDER", NULL, NULL);
		return "not in the folder";
	}

	if (!entry->ok)
	{
		switch (entry->refusal)
		{
		case NATIVE_TRACK_REFUSAL_NEWER:
			NativeTrack_RefusalLines(text, "NEEDS NEWER", "NEEDS A", "NEWER", "VERSION OF", "CTR RELOAD");
			break;
		case NATIVE_TRACK_REFUSAL_OLDER:
			// Players pack with Reload Studio, not with rldpack. "IN
			// RELOAD STUDIO" would be 16 characters, so it takes two lines.
			NativeTrack_RefusalLines(text, "OLD FORMAT", "OLD FORMAT", "PACK AGAIN", "IN RELOAD", "STUDIO");
			break;
		case NATIVE_TRACK_REFUSAL_MEMORY:
			NativeTrack_RefusalLines(text, "MEMORY INFO", "MEMORY INFO", "TOO LOW", "PACK AGAIN", NULL);
			break;
		default:
			NativeTrack_RefusalLines(text, "DAMAGED", "FILE IS", "DAMAGED", "SEE LOG", NULL);
			break;
		}
		return (entry->problem != NULL) ? entry->problem : "rejected";
	}

	if (NativeTrack_LevelForIndex(index) < 0)
	{
		NativeTrack_RefusalLines(text, "NO ID", "NO FREE", "LEVEL ID", NULL, NULL);
		return "no free level id";
	}

	// Built AND declared - never an undeclared mode.
	if (((RLD_MODES_PLAYABLE & bit) == 0u) || ((entry->modes & bit) == 0u))
	{
		if (bit == RLD_MODE_CRYSTAL_CHALLENGE)
		{
			NativeTrack_RefusalLines(text, "NO CRYSTAL", "NO CRYSTAL", "MODE", "DECLARED", NULL);
			return "does not declare Crystal Challenge - not offered";
		}

		if (bit == RLD_MODE_CTR_CHALLENGE)
		{
			NativeTrack_RefusalLines(text, "NO CTR MODE", "NO CTR MODE", "DECLARED", NULL, NULL);
			return "does not declare CTR Challenge - not offered";
		}

		NativeTrack_RefusalLines(text, "NO RACE MODE", "NO RACE MODE", "DECLARED", NULL, NULL);
		return "does not declare Race - not offered";
	}

	return NULL;
}

int NativeTrack_OffersRace(int index)
{
	return NativeTrack_WhyNoRace(index, NULL) == NULL;
}

// NOT IN THE RACE LIST: a valid
// container that does not declare Race - a crystal track belongs under
// CRYSTAL, not grayed out in the RACE list. A container with a real error
// (NEEDS NEWER, OLD FORMAT, MEMORY INFO, DAMAGED, NO ID) stays visible, gray
// with a reason, otherwise an author goes looking for the track. The cups still name
// the reason NO RACE MODE (NativeCup_Finish), because there someone
// entered the track explicitly.
//
// The same for CRYSTAL: only containers that declare Crystal are
// listed there; a valid container without the bit is not in the list.
int NativeTrack_HiddenFromRace(int index)
{
	return NativeTrack_HiddenFrom(index, NATIVE_TRACK_MODE_RACE);
}

int NativeTrack_HiddenFrom(int index, int mode)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	const u32 bit = NativeTrack_ModeBit(mode);

	if ((entry == NULL) || !entry->ok)
	{
		return 0;
	}

	return ((RLD_MODES_PLAYABLE & bit) == 0u) || ((entry->modes & bit) == 0u);
}

int NativeTrack_CountRaceOffered(void)
{
	return NativeTrack_CountOffered(NATIVE_TRACK_MODE_RACE);
}

int NativeTrack_CountOffered(int mode)
{
	int count = 0;
	int i;

	for (i = 0; i < s_nativeTrackCount; i++)
	{
		if (NativeTrack_WhyNotOffered(i, mode, NULL) == NULL)
		{
			count++;
		}
	}

	return count;
}

// The largest amount any of the containers asks for.
//
// Not the selected track, because the player only picks it long after
// MEMPACK_Init. Only containers that are usable: a file whose META cannot
// be read cannot be loaded, so nothing is reserved
// for it either.
//
// IN 64 BITS, AND SATURATING. Rld_ParseMeta already refuses a memTotal or
// primBytes that no LEV can need, so a listed entry cannot wrap this sum. The
// wide sum is the second layer: a memTotal of 0xffffff00 once wrapped to a few
// hundred bytes, the reserve came out too small and the load ran into the
// MEMPACK red screen. Saturated at 2 GiB, not at 4: the caller adds the clip
// surcharge (main.c), and Platform_SetMempackExtra clamps loudly anyway.
#define NATIVE_TRACK_MEMPACK_NEED_MAX 0x80000000u

u32 NativeTrack_MempackExtraNeeded(void)
{
	u64 most = 0;
	int i;

	for (i = 0; i < s_nativeTrackCount; i++)
	{
		const struct NativeTrackEntry *entry = &s_nativeTracks[i];

		// memTotal from META plus the clip buffers that MainInit creates per player
		// (data.PtrClipBuffer has four slots, so four times). META
		// does not carry clipBytes - the number comes from primBytes via the same
		// rule that Rld_MemNeed uses to compute it from the LEV at load time
		// (Rld_ClipBytesForPrimBytes, written out here in 64 bits - its u32
		// product wraps for a primBytes above about 3.7 GB).
		const u64 clipBytes = (u64)(entry->primBytes / RLD_POLY_GT4_BYTES) * RLD_CLIP_RECORD_GT4_BYTES;
		const u64 need = (u64)entry->memTotal + ((u64)NATIVE_TRACK_CLIP_BUFFERS * clipBytes);

		if (entry->ok && (need > most))
		{
			most = need;
		}
	}

	return (most > (u64)NATIVE_TRACK_MEMPACK_NEED_MAX) ? NATIVE_TRACK_MEMPACK_NEED_MAX : (u32)most;
}

// The primitive memory of the loaded track, computed from ITS LEV and not
// from META - the bytes are already here, so the number is measured instead of
// trusted. META only carries it so that startup knows it before any
// LEV is in memory.
u32 NativeTrack_PrimBytes(void)
{
	struct RldMemNeed need;

	if (s_nativeTrackLoaded < 0)
	{
		return 0;
	}

	Rld_MemNeed(&need, s_nativeTrackLev, (size_t)s_nativeTrackLevSize);

	return need.primBytes;
}

// The clip buffer of the loaded track, per player - the same source and
// the same rule as NativeTrack_PrimBytes, see RLD_CLIP_RECORD_GT4_BYTES.
u32 NativeTrack_ClipBytes(void)
{
	struct RldMemNeed need;

	if (s_nativeTrackLoaded < 0)
	{
		return 0;
	}

	Rld_MemNeed(&need, s_nativeTrackLev, (size_t)s_nativeTrackLevSize);

	return need.clipBytes;
}

internal int NativeTrack_HasExtension(const char *fileName)
{
	size_t nameLength = strlen(fileName);
	size_t extLength = strlen(NATIVE_TRACK_EXTENSION);
	size_t i;

	if (nameLength <= extLength)
	{
		return 0;
	}

	// By hand instead of stricmp: the extension may be upper or lower case,
	// and an author on Linux writes it differently than one on
	// Windows.
	for (i = 0; i < extLength; i++)
	{
		char a = fileName[nameLength - extLength + i];
		char b = NATIVE_TRACK_EXTENSION[i];

		if ((a >= 'A') && (a <= 'Z'))
		{
			a = (char)(a - 'A' + 'a');
		}

		if (a != b)
		{
			return 0;
		}
	}

	return 1;
}

internal void NativeTrack_AddFile(const char *dirPath, const char *fileName)
{
	struct NativeTrackEntry *entry;

	if (s_nativeTrackCount >= NATIVE_TRACK_MAX)
	{
		return;
	}

	if (!NativeTrack_HasExtension(fileName))
	{
		return;
	}

	entry = &s_nativeTracks[s_nativeTrackCount];
	memset(entry, 0, sizeof(*entry));

	if (!NativePath_Join(entry->path, sizeof(entry->path), NativeStr8_FromCString(dirPath), NativeStr8_FromCString(fileName)))
	{
		return;
	}

	NativeTrack_CopyText(entry->file, sizeof(entry->file), fileName);
	NativeTrack_ReadForList(entry);

	// A broken container does NOT drop out of the list. It is listed with the
	// reason next to it - whoever puts a file into the folder and then never
	// finds it again blames the game.
	if (entry->ok)
	{
		Platform_Log("[CTR Tracks] %s: '%s' by %s, format %u.%u, modes %s\n", entry->file, entry->name,
		             (entry->author[0] != '\0') ? entry->author : "(not set)", entry->formatMajor, entry->formatMinor,
		             NativeTrack_ModesText(entry->modes));
	}
	else
	{
		Platform_Log("[CTR Tracks] %s: REJECTED - %s\n", entry->file, (entry->problem != NULL) ? entry->problem : "unknown");

		// Both numbers if the version was the reason.
		if ((entry->refusal == NATIVE_TRACK_REFUSAL_NEWER) || (entry->refusal == NATIVE_TRACK_REFUSAL_OLDER))
		{
			Platform_Log("[CTR Tracks]   container format %u.%u, this build reads %u.x%s\n", entry->formatMajor, entry->formatMinor,
			             s_rldTrackFormat.major, (entry->unknownFlags != 0u) ? " - it needs feature bits this build does not know" : "");
			if (entry->unknownFlags != 0u)
			{
				Platform_Log("[CTR Tracks]   unknown feature bits 0x%08x\n", entry->unknownFlags);
			}
		}
	}

	s_nativeTrackCount++;
}

//----------------------------------------------------------------------------------------
// THE ORDER OF THE LIST - defined instead of inherited.
//
// FindFirstFileA returns the names already sorted on NTFS, but that is
// a property of the file system and not a promise: FAT and exFAT return
// creation order, and so does readdir on Linux. Since the assignment of
// level IDs depends on the order of new arrivals, it must be fixed -
// same folder contents, same assignment, on every disk.
//
// Comparison works like NTFS: character by character, UPPER cased. Not
// lower - the underscore (0x5F) lies BETWEEN the upper and the
// lower case letters, and only the upper case comparison sorts "Pizza_Planet" after
// "PizzaX", as NTFS does. A current folder thus keeps its familiar
// order, and with it the list snapshots of the menu reference
// stay valid.
//----------------------------------------------------------------------------------------

internal int NativeTrack_UpperChar(char c)
{
	if ((c >= 'a') && (c <= 'z'))
	{
		return c - ('a' - 'A');
	}

	return (int)(unsigned char)c;
}

internal int NativeTrack_CompareNames(const char *a, const char *b)
{
	while ((*a != '\0') && (NativeTrack_UpperChar(*a) == NativeTrack_UpperChar(*b)))
	{
		a++;
		b++;
	}

	return NativeTrack_UpperChar(*a) - NativeTrack_UpperChar(*b);
}

internal int NativeTrack_CompareEntries(const void *a, const void *b)
{
	return NativeTrack_CompareNames(((const struct NativeTrackEntry *)a)->file, ((const struct NativeTrackEntry *)b)->file);
}

//----------------------------------------------------------------------------------------
// THE LEVEL ID REGISTRY.
//
// File name -> level ID (65..99), maintained by the game, persistent in
// NATIVE_TRACK_ID_FILE IN THE TRACK FOLDER. There and not with the
// settings, because the IDs are a property of the folder contents: every
// folder behind --tracks-dir carries its own registry, the
// reference runs do not depend on the history of the player's folder, and a
// deleted folder takes its registry with it. The scan skips the
// file by itself - it does not end in .rldtrack.
//
// THE RULES:
//
//   known file        keeps its ID (matched by file name,
//                     case-insensitive - repacking under the same
//                     name keeps the ID, renaming is a new track)
//   new file          gets the smallest free ID, in list order
//   missing file      releases its ID, the entry is dropped - and with it
//                     everything attached to it (nothing is attached
//                     yet; best times, once they exist, will be deleted
//                     here as well)
//   new LEVD hash     the same track in a new version: the ID stays, the
//                     stamp is updated, the log says so
//
// If the file is MISSING or BROKEN, it is rebuilt and the log
// says so loudly - all IDs then come fresh from the sorted list. Under
// the settings lock (--settings-defaults) it is neither read nor
// written, like ctr-settings.cfg and for the same reason: a measurement run
// must not read state that someone left behind, and must not write any.
//----------------------------------------------------------------------------------------

struct NativeTrackIdSlot
{
	int used;

	// The file name in the spelling on disk, and the stamp as
	// hex text - exactly as both are stored in the file. "-" means: the file
	// had no readable LEVD directory at the last scan.
	char file[128];
	char sha[65];

	// The list index behind this ID, after reconciliation. A used ID
	// always points to a row - a file that is missing has lost its ID during
	// reconciliation.
	int scanIndex;
};

global_variable struct NativeTrackIdSlot s_trackIdSlots[NATIVE_TRACK_LEVELID_COUNT];
global_variable s16 s_trackIdForIndex[NATIVE_TRACK_MAX];

internal void NativeTrackID_HexFromBytes(char *dst, const u8 *bytes)
{
	local_persist const char digits[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 32; i++)
	{
		dst[i * 2 + 0] = digits[bytes[i] >> 4];
		dst[i * 2 + 1] = digits[bytes[i] & 0x0f];
	}

	dst[64] = '\0';
}

// Splits one line of the registry file into its three fields, in place.
// Returns the reason if the line is not usable - the caller then throws away
// the WHOLE file, because a registry that can only be half trusted
// is worth nothing.
internal const char *NativeTrackID_ParseLine(char *line, int *idOut, char **shaOut, char **fileOut)
{
	char *sha;
	char *file;
	char *end;
	long id;
	int i;

	id = strtol(line, &end, 10);
	if ((end == line) || (*end != '\t'))
	{
		return "a line does not start with <id><TAB>";
	}

	if ((id < NATIVE_TRACK_LEVELID_FIRST) || (id >= (NATIVE_TRACK_LEVELID_FIRST + NATIVE_TRACK_LEVELID_COUNT)))
	{
		return "an id is outside 65..99";
	}

	sha = end + 1;
	file = strchr(sha, '\t');
	if (file == NULL)
	{
		return "a line has no second TAB";
	}

	*file = '\0';
	file++;

	// Strip the line ending, whether LF or CRLF.
	end = file + strlen(file);
	while ((end > file) && ((end[-1] == '\n') || (end[-1] == '\r')))
	{
		*--end = '\0';
	}

	if ((file[0] == '\0') || (strlen(file) >= sizeof(((struct NativeTrackIdSlot *)0)->file)))
	{
		return "a file name is empty or too long";
	}

	if (strcmp(sha, "-") != 0)
	{
		if (strlen(sha) != 64)
		{
			return "a hash is neither 64 hex digits nor '-'";
		}

		for (i = 0; i < 64; i++)
		{
			const char c = sha[i];

			if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f'))))
			{
				return "a hash carries something other than lowercase hex";
			}
		}
	}

	*idOut = (int)id;
	*shaOut = sha;
	*fileOut = file;

	return NULL;
}

// Reads the registry file into the slots. Returns the reason if it is
// broken - the slots are then in an undefined state, and the caller
// clears them and assigns anew.
internal const char *NativeTrackID_ReadFile(FILE *file)
{
	char line[512];
	int s;

	while (fgets(line, sizeof(line), file) != NULL)
	{
		int id;
		char *sha;
		char *name;
		const char *problem;

		if ((line[0] == '#') || (line[0] == '\n') || (line[0] == '\r') || (line[0] == '\0'))
		{
			continue;
		}

		problem = NativeTrackID_ParseLine(line, &id, &sha, &name);
		if (problem != NULL)
		{
			return problem;
		}

		if (s_trackIdSlots[id - NATIVE_TRACK_LEVELID_FIRST].used)
		{
			return "an id appears twice";
		}

		for (s = 0; s < NATIVE_TRACK_LEVELID_COUNT; s++)
		{
			if (s_trackIdSlots[s].used && (NativeTrack_CompareNames(s_trackIdSlots[s].file, name) == 0))
			{
				return "a file name appears twice";
			}
		}

		{
			struct NativeTrackIdSlot *slot = &s_trackIdSlots[id - NATIVE_TRACK_LEVELID_FIRST];

			slot->used = 1;
			slot->scanIndex = -1;
			snprintf(slot->file, sizeof(slot->file), "%s", name);
			snprintf(slot->sha, sizeof(slot->sha), "%s", sha);
		}
	}

	return NULL;
}

internal void NativeTrackID_WriteFile(const char *path)
{
	FILE *file = fopen(path, "w");
	int s;
	int rows = 0;

	if (file == NULL)
	{
		Platform_LogWarn("[CTR Tracks] cannot write the id registry %s - ids hold for this run only\n", path);
		return;
	}

	fprintf(file, "# CTR Reload: fixed level IDs of the track containers in this folder.\n");
	fprintf(file, "# The game maintains this file itself: id<TAB>levd-sha256<TAB>file.\n");
	fprintf(file, "# Deleting it forces a reassignment of all IDs. Renaming a file makes it a new track.\n");

	for (s = 0; s < NATIVE_TRACK_LEVELID_COUNT; s++)
	{
		if (!s_trackIdSlots[s].used)
		{
			continue;
		}

		fprintf(file, "%d\t%s\t%s\n", NATIVE_TRACK_LEVELID_FIRST + s, s_trackIdSlots[s].sha, s_trackIdSlots[s].file);
		rows++;
	}

	fclose(file);
	Platform_Log("[CTR Tracks] id registry written: %d entr%s in %s\n", rows, (rows == 1) ? "y" : "ies", NATIVE_TRACK_ID_FILE);
}

// The reconciliation: read the registry, keep known files, release missing
// ones, assign new ones, write back, every change into the log. Runs
// once per scan, directly after sorting.
internal void NativeTrackID_Assign(const char *dirPath)
{
	char path[NATIVE_ASSETS_PATH_MAX];
	const int locked = Platform_SettingsLocked();
	int havePath = 0;
	int dirty = 0;
	int kept = 0;
	int fresh = 0;
	int freed = 0;
	int without = 0;
	int rejected = 0;
	int i;
	int s;

	memset(s_trackIdSlots, 0, sizeof(s_trackIdSlots));

	for (i = 0; i < NATIVE_TRACK_MAX; i++)
	{
		s_trackIdForIndex[i] = -1;
	}

	if (!locked)
	{
		havePath = NativePath_Join(path, sizeof(path), NativeStr8_FromCString(dirPath), NATIVE_STR8_LIT(NATIVE_TRACK_ID_FILE));

		if (havePath)
		{
			FILE *file = fopen(path, "r");

			if (file == NULL)
			{
				// The first run, or someone deleted it - both mean
				// a rebuild, and both are in the log. Only an assignment sets
				// dirty: an empty folder should not get a file made only of
				// comments.
				Platform_Log("[CTR Tracks] no id registry in the folder - a new one is started\n");
			}
			else
			{
				const char *problem = NativeTrackID_ReadFile(file);

				fclose(file);

				if (problem != NULL)
				{
					Platform_LogWarn("[CTR Tracks] the id registry is damaged (%s) - it is rebuilt, every id is assigned fresh\n", problem);
					memset(s_trackIdSlots, 0, sizeof(s_trackIdSlots));
					dirty = 1;
				}
			}
		}
	}
	else
	{
		Platform_Log("[CTR Tracks] settings are locked - level ids live in memory only, %s is neither read nor written\n", NATIVE_TRACK_ID_FILE);
	}

	// Step 1: known files keep their ID, missing ones release it.
	for (s = 0; s < NATIVE_TRACK_LEVELID_COUNT; s++)
	{
		struct NativeTrackIdSlot *slot = &s_trackIdSlots[s];
		int found = -1;

		if (!slot->used)
		{
			continue;
		}

		for (i = 0; i < s_nativeTrackCount; i++)
		{
			if ((s_trackIdForIndex[i] < 0) && (NativeTrack_CompareNames(slot->file, s_nativeTracks[i].file) == 0))
			{
				found = i;
				break;
			}
		}

		if (found < 0)
		{
			Platform_Log("[CTR Tracks] level id %d freed - '%s' is gone from the folder\n", NATIVE_TRACK_LEVELID_FIRST + s, slot->file);
			memset(slot, 0, sizeof(*slot));
			freed++;
			dirty = 1;
			continue;
		}

		slot->scanIndex = found;
		s_trackIdForIndex[found] = (s16)(NATIVE_TRACK_LEVELID_FIRST + s);
		kept++;

		// The spelling on disk wins - a change of
		// upper/lower case is the same track, but the file should
		// show what is actually there.
		if (strcmp(slot->file, s_nativeTracks[found].file) != 0)
		{
			snprintf(slot->file, sizeof(slot->file), "%s", s_nativeTracks[found].file);
			dirty = 1;
		}

		if (s_nativeTracks[found].levdShaOk)
		{
			char hex[65];

			NativeTrackID_HexFromBytes(hex, s_nativeTracks[found].levdSha);

			if (strcmp(slot->sha, hex) != 0)
			{
				if (strcmp(slot->sha, "-") != 0)
				{
					Platform_Log("[CTR Tracks] '%s' is a new version - the LEVD hash changed, level id %d stays\n", slot->file,
					             NATIVE_TRACK_LEVELID_FIRST + s);
				}

				snprintf(slot->sha, sizeof(slot->sha), "%s", hex);
				dirty = 1;
			}
		}
	}

	// Step 2: new files get the smallest free ID, in the
	// order of the sorted list.
	for (i = 0; i < s_nativeTrackCount; i++)
	{
		struct NativeTrackIdSlot *slot = NULL;

		if (s_trackIdForIndex[i] >= 0)
		{
			continue;
		}

		// A rejected container gets no NEW ID (format 4.1)
		// - the band has 35 slots, and a file that
		// cannot be raced should not take the last one away from a raceable one. One
		// already assigned is kept in step 1 as long as the file is in the folder:
		// a repaired version finds its old number again.
		if (!s_nativeTracks[i].ok)
		{
			Platform_Log("[CTR Tracks] no level id for '%s' - it is rejected (%s)\n", s_nativeTracks[i].file,
			             (s_nativeTracks[i].problem != NULL) ? s_nativeTracks[i].problem : "no reason given");
			rejected++;
			continue;
		}

		for (s = 0; s < NATIVE_TRACK_LEVELID_COUNT; s++)
		{
			if (!s_trackIdSlots[s].used)
			{
				slot = &s_trackIdSlots[s];
				break;
			}
		}

		if (slot == NULL)
		{
			Platform_LogWarn("[CTR Tracks] no free level id for '%s' - all %d are taken, this track cannot be started\n", s_nativeTracks[i].file,
			                 NATIVE_TRACK_LEVELID_COUNT);
			without++;
			continue;
		}

		slot->used = 1;
		slot->scanIndex = i;
		snprintf(slot->file, sizeof(slot->file), "%s", s_nativeTracks[i].file);

		if (s_nativeTracks[i].levdShaOk)
		{
			NativeTrackID_HexFromBytes(slot->sha, s_nativeTracks[i].levdSha);
		}
		else
		{
			snprintf(slot->sha, sizeof(slot->sha), "-");
		}

		s_trackIdForIndex[i] = (s16)(NATIVE_TRACK_LEVELID_FIRST + s);
		fresh++;
		dirty = 1;

		Platform_Log("[CTR Tracks] level id %d -> '%s' (new)\n", NATIVE_TRACK_LEVELID_FIRST + s, slot->file);
	}

	// One summary line per scan - so the proof of "restart without changes:
	// all IDs the same" is in every log.
	Platform_Log("[CTR Tracks] level ids: %d kept, %d new, %d freed, %d without an id, %d rejected without a new id\n", kept, fresh, freed, without,
	             rejected);

	if (!locked && dirty && havePath)
	{
		NativeTrackID_WriteFile(path);
	}
}

int NativeTrack_LevelForIndex(int index)
{
	if (!s_nativeTrackScanned)
	{
		NativeTrack_Scan();
	}

	if ((index < 0) || (index >= s_nativeTrackCount))
	{
		return -1;
	}

	return s_trackIdForIndex[index];
}

int NativeTrack_IndexForLevel(int levelID)
{
	const int s = levelID - NATIVE_TRACK_LEVELID_FIRST;

	if (!s_nativeTrackScanned)
	{
		NativeTrack_Scan();
	}

	if ((s < 0) || (s >= NATIVE_TRACK_LEVELID_COUNT) || !s_trackIdSlots[s].used)
	{
		return -1;
	}

	return s_trackIdSlots[s].scanIndex;
}

//----------------------------------------------------------------------------------------
// THE CUSTOM CUPS: cups.txt.
//
// Format and rules are in native_assets.h at NATIVE_CUP_FILE. It is read
// once per scan, AFTER the IDs are assigned: a cup names file names, and only
// the assignment says which ID is behind a name. Resolution uses
// the same comparison as the registry (NativeTrack_CompareNames, case-
// insensitive) - one identifier, one rule.
//----------------------------------------------------------------------------------------

global_variable struct NativeCup s_nativeCups[NATIVE_CUP_MAX];
global_variable int s_nativeCupCount = 0;

// The cup currently being read. broken: one of its lines was broken, the
// log named it, and it is dropped when finished.
struct NativeCupDraft
{
	int open;
	int broken;
	int tracks;
	struct NativeCup cup;
};

internal char *NativeCup_Trim(char *s)
{
	char *end;

	while ((*s == ' ') || (*s == '\t'))
	{
		s++;
	}

	end = s + strlen(s);

	while ((end > s) && ((end[-1] == ' ') || (end[-1] == '\t') || (end[-1] == '\r') || (end[-1] == '\n')))
	{
		end--;
	}

	*end = '\0';
	return s;
}

// A broken line: loud, with its number, and the cup it belongs to
// is dropped. Before the first "cup =" it belongs to none and is just
// skipped.
internal void NativeCup_Bad(struct NativeCupDraft *draft, int lineNo, const char *reason)
{
	if (draft->open)
	{
		Platform_LogWarn("[CTR Cups] %s line %d: %s - cup '%s' (line %d) is left out\n", NATIVE_CUP_FILE, lineNo, reason,
		                 (draft->cup.name[0] != '\0') ? draft->cup.name : "(no name)", draft->cup.line);
		draft->broken = 1;
	}
	else
	{
		Platform_LogWarn("[CTR Cups] %s line %d: %s - the line belongs to no cup and is ignored\n", NATIVE_CUP_FILE, lineNo, reason);
	}
}

// Finishing a cup: four tracks? Room on the screen? Then look up the
// containers - good or gray, and both into the log.
internal void NativeCup_Finish(struct NativeCupDraft *draft, int *leftOut, int *grey)
{
	struct NativeCup *cup = &draft->cup;
	int t;

	if (!draft->open)
	{
		return;
	}

	draft->open = 0;

	if (draft->broken)
	{
		(*leftOut)++;
		return;
	}

	if (draft->tracks != NATIVE_CUP_TRACKS)
	{
		Platform_LogWarn("[CTR Cups] %s line %d: cup '%s' has %d track(s), a cup needs exactly %d - the cup is left out\n", NATIVE_CUP_FILE, cup->line,
		                 cup->name, draft->tracks, NATIVE_CUP_TRACKS);
		(*leftOut)++;
		return;
	}

	// The screen has four boxes like the retail cup screen.
	if (s_nativeCupCount >= NATIVE_CUP_MAX)
	{
		Platform_LogWarn("[CTR Cups] %s line %d: cup '%s' is left out - the cup screen shows at most %d\n", NATIVE_CUP_FILE, cup->line, cup->name,
		                 NATIVE_CUP_MAX);
		(*leftOut)++;
		return;
	}

	cup->ok = 1;
	cup->problem[0] = '\0';

	for (t = 0; t < NATIVE_CUP_TRACKS; t++)
	{
		int i;

		cup->index[t] = -1;
		cup->levelID[t] = -1;

		for (i = 0; i < s_nativeTrackCount; i++)
		{
			if (NativeTrack_CompareNames(cup->file[t], s_nativeTracks[i].file) == 0)
			{
				cup->index[t] = i;
				cup->levelID[t] = s_trackIdForIndex[i];
				break;
			}
		}

		// The first reason counts; all four are resolved anyway, so that
		// the screen can show every track that is there.
		if (!cup->ok)
		{
			continue;
		}

		if (cup->index[t] < 0)
		{
			snprintf(cup->problem, sizeof(cup->problem), "track %d, '%s', is not in the folder", t + 1, cup->file[t]);
			cup->ok = 0;
		}
		else if (!s_nativeTracks[cup->index[t]].ok)
		{
			snprintf(cup->problem, sizeof(cup->problem), "track %d, '%s', was rejected by the reader (%s)", t + 1, cup->file[t],
			         (s_nativeTracks[cup->index[t]].problem != NULL) ? s_nativeTracks[cup->index[t]].problem : "no reason given");
			cup->ok = 0;
		}
		else if (cup->levelID[t] < 0)
		{
			snprintf(cup->problem, sizeof(cup->problem), "track %d, '%s', has no level id", t + 1, cup->file[t]);
			cup->ok = 0;
		}
		else if (!NativeTrack_OffersRace(cup->index[t]))
		{
			snprintf(cup->problem, sizeof(cup->problem), "track %d, '%s', does not declare Race - a cup is a race", t + 1, cup->file[t]);
			cup->ok = 0;
		}
	}

	s_nativeCups[s_nativeCupCount++] = *cup;

	if (cup->ok)
	{
		Platform_Log("[CTR Cups] cup '%s' (line %d): %d %s, %d %s, %d %s, %d %s\n", cup->name, cup->line, cup->levelID[0], cup->file[0], cup->levelID[1],
		             cup->file[1], cup->levelID[2], cup->file[2], cup->levelID[3], cup->file[3]);
	}
	else
	{
		Platform_LogWarn("[CTR Cups] cup '%s' (line %d) is greyed out and cannot be chosen: %s\n", cup->name, cup->line, cup->problem);
		(*grey)++;
	}
}

internal void NativeCup_Read(const char *dirPath)
{
	char path[NATIVE_ASSETS_PATH_MAX];
	char line[512];
	struct NativeCupDraft draft;
	int lineNo = 0;
	int leftOut = 0;
	int grey = 0;
	FILE *file;

	memset(s_nativeCups, 0, sizeof(s_nativeCups));
	memset(&draft, 0, sizeof(draft));
	s_nativeCupCount = 0;

	if (!NativePath_Join(path, sizeof(path), NativeStr8_FromCString(dirPath), NATIVE_STR8_LIT(NATIVE_CUP_FILE)))
	{
		return;
	}

	file = fopen(path, "r");

	if (file == NULL)
	{
		Platform_Log("[CTR Cups] no %s in the folder - NITRO CUP in NITRO-PIT stays locked\n", NATIVE_CUP_FILE);
		return;
	}

	while (fgets(line, sizeof(line), file) != NULL)
	{
		char *text = line;
		char *eq;
		char *key;
		char *value;
		char reason[160];

		lineNo++;

		// A line longer than the buffer: discard the rest and report it,
		// instead of reading it as the next line.
		if ((strchr(line, '\n') == NULL) && !feof(file))
		{
			int c;

			while (((c = fgetc(file)) != EOF) && (c != '\n'))
			{
			}

			NativeCup_Bad(&draft, lineNo, "a line longer than 510 characters");
			continue;
		}

		// A UTF-8 BOM, as the Windows editor writes it.
		if ((lineNo == 1) && ((u8)text[0] == 0xEF) && ((u8)text[1] == 0xBB) && ((u8)text[2] == 0xBF))
		{
			text += 3;
		}

		text = NativeCup_Trim(text);

		if ((*text == '\0') || (*text == '#'))
		{
			continue;
		}

		eq = strchr(text, '=');

		if (eq == NULL)
		{
			NativeCup_Bad(&draft, lineNo, "a line without '='");
			continue;
		}

		*eq = '\0';
		key = NativeCup_Trim(text);
		value = NativeCup_Trim(eq + 1);

		if (NativeTrack_CompareNames(key, "cup") == 0)
		{
			NativeCup_Finish(&draft, &leftOut, &grey);

			memset(&draft, 0, sizeof(draft));
			draft.open = 1;
			draft.cup.line = lineNo;

			if (*value == '\0')
			{
				NativeCup_Bad(&draft, lineNo, "a cup without a name");
			}
			else if (strlen(value) >= NATIVE_CUP_NAME_MAX)
			{
				snprintf(draft.cup.name, sizeof(draft.cup.name), "%s", value);
				snprintf(reason, sizeof(reason), "a cup name longer than %d characters", NATIVE_CUP_NAME_MAX - 1);
				NativeCup_Bad(&draft, lineNo, reason);
			}
			else
			{
				snprintf(draft.cup.name, sizeof(draft.cup.name), "%s", value);
			}
		}
		else if (NativeTrack_CompareNames(key, "track") == 0)
		{
			if (!draft.open)
			{
				NativeCup_Bad(&draft, lineNo, "a track before any 'cup ='");
			}
			else if (*value == '\0')
			{
				NativeCup_Bad(&draft, lineNo, "a track without a file name");
			}
			else if (strlen(value) >= sizeof(draft.cup.file[0]))
			{
				NativeCup_Bad(&draft, lineNo, "a file name longer than 127 characters");
			}
			else if (draft.tracks >= NATIVE_CUP_TRACKS)
			{
				NativeCup_Bad(&draft, lineNo, "a fifth track");
			}
			else
			{
				snprintf(draft.cup.file[draft.tracks], sizeof(draft.cup.file[0]), "%s", value);
				draft.tracks++;
			}
		}
		else
		{
			snprintf(reason, sizeof(reason), "an unknown key '%.40s' (known: cup, track)", key);
			NativeCup_Bad(&draft, lineNo, reason);
		}
	}

	fclose(file);
	NativeCup_Finish(&draft, &leftOut, &grey);

	Platform_Log("[CTR Cups] %d cup(s) from %s, %d greyed out, %d left out%s\n", s_nativeCupCount, NATIVE_CUP_FILE, grey, leftOut,
	             (s_nativeCupCount == 0) ? " - NITRO CUP in NITRO-PIT stays locked" : "");
}

int NativeCup_Count(void)
{
	if (!s_nativeTrackScanned)
	{
		NativeTrack_Scan();
	}

	return s_nativeCupCount;
}

const struct NativeCup *NativeCup_Get(int index)
{
	if (!s_nativeTrackScanned)
	{
		NativeTrack_Scan();
	}

	if ((index < 0) || (index >= s_nativeCupCount))
	{
		return NULL;
	}

	return &s_nativeCups[index];
}

// Folder of the last scan (tracks\ or --tracks-dir), empty before the first.
global_variable char s_nativeTrackDirPath[1024];

// Track preview (platform/native_preview.c): <folder>\vorschau\<file
// without .rldtrack>.rldprev. dirPath receives the vorschau folder.
int NativeTrack_PreviewPath(int index, char *path, int pathSize, char *dirPath, int dirPathSize)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	char stem[128];
	char name[160];
	size_t len;

	if ((entry == NULL) || (s_nativeTrackDirPath[0] == '\0'))
	{
		return 0;
	}

	snprintf(stem, sizeof(stem), "%s", entry->file);
	len = strlen(stem);
	if (len > 9)
	{
		const char *ext = ".rldtrack";
		size_t k;

		for (k = 0; (k < 9) && ((stem[len - 9 + k] | 0x20) == ext[k]); k++)
		{
		}

		if (k == 9)
		{
			stem[len - 9] = '\0';
		}
	}

	if (!NativePath_Join(dirPath, dirPathSize, NativeStr8_FromCString(s_nativeTrackDirPath), NATIVE_STR8_LIT("vorschau")))
	{
		return 0;
	}

	snprintf(name, sizeof(name), "%s.rldprev", stem);
	return NativePath_Join(path, pathSize, NativeStr8_FromCString(dirPath), NativeStr8_FromCString(name));
}

int NativeTrack_Scan(void)
{
	char dirPath[NATIVE_ASSETS_PATH_MAX];

	s_nativeTrackCount = 0;
	s_nativeTrackScanned = 1;
	s_nativeCupCount = 0;

	if (!g_cfg_tracks)
	{
		return 0;
	}

	if (!s_nativeAssetsInitialized && !NativeAssets_Init("."))
	{
		return 0;
	}

	if (s_nativeTrackFolder[0] == '\0')
	{
		if (!NativePath_Join(dirPath, sizeof(dirPath), NativeStr8_FromCString(s_nativeAssetsBaseDir), NATIVE_STR8_LIT(NATIVE_TRACK_DIR_NAME)))
		{
			return 0;
		}
	}
	else if ((s_nativeTrackFolder[0] == '/') || (s_nativeTrackFolder[0] == '\\') || ((s_nativeTrackFolder[0] != '\0') && (s_nativeTrackFolder[1] == ':')))
	{
		snprintf(dirPath, sizeof(dirPath), "%s", s_nativeTrackFolder);
	}
	else if (!NativePath_Join(dirPath, sizeof(dirPath), NativeStr8_FromCString(s_nativeAssetsBaseDir), NativeStr8_FromCString(s_nativeTrackFolder)))
	{
		// Said: a run without containers that should have had them measures
		// another menu.
		Platform_LogWarn("[CTR Tracks] --tracks-dir %s under %s is too long - no containers\n", s_nativeTrackFolder, s_nativeAssetsBaseDir);
		return 0;
	}

	// For the track preview (NativeTrack_PreviewPath): the same folder.
	snprintf(s_nativeTrackDirPath, sizeof(s_nativeTrackDirPath), "%s", dirPath);

	Platform_Log("[CTR Tracks] scanning %s%s\n", dirPath, (s_nativeTrackFolder[0] != '\0') ? " (--tracks-dir)" : "");

#if defined(_WIN32)
	{
		char searchPath[NATIVE_ASSETS_PATH_MAX];
		WIN32_FIND_DATAA findData;
		HANDLE findHandle;

		if (!NativePath_Join(searchPath, sizeof(searchPath), NativeStr8_FromCString(dirPath), NATIVE_STR8_LIT("*")))
		{
			return 0;
		}

		findHandle = FindFirstFileA(searchPath, &findData);
		if (findHandle == INVALID_HANDLE_VALUE)
		{
			Platform_Log("[CTR Tracks] no tracks folder - nothing to list\n");
			return 0;
		}

		do
		{
			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				continue;
			}

			NativeTrack_AddFile(dirPath, findData.cFileName);
		} while (FindNextFileA(findHandle, &findData) != 0);

		FindClose(findHandle);
	}
#else
	{
		struct dirent *dirEntry;
		DIR *dir = opendir(dirPath);

		if (dir == NULL)
		{
			Platform_Log("[CTR Tracks] no tracks folder - nothing to list\n");
			return 0;
		}

		while ((dirEntry = readdir(dir)) != NULL)
		{
			NativeTrack_AddFile(dirPath, dirEntry->d_name);
		}

		closedir(dir);
	}
#endif

	Platform_Log("[CTR Tracks] %d container(s) in the folder\n", s_nativeTrackCount);

	// First fix the order, then assign the IDs along it - see the
	// two blocks above NativeTrackID_Assign.
	if (s_nativeTrackCount > 1)
	{
		qsort(s_nativeTracks, (size_t)s_nativeTrackCount, sizeof(s_nativeTracks[0]), NativeTrack_CompareEntries);
	}

	NativeTrackID_Assign(dirPath);

	// Whoever does not make it into the RACE list is logged here - with mode and reason.
	{
		int k;

		for (k = 0; k < s_nativeTrackCount; k++)
		{
			if (NativeTrack_HiddenFromRace(k))
			{
				Platform_Log("[CTR Tracks] %s: not in the RACE list - modes %s, Race not declared\n", s_nativeTracks[k].file,
				             NativeTrack_ModesText(s_nativeTracks[k].modes));
			}
		}
	}

	// The cups name file names; only the assignment above says which ID is
	// behind them.
	NativeCup_Read(dirPath);

	return s_nativeTrackCount;
}

int NativeTrack_Count(void)
{
	if (!s_nativeTrackScanned)
	{
		NativeTrack_Scan();
	}

	return s_nativeTrackCount;
}

const struct NativeTrackEntry *NativeTrack_Get(int index)
{
	if ((index < 0) || (index >= s_nativeTrackCount))
	{
		return NULL;
	}

	return &s_nativeTracks[index];
}

//----------------------------------------------------------------------------------------
// SOUND FROM THE CONTAINER
//----------------------------------------------------------------------------------------
//
// WHAT HAPPENS HERE. A container track brings individual banks and sequences
// from KART.HWL in its SNDB chunk, and the chunk header says which
// ONE bank and which ONE sequence it plays. These two are slipped to the game
// here: the number at selection time (instead of data.levBank_FX[level]
// and data.levBank_Song[level]), and the sectors at read time (instead of the
// retail file).
//
// EXCLUSIVELY FOR CONTAINER TRACKS, and behind two switches that
// must both be on:
//
//   * s_nativeSoundOk - the loaded container has an SNDB at all, and
//     Rld_ParseSndb accepted it. Without a container this is 0.
//   * s_nativeSoundArmed - the level currently loading IS the host slot
//     of this container. It is set in NativeSound_ArmForLevel via
//     NativeTrack_ActiveForLevel, i.e. via exactly the question that the
//     camera path and the primitive memory ask as well.
//
// A disc track leaves both at 0. It goes through the same lines as
// before, fetches the same sectors from the same file and gets the same
// numbers from the same tables. The only extra thing it does is read a 0
// twice.
//
// WHY THE LAYOUT COMES FROM THE GAME SIDE. Where bank 3 starts in KART.HWL
// is stored in howl_bankOffsets - a table that howl_ParseHeader builds from the header
// of the RETAIL FILE. The container does not know it and is not supposed to
// know it: it names banks by their NUMBER, not by their position. The
// game side hands the tables over once (NativeSound_SetHowlLayout,
// at the end of howl_LoadHeader), and only then can a number be translated into a
// sector range.
//
// WHY ONLY THE TWO NAMED ENTRIES ARE REPLACED. An SNDB carries all
// entries that the patch changed - for Inferno Island four banks and
// three sequences. Which of them is the music is said by the header; the rest are
// what the packer chose from. If all of them were replaced,
// the longer ones would reach into the sector range of their SUCCESSOR -
// for Inferno Island bank 21 is three sectors longer than the retail slot
// it occupies, and would thereby cover the start of bank 22. That is why only
// the two that the container names explicitly are replaced.

global_variable u8 *s_nativeSoundChunk = NULL;
global_variable struct RldSndb s_nativeSound;
global_variable int s_nativeSoundOk = 0;
global_variable int s_nativeSoundArmed = 0;

// The retail layout of KART.HWL, handed over once by the game side.
// s_howlSpuRows points to the SpuAddrEntry table: two u16 per row,
// first spuAddr, then spuSize.
global_variable int s_howlFileIndex = -1;
global_variable const u16 *s_howlBankOffsets = NULL;
global_variable const u16 *s_howlSongOffsets = NULL;
global_variable int s_howlBankCount = 0;
global_variable int s_howlSongCount = 0;
global_variable u16 *s_howlSpuRows = NULL;
global_variable int s_howlSpuCount = 0;

// What was overwritten when switching on, in the original - so that switching off
// does not have to guess what was there before.
global_variable u16 s_spuSavedIndex[RLD_HOWL_SPUADDR_COUNT];
global_variable u16 s_spuSavedSize[RLD_HOWL_SPUADDR_COUNT];
global_variable int s_spuSavedCount = 0;

void NativeSound_SetHowlLayout(int fileIndex, const u16 *bankOffsets, int bankCount, const u16 *songOffsets, int songCount, u16 *spuRows,
                               int spuCount)
{
	s_howlFileIndex = fileIndex;
	s_howlBankOffsets = bankOffsets;
	s_howlBankCount = bankCount;
	s_howlSongOffsets = songOffsets;
	s_howlSongCount = songCount;
	s_howlSpuRows = spuRows;
	s_howlSpuCount = spuCount;
}

// ONLY spuSize, NOT spuAddr.
//
// The size is file content and is needed: Bank_AssignSpuAddrs computes
// from it how many sectors the bank has. Measured on Inferno Island,
// without this correction bank 3 would be at 151 instead of 149 sectors - the game
// would read past the end of the bank and give every sample after row 394
// the wrong place in the SPU.
//
// The address on the other hand is run-time state, not a statement of the file: in the file
// it is 0 in both versions, and Bank_AssignSpuAddrs fills it in itself at
// load time. Writing it would mean overwriting a live allocation.
// So the field stays in the format and is not touched here.
static void NativeSound_ApplySpuSizes(void)
{
	u32 i;

	s_spuSavedCount = 0;

	if ((s_howlSpuRows == NULL) || (s_nativeSoundOk == 0))
	{
		return;
	}

	for (i = 0; i < s_nativeSound.spuFixupCount; i++)
	{
		struct RldSndbSpuFixup fixup;

		Rld_SndbSpuFixup(&s_nativeSound, i, &fixup);

		if ((int)fixup.index >= s_howlSpuCount)
		{
			continue;
		}

		s_spuSavedIndex[s_spuSavedCount] = (u16)fixup.index;
		s_spuSavedSize[s_spuSavedCount] = s_howlSpuRows[fixup.index * 2u + 1u];
		s_spuSavedCount++;

		s_howlSpuRows[fixup.index * 2u + 1u] = (u16)fixup.spuSize;
	}
}

static void NativeSound_RestoreSpuSizes(void)
{
	int i;

	for (i = 0; i < s_spuSavedCount; i++)
	{
		if (s_howlSpuRows != NULL)
		{
			s_howlSpuRows[s_spuSavedIndex[i] * 2u + 1u] = s_spuSavedSize[i];
		}
	}

	s_spuSavedCount = 0;
}

static void NativeSound_Forget(void)
{
	NativeSound_RestoreSpuSizes();

	free(s_nativeSoundChunk);
	s_nativeSoundChunk = NULL;
	s_nativeSoundArmed = 0;
	s_nativeSoundOk = 0;
	memset(&s_nativeSound, 0, sizeof(s_nativeSound));
}

int NativeSound_BankForLevel(int levelID)
{
	if ((s_nativeSoundOk == 0) || !NativeTrack_ActiveForLevel(levelID))
	{
		return -1;
	}

	return (int)s_nativeSound.playBank;
}

int NativeSound_SongForLevel(int levelID)
{
	if ((s_nativeSoundOk == 0) || !NativeTrack_ActiveForLevel(levelID))
	{
		return -1;
	}

	return (int)s_nativeSound.playSong;
}

// THE VALUES OF THE TRACK (PARM, format 4.1).
//
// What a foreign track borrowed from the donor slot up to 4.0: reverb,
// bot strength, ambient sound. The defaults apply to EVERY loaded
// container, with or without PARM - unlike the
// sound, which without SNDB uses the slot's table. That is why only
// NativeTrack_ActiveForLevel is asked here and no "ok" flag.
#define NATIVE_PARM_DEFAULT_REVERB 2
#define NATIVE_PARM_DEFAULT_BOTROW 0
#define NATIVE_PARM_REVERB_OFF     5 // SetReverbMode: everything from 5 switches off

global_variable struct RldParm s_nativeParm;

// Once per load: the ambient sound is queried every frame, the line for it
// should appear only once in the log.
global_variable int s_nativeParmAmbientLogged = 0;

internal void NativeParm_Forget(void)
{
	memset(&s_nativeParm, 0, sizeof(s_nativeParm));
	s_nativeParmAmbientLogged = 0;
}

// Takes over what NativeTrack_Load read, and says in one line what
// applies - defaults included. Invalid values and a broken PARM are reported
// loudly, because the author set something that does not arrive.
internal void NativeParm_Take(const struct NativeTrackEntry *entry, const struct RldParm *parm, const char *problem)
{
	char reverbText[24];
	char ambientText[32];

	NativeParm_Forget();

	if ((parm != NULL) && (problem != NULL))
	{
		Platform_LogWarn("[CTR Tracks] %s: PARM unusable - %s; the defaults apply\n", entry->file, problem);
	}
	else if (parm != NULL)
	{
		s_nativeParm = *parm;

		if (parm->reverbState == RLD_PARM_INVALID)
		{
			Platform_LogWarn("[CTR Tracks] %s: PARM reverb %u is not 0..4 or off - the default %d applies\n", entry->file, parm->reverb,
			                 NATIVE_PARM_DEFAULT_REVERB);
		}
		if (parm->botsState == RLD_PARM_INVALID)
		{
			Platform_LogWarn("[CTR Tracks] %s: PARM bots %u is not a row 0..17 - the default row %d applies\n", entry->file, parm->bots,
			                 NATIVE_PARM_DEFAULT_BOTROW);
		}
		if (parm->ambientState == RLD_PARM_INVALID)
		{
			Platform_LogWarn("[CTR Tracks] %s: PARM ambient has the wrong length - no ambient sound\n", entry->file);
		}
		if (parm->unknownCount != 0u)
		{
			Platform_Log("[CTR Tracks] %s: PARM carries %u key(s) this build does not know (first %u) - skipped\n", entry->file, parm->unknownCount,
			             parm->firstUnknown);
		}
	}

	if (s_nativeParm.reverbState != RLD_PARM_SET)
	{
		snprintf(reverbText, sizeof(reverbText), "%d (default)", NATIVE_PARM_DEFAULT_REVERB);
	}
	else if (s_nativeParm.reverb == RLD_PARM_REVERB_OFF)
	{
		snprintf(reverbText, sizeof(reverbText), "off");
	}
	else
	{
		snprintf(reverbText, sizeof(reverbText), "%u", s_nativeParm.reverb);
	}

	if (s_nativeParm.ambientState != RLD_PARM_SET)
	{
		snprintf(ambientText, sizeof(ambientText), "none (default)");
	}
	else
	{
		snprintf(ambientText, sizeof(ambientText), "0x%x/0x%x", s_nativeParm.ambient[0], s_nativeParm.ambient[1]);
	}

	Platform_Log("[CTR Tracks] %s: track values - reverb %s, bots row %u%s, ambient %s\n", entry->file, reverbText,
	             (s_nativeParm.botsState == RLD_PARM_SET) ? s_nativeParm.bots : (u32)NATIVE_PARM_DEFAULT_BOTROW,
	             (s_nativeParm.botsState == RLD_PARM_SET) ? "" : " (default)", ambientText);
}

int NativeParm_ReverbForLevel(int levelID)
{
	if (!NativeTrack_ActiveForLevel(levelID))
	{
		return -1;
	}

	if (s_nativeParm.reverbState != RLD_PARM_SET)
	{
		return NATIVE_PARM_DEFAULT_REVERB;
	}

	return (s_nativeParm.reverb == RLD_PARM_REVERB_OFF) ? NATIVE_PARM_REVERB_OFF : (int)s_nativeParm.reverb;
}

int NativeParm_BotRowForLevel(int levelID)
{
	if (!NativeTrack_ActiveForLevel(levelID))
	{
		return -1;
	}

	return (s_nativeParm.botsState == RLD_PARM_SET) ? (int)s_nativeParm.bots : NATIVE_PARM_DEFAULT_BOTROW;
}

int NativeParm_AmbientForLevel(int levelID, int slot)
{
	int sound;

	if (!NativeTrack_ActiveForLevel(levelID) || (slot < 0) || (slot > 1))
	{
		return -1;
	}

	sound = (s_nativeParm.ambientState == RLD_PARM_SET) ? (int)s_nativeParm.ambient[slot] : 0;

	if (!s_nativeParmAmbientLogged)
	{
		s_nativeParmAmbientLogged = 1;
		Platform_Log("[CTR Sound] level %d: ambient sound from the container - 0x%x / 0x%x%s\n", levelID,
		             (s_nativeParm.ambientState == RLD_PARM_SET) ? s_nativeParm.ambient[0] : 0u,
		             (s_nativeParm.ambientState == RLD_PARM_SET) ? s_nativeParm.ambient[1] : 0u,
		             (s_nativeParm.ambientState == RLD_PARM_SET) ? "" : " (default: none, the seat's table is not used)");
	}

	return sound;
}

// Switching on and off, on exactly one question: is this level the host slot.
//
// This is called at the top of LOAD_TenStages, i.e. on every load and every
// stage. It is idempotent on purpose: the state only changes when the
// answer changes, and then there is a line in the log. The switch
// disc -> container -> disc thus works out correctly by itself - the way back
// is the same call with a different answer.
void NativeSound_ArmForLevel(int levelID)
{
	const int want = (s_nativeSoundOk != 0) && NativeTrack_ActiveForLevel(levelID);

	// Which level is loading, for NativeTrack_ActiveLevdSha256 - every load
	// passes here, disc track or container, with or without sound.
	s_nativeTrackLevelNow = levelID;

	if (want == s_nativeSoundArmed)
	{
		return;
	}

	if (want != 0)
	{
		NativeSound_ApplySpuSizes();
		s_nativeSoundArmed = 1;
		Platform_Log("[CTR Sound] '%s' on level %d plays bank %u and sequence %u out of the container; %d SPU size(s) corrected\n",
		             NativeTrack_LoadedName(), levelID, s_nativeSound.playBank, s_nativeSound.playSong, s_spuSavedCount);
	}
	else
	{
		s_nativeSoundArmed = 0;
		NativeSound_RestoreSpuSizes();
		Platform_Log("[CTR Sound] level %d is not the container's seat - stock sound, SPU sizes back to the disc values\n", levelID);
	}
}

// The fourth read path. Returns the bytes that should be handed out instead of
// the retail sectors, or NULL.
//
// NULL always means "not responsible", never "broken": every way out of
// here leads to the ordinary read path doing exactly what it would do without
// this file.
const u8 *NativeSound_SectorData(int fileIndex, u32 firstSector, u32 sectorCount)
{
	u32 i;

	if ((s_nativeSoundArmed == 0) || (sectorCount == 0u) || (fileIndex != s_howlFileIndex))
	{
		return NULL;
	}

	for (i = 0; i < s_nativeSound.entryCount; i++)
	{
		struct RldSndbEntry entry;
		u32 start;
		u32 sectors;
		u32 within;

		Rld_SndbEntry(&s_nativeSound, i, &entry);

		if (entry.kind == RLD_SNDB_KIND_BANK)
		{
			if ((entry.index != s_nativeSound.playBank) || ((int)entry.index >= s_howlBankCount) || (s_howlBankOffsets == NULL))
			{
				continue;
			}

			start = s_howlBankOffsets[entry.index];
		}
		else
		{
			if ((entry.index != s_nativeSound.playSong) || ((int)entry.index >= s_howlSongCount) || (s_howlSongOffsets == NULL))
			{
				continue;
			}

			start = s_howlSongOffsets[entry.index];
		}

		if (firstSector < start)
		{
			continue;
		}

		within = firstSector - start;
		sectors = entry.size / RLD_HOWL_SECTOR;

		if (within >= sectors)
		{
			continue;
		}

		// Half inside is not inside. A read request that starts in this entry
		// and reaches past it would need retail bytes at the end
		// - the halves would then come from two different
		// versions of the same bank. Better not to replace at all and leave the
		// track silent than half.
		if (sectorCount > (sectors - within))
		{
			Platform_LogWarn("[CTR Sound] read of %u sector(s) at %u runs past the %u sector(s) the container carries for %s %u - not substituted\n",
			                 sectorCount, firstSector, sectors, (entry.kind == RLD_SNDB_KIND_BANK) ? "bank" : "sequence", entry.index);
			return NULL;
		}

		return &s_nativeSound.payload[entry.offset + within * RLD_HOWL_SECTOR];
	}

	return NULL;
}

void NativeTrack_Release(void)
{
	NativeSound_Forget();
	NativeParm_Forget();

	free(s_nativeTrackLev);
	free(s_nativeTrackVrm);

	s_nativeTrackLev = NULL;
	s_nativeTrackVrm = NULL;
	s_nativeTrackLevSize = 0;
	s_nativeTrackVrmSize = 0;
	s_nativeTrackLoaded = -1;
	s_nativeTrackLevSubfile = -1;
	s_nativeTrackVrmSubfile = -1;
	s_nativeTrackDonorLevel = -1;
}

// Everything the load takes out of the file, before any of it becomes game
// state. NativeTrack_Load and NativeTrack_SelfTestFile read through the same
// function, so the self-test checks exactly what the game checks.
struct NativeTrackRead
{
	struct RldMeta meta;
	u8 *lev;
	size_t levSize;
	u8 *vrm;
	size_t vrmSize;
	u8 levdSha[32];

	struct RldSndb sound;
	u8 *sndb;
	size_t sndbSize;
	const char *soundProblem;

	struct RldParm parm;
	int parmFound;
	const char *parmProblem;

	int memoryRefused;
};

// ---------------------------------------------------------------------------
// THE RACE TABLES OF THE LEV, held against what the race reads without a check.
//
// Runs after Rld_CheckLev, which has proven the body, the pointer map, every
// pointer field of struct Level (NULL or mapped, pointing into the body) and
// the spawn table (count 0..16, slots 2 and 3 set where CAM.c needs them).
// What it did not look at are the numbers INSIDE those tables that the race
// uses as indices or divisors:
//
//   restart points  (struct CheckpointNode, 12 bytes; Level 0x148 count, 0x14c
//                   table) - VehLap, the mask grab (VehStuckProc) and the
//                   warpball index the table with nextIndex_* of the nodes and
//                   with the checkpointIndex of the quadblocks, and divide by
//                   distToFinish of node 0 (VehLap, RB_Warpball: % length).
//   end-of-race cameras  (spawn slot 2) - CAM.c steps through the entries by
//                   data.EndOfRace_Camera_Size[mode] and indexes the restart
//                   points with each entry's respawn point (and, in modes 9
//                   and 13, with the first data word).
//   the map table   (spawn slot 0, struct UIMapSpawnMetadata) -
//                   UI_Map_GetIconPos divides by worldEnd - worldStart.
//
// Measured on 2026-09-30 on the 20 real containers at hand: 0 to 159 restart
// points, every nextIndex_forward inside, every other link inside or 0xff,
// node 0 with a distance of 5,440 or more, every quadblock checkpoint 0xff or
// inside, no end-of-race camera table at all, map ranges of 4,133 and more.
// NOT a rule: a race track without restart points. Five real arena
// containers declare Race with 0 points, so that case is handled in the race
// instead (VehLap and VehStuckProc already skip the table, VehPickupItem
// skips the warpball).
// ---------------------------------------------------------------------------
#define NATIVE_LEV_RESTART_COUNT   0x148u
#define NATIVE_LEV_RESTART_TABLE   0x14cu
#define NATIVE_LEV_RESTART_MAX     0xffu // u8 indices, 0xff means "none"
#define NATIVE_LEV_NODE_BYTES      12u   // struct CheckpointNode
#define NATIVE_LEV_NODE_DISTANCE   6u    // CheckpointNode.distToFinish, u16
#define NATIVE_LEV_NODE_FORWARD    8u    // nextIndex_forward, _left, _backward, _right
#define NATIVE_LEV_MESH_QUADS      0x0cu // mesh_info.ptrQuadBlockArray
#define NATIVE_LEV_QUAD_CHECKPOINT 0x3eu // QuadBlock.checkpointIndex, u8
#define NATIVE_LEV_SPAWN_TABLE     0x134u
#define NATIVE_LEV_UIMAP_BYTES     0x14u // struct UIMapSpawnMetadata

// Whether the pointer map lists this body offset. Rld_CheckLev has proven the
// map lies inside the LEV.
internal int NativeTrack_LevMapped(const u8 *lev, u32 site)
{
	const size_t mapAt = NATIVE_TRACK_LEV_BODY + (size_t)Rld_ReadLE32(&lev[0]);
	const u32 count = Rld_ReadLE32(&lev[mapAt]) / 4u;
	u32 i;

	for (i = 0; i < count; i++)
	{
		if ((Rld_ReadLE32(&lev[mapAt + 4u + ((size_t)i * 4u)]) & ~3u) == site)
		{
			return 1;
		}
	}

	return 0;
}

internal int NativeTrack_LevS16(const u8 *lev, u32 offset)
{
	const u32 raw = Rld_ReadLE16(&lev[NATIVE_TRACK_LEV_BODY + (size_t)offset]);

	return (raw >= 0x8000u) ? ((int)raw - 0x10000) : (int)raw;
}

internal const char *NativeTrack_CheckLevRace(const u8 *lev)
{
	const u32 body = Rld_ReadLE32(&lev[0]);
	const u32 mesh = NativeTrack_LevField(lev, 0x00u);
	const u32 spawn = NativeTrack_LevField(lev, NATIVE_LEV_SPAWN_TABLE);
	const int nodes = (int)NativeTrack_LevField(lev, NATIVE_LEV_RESTART_COUNT);
	u32 table = 0;
	u32 spawnCount;
	int i;

	// The restart points.
	if ((nodes < 0) || (nodes > (int)NATIVE_LEV_RESTART_MAX))
	{
		return "the LEV restart point count (cnt_restart_points) is outside 0..255";
	}

	if (nodes > 0)
	{
		int link;

		table = NativeTrack_LevField(lev, NATIVE_LEV_RESTART_TABLE);
		if (!NativeTrack_LevMapped(lev, NATIVE_LEV_RESTART_TABLE) || (((u64)table + ((u64)nodes * NATIVE_LEV_NODE_BYTES)) > (u64)body))
		{
			return "the LEV restart points (ptr_restart_points) do not lie inside the level body";
		}

		for (i = 0; i < nodes; i++)
		{
			const u8 *node = &lev[NATIVE_TRACK_LEV_BODY + (size_t)table + ((size_t)i * NATIVE_LEV_NODE_BYTES)];

			if (node[NATIVE_LEV_NODE_FORWARD] >= (u32)nodes)
			{
				return "a LEV restart point leads forward to a restart point that does not exist";
			}

			for (link = 1; link < 4; link++)
			{
				const u32 next = node[NATIVE_LEV_NODE_FORWARD + (u32)link];

				if ((next != NATIVE_LEV_RESTART_MAX) && (next >= (u32)nodes))
				{
					return "a LEV restart point branches to a restart point that does not exist";
				}
			}
		}

		if (Rld_ReadLE16(&lev[NATIVE_TRACK_LEV_BODY + (size_t)table + NATIVE_LEV_NODE_DISTANCE]) == 0u)
		{
			return "the LEV restart point 0 has distToFinish 0 - the lap and the warpball divide by it";
		}
	}

	// The checkpoint of every quadblock: 0xff or a restart point.
	if ((mesh != 0u) && (((u64)mesh + 0x20u) <= (u64)body))
	{
		const int quads = (int)NativeTrack_LevField(lev, mesh);

		if (quads > 0)
		{
			const u32 array = NativeTrack_LevField(lev, mesh + NATIVE_LEV_MESH_QUADS);

			if (!NativeTrack_LevMapped(lev, mesh + NATIVE_LEV_MESH_QUADS) || (((u64)array + ((u64)quads * RLD_QUADBLOCK_BYTES)) > (u64)body))
			{
				return "the LEV quadblocks (ptrQuadBlockArray) do not lie inside the level body";
			}

			for (i = 0; i < quads; i++)
			{
				const u32 checkpoint = lev[NATIVE_TRACK_LEV_BODY + (size_t)array + ((size_t)i * RLD_QUADBLOCK_BYTES) + NATIVE_LEV_QUAD_CHECKPOINT];

				if ((checkpoint != NATIVE_LEV_RESTART_MAX) && (checkpoint >= (u32)nodes))
				{
					return "a LEV quadblock names a restart point (checkpointIndex) that does not exist";
				}
			}
		}
	}

	// The spawn table: Rld_CheckLev has proven it is mapped, inside the body,
	// with count 0..16 and every slot NULL or a pointer into the body.
	spawnCount = NativeTrack_LevField(lev, spawn);

	// Slot 0, the map table.
	if (spawnCount > 0u)
	{
		const u32 map = NativeTrack_LevField(lev, spawn + 4u);

		if (map != 0u)
		{
			if (((u64)map + NATIVE_LEV_UIMAP_BYTES) > (u64)body)
			{
				return "the LEV map table (spawn slot 0) runs past the level body";
			}

			// worldEndX/Y at 0x0 / 0x2, worldStartX/Y at 0x4 / 0x6.
			if ((NativeTrack_LevS16(lev, map + 0u) == NativeTrack_LevS16(lev, map + 4u)) ||
			    (NativeTrack_LevS16(lev, map + 2u) == NativeTrack_LevS16(lev, map + 6u)))
			{
				return "the LEV map table (spawn slot 0) has a width or height of 0 - the map divides by it";
			}
		}
	}

	// Slot 2, the end-of-race cameras: s16 count, then per camera s16 respawn
	// point, s16 mode, and data.EndOfRace_Camera_Size[|mode|] bytes of data.
	if (spawnCount > 2u)
	{
		const int sizeCount = (int)(sizeof(data.EndOfRace_Camera_Size) / sizeof(data.EndOfRace_Camera_Size[0]));
		const u32 cameras = NativeTrack_LevField(lev, spawn + 4u + 8u);
		int count;
		u64 at;

		if (((u64)cameras + 2u) > (u64)body)
		{
			return "the LEV end-of-race camera table runs past the level body";
		}

		count = NativeTrack_LevS16(lev, cameras);
		if (count < 0)
		{
			return "the LEV end-of-race camera count is negative";
		}

		at = (u64)cameras + 2u;
		for (i = 0; i < count; i++)
		{
			int respawn;
			int mode;
			int size;

			if ((at + 4u) > (u64)body)
			{
				return "the LEV end-of-race camera table runs past the level body";
			}

			respawn = NativeTrack_LevS16(lev, (u32)at);
			mode = NativeTrack_LevS16(lev, (u32)at + 2u);
			mode = (mode < 0) ? -mode : mode;

			if ((mode >= sizeCount) || (data.EndOfRace_Camera_Size[mode] < 0))
			{
				return "a LEV end-of-race camera has a mode the game does not know";
			}

			size = data.EndOfRace_Camera_Size[mode];
			if ((at + 4u + (u64)size) > (u64)body)
			{
				return "the LEV end-of-race camera table runs past the level body";
			}

			if ((respawn < 0) || (respawn >= nodes))
			{
				return "a LEV end-of-race camera names a restart point that does not exist";
			}

			// Modes 9 and 13 follow the track path from the restart point in
			// their first data word (CAM.c, trackPathNode).
			if (((mode == 9) || (mode == 13)) && (size >= 2))
			{
				const int path = NativeTrack_LevS16(lev, (u32)at + 4u);

				if ((path < 0) || (path >= nodes))
				{
					return "a LEV end-of-race camera follows the path from a restart point that does not exist";
				}
			}

			at += 4u + (u64)size;
		}
	}

	return NULL;
}

internal void NativeTrack_FreeRead(struct NativeTrackRead *read)
{
	free(read->lev);
	free(read->vrm);
	free(read->sndb);
	read->lev = NULL;
	read->vrm = NULL;
	read->sndb = NULL;
}

// Takes out LEVD and VRMD. Only here - nobody touched them before.
//
// There used to be a second signature check here, on the grounds that any
// number of minutes can pass between listing and loading and the file may have
// been swapped in between. The reason still holds, and so does the check that
// covers it: Rld_ReadChunk checks EVERY chunk against its SHA-256,
// exactly at the moment it is fetched. That was already the check that
// would have caught a swapped LEVD - the signature only covered the
// declaration.
//
// And the hash is not the end of it (2026-09-30): it carries no key, anyone
// can recompute it for broken bytes. So LEVD, VRMD and the bank headers in
// SNDB are held against what the load path relies on (Rld_CheckLev,
// Rld_CheckVrm, Rld_CheckSndbBanks in rldtrack.inc, and the race tables in
// NativeTrack_CheckLevRace above) before any of them reaches it. A refusal
// there is DAMAGED, like a hash mismatch.
//
// NULL = everything read and accepted; the caller owns what is in `read`.
// Otherwise the reason, and `read` holds no memory. entry->note may carry the
// text (the memory refusal names numbers).
internal const char *NativeTrack_ReadAll(struct NativeTrackEntry *entry, struct NativeTrackRead *read)
{
	struct RldReader reader;
	const char *error;
	u8 *metaData;
	size_t metaSize;
	int metaIndex = -1;
	int levIndex = -1;
	int vrmIndex = -1;

	// Zeroed before any branch can fill them. The flow below leaves
	// `meta` unread only if `error` is set - but that depends on
	// four interlocking branches, and whoever has to read up on that to
	// know whether there is garbage here reads one line too many.
	memset(read, 0, sizeof(*read));

	error = Rld_Open(&reader, entry->path);
	if (error == NULL)
	{
		if (Rld_FindEntry(&reader, "META", &metaIndex) == NULL)
		{
			error = "META is missing - required chunk";
		}
	}

	if (error == NULL)
	{
		metaData = Rld_ReadChunk(&reader, metaIndex, &metaSize, &error);
		if (metaData != NULL)
		{
			error = Rld_ParseMeta(&read->meta, metaData, metaSize);
			free(metaData);
		}
	}

	if ((error == NULL) && (Rld_FindEntry(&reader, "LEVD", &levIndex) == NULL))
	{
		error = "LEVD is missing - required chunk";
	}

	if ((error == NULL) && (Rld_FindEntry(&reader, "VRMD", &vrmIndex) == NULL))
	{
		error = "VRMD is missing - required chunk";
	}

	if (error == NULL)
	{
		read->lev = Rld_ReadChunk(&reader, levIndex, &read->levSize, &error);
	}

	// The hash Rld_ReadChunk has just proven for these bytes - the stamp of the
	// track that is raced (NativeTrack_ActiveLevdSha256). From this open of the
	// file, not from the scan: the file may have been swapped in between.
	if (error == NULL)
	{
		memcpy(read->levdSha, &reader.directory[(levIndex * RLD_DIR_ENTRY_SIZE) + RLD_DIR_HASH_OFFSET], sizeof(read->levdSha));
		error = Rld_CheckLev(read->lev, read->levSize);
	}

	if (error == NULL)
	{
		error = NativeTrack_CheckLevRace(read->lev);
	}

	if (error == NULL)
	{
		read->vrm = Rld_ReadChunk(&reader, vrmIndex, &read->vrmSize, &error);
	}

	if (error == NULL)
	{
		error = Rld_CheckVrm(read->vrm, read->vrmSize);
	}

	// THE MEMORY NUMBERS ARE BINDING (format 4.1).
	//
	// The reserve behind the pack was computed from META before MEMPACK_Init
	// (NativeTrack_MempackExtraNeeded) - with the numbers of this entry,
	// as the scan read them. If the LEV needs more, loading would run into
	// "OUT OF MEMORY" and the red screen (MEMPACK.c:95). Here it is
	// said beforehand, loudly and with both numbers, and the track stays gray.
	if (error == NULL)
	{
		struct RldMemNeed need;

		Rld_MemNeed(&need, read->lev, read->levSize);

		if ((need.total > entry->memTotal) || (need.primBytes > entry->primBytes))
		{
			snprintf(entry->note, sizeof(entry->note),
			         "META understates the memory need - the LEV needs %u bytes (draw %u), META says %u (draw %u); pack it again with Reload Studio",
			         need.total, need.primBytes, entry->memTotal, entry->primBytes);
			error = entry->note;
			read->memoryRefused = 1;
		}
	}

	// SNDB IS OPTIONAL, and its absence is not a fault.
	//
	// Rld_FindEntry searches by type, not by position. A
	// four-part container returns NULL here, and that is an answer: the
	// track runs, it just brings no sound of its own.
	//
	// For the same reason, an SNDB that Rld_ParseSndb rejects only costs the
	// sound. The reason is reported - a silent track without explanation
	// would be a question nobody can answer.
	//
	// NOT SO for the bank headers inside an accepted SNDB (Rld_CheckSndbBanks):
	// the game would index the SPU table with them and write there. That is
	// damage to the file, not a missing sound - the container is refused.
	// The host's SPU sizes come from KART.HWL; NativeTrack_Release has put
	// them back to the retail values before this runs.
	if (error == NULL)
	{
		int sndbIndex = -1;

		if (Rld_FindEntry(&reader, "SNDB", &sndbIndex) != NULL)
		{
			u8 *sndbData = Rld_ReadChunk(&reader, sndbIndex, &read->sndbSize, &read->soundProblem);

			if (sndbData != NULL)
			{
				read->soundProblem = Rld_ParseSndb(&read->sound, sndbData, read->sndbSize);
			}

			if ((sndbData != NULL) && (read->soundProblem == NULL))
			{
				error = Rld_CheckSndbBanks(&read->sound, (const unsigned short *)s_howlSpuRows, (s_howlSpuCount > 0) ? (u32)s_howlSpuCount : 0u);
			}

			if ((error == NULL) && (read->soundProblem == NULL))
			{
				read->sndb = sndbData;
			}
			else
			{
				free(sndbData);
				memset(&read->sound, 0, sizeof(read->sound));
			}
		}
	}

	// PARM IS OPTIONAL like SNDB: a broken one only costs the values, the
	// defaults apply then.
	if (error == NULL)
	{
		int parmIndex = -1;

		if (Rld_FindEntry(&reader, "PARM", &parmIndex) != NULL)
		{
			size_t parmSize = 0;
			u8 *parmData = Rld_ReadChunk(&reader, parmIndex, &parmSize, &read->parmProblem);

			read->parmFound = 1;
			if (parmData != NULL)
			{
				read->parmProblem = Rld_ParseParm(&read->parm, parmData, parmSize);
				free(parmData);
			}
		}
	}

	Rld_Close(&reader);

	if (error != NULL)
	{
		NativeTrack_FreeRead(read);
	}

	return error;
}

int NativeTrack_Load(int index, int donorLevelID)
{
	struct NativeTrackEntry *entry;
	struct NativeTrackRead read;
	const char *error;

	if ((index < 0) || (index >= s_nativeTrackCount))
	{
		return 0;
	}

	entry = &s_nativeTracks[index];
	NativeTrack_Release();

	error = NativeTrack_ReadAll(entry, &read);

	if (error != NULL)
	{
		entry->ok = 0;
		entry->refusal = read.memoryRefused ? NATIVE_TRACK_REFUSAL_MEMORY : NATIVE_TRACK_REFUSAL_DAMAGED;
		entry->problem = error;
		Platform_LogError("[CTR Tracks] %s: NOT LOADED - %s\n", entry->file, error);
		return 0;
	}

	// Only here and not while listing: for that the list would have to read LEVD,
	// and that is exactly what the list rule forbids.
	//
	// Reported, not refused. See above for why.
	{
		const int missing = NativeTrack_MissingLevTables(entry, read.lev, read.levSize);

		if (missing != 0)
		{
			Platform_Log("[CTR Tracks] %s: %d field(s) that every disc track fills are empty here:\n", entry->file, missing);
			Platform_Log("[CTR Tracks]   %s\n", entry->note);
			Platform_Log("[CTR Tracks]   All 18 arcade tracks and all 5 arenas of the NTSC-U disc fill every one.\n");
			Platform_Log("[CTR Tracks]   Six of them are read NOWHERE in this tree, and the three that are read\n");
			Platform_Log("[CTR Tracks]   are guarded against null. So this is a difference, not a diagnosis -\n");
			Platform_Log("[CTR Tracks]   loading anyway. If it dies, the load stage above says where.\n");
		}
	}

	s_nativeTrackLev = read.lev;
	s_nativeTrackLevSize = (u32)read.levSize;
	s_nativeTrackVrm = read.vrm;
	s_nativeTrackVrmSize = (u32)read.vrmSize;
	memcpy(s_nativeTrackLevdSha, read.levdSha, sizeof(s_nativeTrackLevdSha));
	s_nativeTrackLoaded = index;
	s_nativeTrackDonorLevel = donorLevelID;

	// The sound, if there was one. NativeTrack_Release already cleaned up
	// above; here we only take what came along this time.
	if (read.sndb != NULL)
	{
		s_nativeSoundChunk = read.sndb;
		s_nativeSound = read.sound;
		s_nativeSoundOk = 1;
		Platform_Log("[CTR Sound] %s: SNDB %u bytes, %u entries, plays bank %u and sequence %u\n", entry->file, (u32)read.sndbSize,
		             s_nativeSound.entryCount, s_nativeSound.playBank, s_nativeSound.playSong);
	}
	else if (read.soundProblem != NULL)
	{
		Platform_LogWarn("[CTR Sound] %s: no sound from the container - %s\n", entry->file, read.soundProblem);
	}

	NativeParm_Take(entry, read.parmFound ? &read.parm : NULL, read.parmProblem);

	Platform_Log("[CTR Tracks] %s: LEVD %u bytes, VRMD %u bytes, in memory - nothing written to disk\n", entry->file, s_nativeTrackLevSize,
	             s_nativeTrackVrmSize);
	Platform_Log("[CTR Tracks] '%s' sits in level slot %d. The name is the identity, the slot is only the seat.\n", entry->name, donorLevelID);
	return 1;
}

// The slot the loaded container got - the stored one, not
// computed a second time. -1 if none is loaded. Used by the level funnel in
// MM_NativeMenu.c and by --autoload-track, which after MM_NativeTracks_LoadRow
// must know which level to start, without copying the slot rule.
int NativeTrack_LoadedDonorLevel(void)
{
	return (s_nativeTrackLoaded >= 0) ? s_nativeTrackDonorLevel : -1;
}

int NativeTrack_LoadedIndex(void)
{
	return s_nativeTrackLoaded;
}

// ---------------------------------------------------------------------------
//  THE MINIMAP OF A CONTAINER, for the track screen of NITRO-PIT
//  (game/230/MM_NativeTrackSelect.c).
//
//  In the race the map comes from gGT->ptrIcons[3] and [4] (UI_RenderFrame.c,
//  UI_Map_DrawMap), and they are there because DecalGlobal_Store sorts the icons of
//  the levTexLookup in by their global_IconArray_Index. Their texels and
//  color tables are in the track's VRM - not loaded in the menu. Here
//  both are read from the container without loading it: the container
//  that may currently be armed stays untouched.
//
//  SINCE 4.1 ONE SOURCE: LEV and VRM. If the map is taller than the free
//  strip of the menu (32 rows, Rld_MapWhyNotMenu), Rld_ScaleMap scales it
//  down here, the first time the row is shown - measured 0.68 ms (Inferno Island)
//  and 0.87 ms (Sunset Vista), byte-identical to the MMAP that rldpack put into
//  containers before 4.1. An MMAP in old containers is skipped. Whether the map fits in the end is decided by
//  MM_NativeTrackSelect_MapPlace. The race reads the LEV's map as it is.
//
//  Reading and scaling live in include/rldtrack.inc (Rld_LevMap,
//  Rld_ScaleMap): this way the packer shows in its report what the menu will show,
//  and two copies would be exactly the pattern from the header of that file. Everything comes from the file and is
//  checked against the file: every offset and every size against the chunk size.
// ---------------------------------------------------------------------------

void NativeTrack_FreeMinimap(struct NativeTrackMinimap *map)
{
	int k;

	for (k = 0; k < 2; k++)
	{
		free(map->half[k].texels);
		map->half[k].texels = NULL;
	}
}

// Takes over what Rld_LevMap read or Rld_ScaleMap scaled down. The
// texels change owner, NativeTrack_FreeMinimap releases them.
internal void NativeMinimap_Take(struct NativeTrackMinimap *out, struct RldMapHalf half[2], int scaled)
{
	int k;

	for (k = 0; k < 2; k++)
	{
		struct NativeTrackMinimapHalf *dst = &out->half[k];

		memcpy(dst->layout, half[k].layout, sizeof(dst->layout));
		dst->depth = half[k].depth;
		dst->srcX = half[k].srcX;
		dst->srcY = half[k].srcY;
		dst->w = half[k].w;
		dst->h = half[k].h;
		dst->texelBaseU = half[k].texelBaseU;
		dst->texelBaseV = half[k].texelBaseV;
		dst->texels = (u16 *)half[k].texels;
		dst->clutX = half[k].clutX;
		dst->clutY = half[k].clutY;
		dst->clutW = half[k].clutW;
		memcpy(dst->clut, half[k].clut, sizeof(dst->clut));
		half[k].texels = NULL;
	}

	out->scaled = scaled;
}

// The map of one file: LEV and VRM out of the container, Rld_LevMap, and
// Rld_ScaleMap if it is too tall. The track wheel and the self-test read it
// through here. NULL = out holds both halves.
internal const char *NativeMinimap_ReadPath(const char *path, struct NativeTrackMinimap *out)
{
	struct RldMapHalf half[2];
	struct RldReader reader;
	const char *error = NULL;
	u8 *lev = NULL;
	u8 *vrm = NULL;
	size_t levSize = 0;
	size_t vrmSize = 0;
	int levIndex = -1;
	int vrmIndex = -1;
	int scaled = 0;

	error = Rld_Open(&reader, path);

	// Rld_Open already required LEVD and VRMD; so FindEntry finds them.
	if (error == NULL)
	{
		Rld_FindEntry(&reader, "LEVD", &levIndex);
		Rld_FindEntry(&reader, "VRMD", &vrmIndex);
		lev = Rld_ReadChunk(&reader, levIndex, &levSize, &error);
	}

	if (error == NULL)
	{
		vrm = Rld_ReadChunk(&reader, vrmIndex, &vrmSize, &error);
	}

	Rld_Close(&reader);

	if (error == NULL)
	{
		error = Rld_LevMap(lev, levSize, vrm, vrmSize, half);
	}

	free(lev);
	free(vrm);

	if (error != NULL)
	{
		return error;
	}

	// Too tall for the strip: scale it down. If it cannot be
	// scaled down (15 bit), the LEV's map stays, and MapPlace says that
	// it does not fit - as up to 4.0 without MMAP.
	if (Rld_MapWhyNotMenu(half) != NULL)
	{
		struct RldMapHalf small[2];

		if (Rld_ScaleMap(half, small) == NULL)
		{
			Rld_FreeMap(half);
			memcpy(half, small, sizeof(small));
			scaled = 1;
		}
	}

	NativeMinimap_Take(out, half, scaled);
	return NULL;
}

int NativeTrack_ReadMinimap(int index, struct NativeTrackMinimap *out, const char **why)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	const char *error;
	const u64 started = SDL_GetPerformanceCounter();

	memset(out, 0, sizeof(*out));
	*why = NULL;

	if ((entry == NULL) || !entry->ok)
	{
		*why = "the container is not usable";
		return 0;
	}

	error = NativeMinimap_ReadPath(entry->path, out);
	if (error != NULL)
	{
		*why = error;
		return 0;
	}

	out->microseconds = (u32)(((SDL_GetPerformanceCounter() - started) * 1000000u) / SDL_GetPerformanceFrequency());
	return 1;
}

// How much primitive memory the sky of this track needs.
//
// The primitive memory is allocated BEFORE the LEV is loaded - stage 5 versus
// stage 6. So at that point the game cannot read the need from its
// own level structure. The host side can: the container has long been
// in memory, and the fields are at fixed places.
//
// What one frame costs is computed: the four largest of the eight segments,
// because DrawSky_Full draws four per frame. A triangle is a POLY_G3, 28 bytes.
//
// 0 means "nothing extra" - no container, no sky, or one that fits into the
// budget of the host track.
internal u32 NativeTrack_SkyPrimBytesFor(const u8 *lev, size_t levSize)
{
	const size_t body = 4;
	u32 skyOffset;
	u32 counts[8];
	u32 sorted[8];
	u32 drawn;
	int i;
	int a;
	int b;

	if ((lev == NULL) || (levSize < (body + 0x200u)))
	{
		return 0;
	}

	// In u64: with a 32-bit size_t, body + skyOffset + 56 wrapped for an
	// offset near 4 GiB, and the reads below landed before the LEV.
	skyOffset = NativeTrack_LevField(lev, 0x04);
	if ((skyOffset == 0) || (((u64)body + skyOffset + 56u) > (u64)levSize))
	{
		return 0;
	}

	for (i = 0; i < 8; i++)
	{
		const u8 *at = &lev[body + skyOffset + 8u + (u32)i * 2u];

		counts[i] = (u32)at[0] | ((u32)at[1] << 8);
		sorted[i] = counts[i];
	}

	for (a = 0; a < 8; a++)
	{
		for (b = a + 1; b < 8; b++)
		{
			if (sorted[b] > sorted[a])
			{
				const u32 swap = sorted[a];

				sorted[a] = sorted[b];
				sorted[b] = swap;
			}
		}
	}

	drawn = sorted[0] + sorted[1] + sorted[2] + sorted[3];

	return drawn * 28u;
}

u32 NativeTrack_SkyPrimBytes(void)
{
	return NativeTrack_SkyPrimBytesFor(s_nativeTrackLev, (size_t)s_nativeTrackLevSize);
}

// THE CONTAINER SELF-TEST - see native_assets.h.
//
// The game's own order: the scan (NativeTrack_ReadForList, which also reads
// the META numbers the reserve before MEMPACK_Init is computed from), the track
// wheel (the map), the load (NativeTrack_ReadAll),
// and what reads the loaded LEV afterwards (missing tables, sky, draw and
// clip memory). Only the scan and the load can refuse - the map and the
// numbers after the load cannot in the game either; here they run so that
// whatever they would trip over trips here. Nothing lands in s_nativeTracks,
// nothing is logged: the caller prints the line.
int NativeTrack_SelfTestFile(const char *path, char *why, int whyBytes)
{
	struct NativeTrackEntry *entry;
	struct NativeTrackRead read;
	const char *error;
	const char *name;
	int accepted = 0;

	if ((why != NULL) && (whyBytes > 0))
	{
		why[0] = '\0';
	}

	entry = (struct NativeTrackEntry *)calloc(1, sizeof(*entry));
	if (entry == NULL)
	{
		error = "out of memory";
	}
	else if ((path == NULL) || (strlen(path) >= sizeof(entry->path)))
	{
		error = "the path is too long";
	}
	else
	{
		NativeTrack_CopyText(entry->path, sizeof(entry->path), path);

		name = path + strlen(path);
		while ((name > path) && (name[-1] != '/') && (name[-1] != '\\'))
		{
			name--;
		}
		NativeTrack_CopyText(entry->file, sizeof(entry->file), name);

		// The scan.
		NativeTrack_ReadForList(entry);
		error = entry->ok ? NULL : ((entry->problem != NULL) ? entry->problem : "rejected");
	}

	// The track wheel: the map. A map that cannot be read only leaves the
	// preview without it - no refusal, in the game neither.
	if (error == NULL)
	{
		struct NativeTrackMinimap map;

		memset(&map, 0, sizeof(map));
		if (NativeMinimap_ReadPath(entry->path, &map) == NULL)
		{
			NativeTrack_FreeMinimap(&map);
		}

	}

	// The load.
	if (error == NULL)
	{
		error = NativeTrack_ReadAll(entry, &read);

		if (error == NULL)
		{
			struct RldMemNeed need;

			(void)NativeTrack_MissingLevTables(entry, read.lev, read.levSize);
			(void)NativeTrack_SkyPrimBytesFor(read.lev, read.levSize);
			Rld_MemNeed(&need, read.lev, read.levSize);
			NativeTrack_FreeRead(&read);
			accepted = 1;
		}
	}

	if ((error != NULL) && (why != NULL) && (whyBytes > 0))
	{
		NativeTrack_CopyText(why, (size_t)whyBytes, error);
	}

	free(entry);
	return accepted;
}

int NativeTrack_ActiveLevdSha256(unsigned char out[32])
{
	if ((s_nativeTrackLoaded < 0) || (s_nativeTrackLev == NULL) || !NativeTrack_ActiveForLevel(s_nativeTrackLevelNow))
	{
		return 0;
	}

	memcpy(out, s_nativeTrackLevdSha, sizeof(s_nativeTrackLevdSha));
	return 1;
}

// --selftest-containers <dir> (main.c): every *.rldtrack of the folder through
// NativeTrack_SelfTestFile, sorted like the track list. good-* must be
// accepted, bad-* refused; one line per file. 0 only if every file is as
// expected and there is at least one of each kind.
#define NATIVE_SELFTEST_MAX_FILES 1024

internal int NativeTrack_SelfTestCompare(const void *a, const void *b)
{
	return NativeTrack_CompareNames(*(const char *const *)a, *(const char *const *)b);
}

int NativeTrack_SelfTestFolder(const char *dir)
{
	char **names = (char **)calloc(NATIVE_SELFTEST_MAX_FILES, sizeof(char *));
	int count = 0;
	int good = 0;
	int bad = 0;
	int unexpected = 0;
	int listed = 1;
	int i;

	if (names == NULL)
	{
		printf("[selftest] out of memory\n");
		return 1;
	}

#if defined(_WIN32)
	{
		char searchPath[NATIVE_ASSETS_PATH_MAX];
		WIN32_FIND_DATAA findData;
		HANDLE findHandle = INVALID_HANDLE_VALUE;

		memset(&findData, 0, sizeof(findData));
		if (NativePath_Join(searchPath, sizeof(searchPath), NativeStr8_FromCString(dir), NATIVE_STR8_LIT("*")))
		{
			findHandle = FindFirstFileA(searchPath, &findData);
		}

		if (findHandle == INVALID_HANDLE_VALUE)
		{
			listed = 0;
		}
		else
		{
			do
			{
				if (((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) && NativeTrack_HasExtension(findData.cFileName) &&
				    (count < NATIVE_SELFTEST_MAX_FILES))
				{
					names[count] = NativeAssets_CopyCString(findData.cFileName);
					count += (names[count] != NULL) ? 1 : 0;
				}
			} while (FindNextFileA(findHandle, &findData) != 0);

			FindClose(findHandle);
		}
	}
#else
	{
		struct dirent *dirEntry;
		DIR *folder = opendir(dir);

		if (folder == NULL)
		{
			listed = 0;
		}
		else
		{
			while ((dirEntry = readdir(folder)) != NULL)
			{
				if (NativeTrack_HasExtension(dirEntry->d_name) && (count < NATIVE_SELFTEST_MAX_FILES))
				{
					names[count] = NativeAssets_CopyCString(dirEntry->d_name);
					count += (names[count] != NULL) ? 1 : 0;
				}
			}

			closedir(folder);
		}
	}
#endif

	if (!listed)
	{
		printf("[selftest] %s: the folder cannot be read\n", dir);
		free(names);
		return 1;
	}

	if (count > 1)
	{
		qsort(names, (size_t)count, sizeof(names[0]), NativeTrack_SelfTestCompare);
	}

	for (i = 0; i < count; i++)
	{
		char path[NATIVE_ASSETS_PATH_MAX];
		char why[512];
		const int wantGood = (strncmp(names[i], "good-", 5) == 0);
		const int wantBad = (strncmp(names[i], "bad-", 4) == 0);
		int accepted = 0;
		int ok;

		why[0] = '\0';

		if (NativePath_Join(path, sizeof(path), NativeStr8_FromCString(dir), NativeStr8_FromCString(names[i])))
		{
			accepted = NativeTrack_SelfTestFile(path, why, (int)sizeof(why));
		}
		else
		{
			snprintf(why, sizeof(why), "the path is too long");
		}

		good += wantGood ? 1 : 0;
		bad += wantBad ? 1 : 0;
		ok = (wantGood && accepted) || (wantBad && !accepted);
		unexpected += ok ? 0 : 1;

		if (accepted)
		{
			printf("[selftest] %s: accepted - %s\n", names[i], ok ? "OK" : "UNEXPECTED");
		}
		else
		{
			printf("[selftest] %s: refused (%s) - %s\n", names[i], why, ok ? "OK" : "UNEXPECTED");
		}

		free(names[i]);
	}

	free(names);

	printf("[selftest] %d container(s): %d good-, %d bad-, %d unexpected%s\n", count, good, bad, unexpected,
	       ((good == 0) || (bad == 0)) ? " - needs at least one good- and one bad- file" : "");
	fflush(stdout);

	return ((unexpected == 0) && (good > 0) && (bad > 0)) ? 0 : 1;
}

// Whether this load comes from the container at all.
//
// The game side needs this to write its lines ONLY when
// a container is in play. A load path that has a say in every track
// would be a load path that has to be switched off again next time.
int NativeTrack_ActiveForLevel(int levelID)
{
	return (s_nativeTrackLoaded >= 0) && (levelID == s_nativeTrackDonorLevel);
}

// The name of the loaded track, for lines on the game side. Never NULL.
const char *NativeTrack_LoadedName(void)
{
	if (s_nativeTrackLoaded < 0)
	{
		return "(none)";
	}

	return s_nativeTracks[s_nativeTrackLoaded].name;
}

// Tells the read path which two bigfile numbers this load replaces.
//
// The load path computes the numbers itself, at the same place where it
// would otherwise put them into the queue - nothing is guessed here.
//
// And the slot is checked. Without this line every further track would get
// the container data as soon as a container has been loaded once - two tracks
// would then both load as the same "Level 4".
void NativeTrack_ArmSubfiles(int levelID, int levSubfile, int vramSubfile)
{
	if ((s_nativeTrackLoaded < 0) || (levelID != s_nativeTrackDonorLevel))
	{
		return;
	}

	s_nativeTrackLevSubfile = levSubfile;
	s_nativeTrackVrmSubfile = vramSubfile;

	Platform_Log("[CTR Tracks] level %d loads as '%s' - bigfile slots LEV %d and VRAM %d come out of the container\n", levelID,
	             s_nativeTracks[s_nativeTrackLoaded].name, levSubfile, vramSubfile);
}

const u8 *NativeTrack_SubfileData(int subfileIndex, u32 *sizeOut)
{
	if (s_nativeTrackLoaded < 0)
	{
		return NULL;
	}

	if ((subfileIndex == s_nativeTrackLevSubfile) && (s_nativeTrackLev != NULL))
	{
		// Once. A load fetches every file exactly once, and whatever is
		// read later under the same number belongs to another
		// level.
		s_nativeTrackLevSubfile = -1;
		*sizeOut = s_nativeTrackLevSize;
		Platform_Log("[CTR Tracks] LEVD handed over for subfile %d, %u bytes\n", subfileIndex, s_nativeTrackLevSize);
		return s_nativeTrackLev;
	}

	if ((subfileIndex == s_nativeTrackVrmSubfile) && (s_nativeTrackVrm != NULL))
	{
		s_nativeTrackVrmSubfile = -1;
		*sizeOut = s_nativeTrackVrmSize;
		Platform_Log("[CTR Tracks] VRMD handed over for subfile %d, %u bytes\n", subfileIndex, s_nativeTrackVrmSize);
		return s_nativeTrackVrm;
	}

	return NULL;
}
