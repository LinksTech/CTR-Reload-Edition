#include "platform/native_disc_image.h"

#include <platform/native_log.h>
#include <platform/native_path.h>

#include <limits.h>
#if defined(_WIN32)
#include <platform/native_win32.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_DISC_IMAGE_PATH_MAX          1024
#define NATIVE_DISC_IMAGE_BIN_PATH          "ctr-u.bin"
#define NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE   2352u
#define NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET 24u
#define NATIVE_DISC_IMAGE_FORM1_DATA_SIZE   2048u
#define NATIVE_DISC_IMAGE_MODE2_USER_OFFSET 16u
#define NATIVE_DISC_IMAGE_MODE2_USER_SIZE   2336u
#define NATIVE_DISC_IMAGE_PVD_LBA           16u
#define NATIVE_DISC_IMAGE_PVD_ROOT_RECORD   156u
#define NATIVE_DISC_IMAGE_DIRECTORY_FLAG    0x02u

// NOTE(aalhendi): This hardcodes only the common NTSC-U raw BIN layout:
// one MODE2/2352 data track at byte zero. The user still supplies all disc
// contents; native only uses this sector contract to read the image.

struct NativeDiscImageDirRecord
{
	u32 lba;
	u32 size;
	u8 flags;
	u8 nameLen;
	const u8 *name;
};

global_variable char s_nativeDiscImagePath[NATIVE_DISC_IMAGE_PATH_MAX];
global_variable FILE *s_nativeDiscImageFile;
global_variable struct NativeDiscImageFile s_nativeDiscImageRoot;
global_variable int s_nativeDiscImageAvailable;

internal int NativeDiscImage_FindHostImagePath(char *dst, size_t dstSize, NativeStr8 assetsDir)
{
#if defined(_WIN32)
	return NativePath_Join(dst, dstSize, assetsDir, NATIVE_STR8_LIT(NATIVE_DISC_IMAGE_BIN_PATH));
#else
	char dirPath[NATIVE_DISC_IMAGE_PATH_MAX];
	struct dirent *entry;
	int found = 0;

	if (!NativePath_NormalizeSlashes(dirPath, sizeof(dirPath), assetsDir))
	{
		return 0;
	}

	DIR *dir = opendir(dirPath);
	if (dir == NULL)
	{
		return 0;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		NativeStr8 entryName = NativeStr8_FromCString(entry->d_name);

		if (!NativeStr8_EqualsIgnoreCaseAscii(entryName, NATIVE_STR8_LIT(NATIVE_DISC_IMAGE_BIN_PATH)))
		{
			continue;
		}

		found = NativePath_Join(dst, dstSize, NativeStr8_FromCString(dirPath), entryName);
		break;
	}

	closedir(dir);
	return found;
#endif
}

internal void NativeDiscImage_NoteRead(const struct NativeDiscImageFile *file, u64 bytes);

internal u32 NativeDiscImage_ReadLE32(const u8 *buf)
{
	return ((u32)buf[0]) | ((u32)buf[1] << 8) | ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
}

internal int NativeDiscImage_CheckRawSectorHeader(const u8 *sector)
{
	if ((sector[0] != 0x00) || (sector[11] != 0x00) || (sector[15] != 0x02))
	{
		return 0;
	}

	for (u32 i = 1; i < 11; i++)
	{
		if (sector[i] != 0xff)
		{
			return 0;
		}
	}

	return 1;
}

internal int NativeDiscImage_ReadRawSector(u32 lba, u8 *sector)
{
	if (s_nativeDiscImageFile == NULL)
	{
		return 0;
	}

	u64 offset = (u64)lba * NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE;
	if ((offset > (u64)LONG_MAX) || (fseek(s_nativeDiscImageFile, (long)offset, SEEK_SET) != 0))
	{
		return 0;
	}

	if (fread(sector, 1, NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE, s_nativeDiscImageFile) != NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE)
	{
		return 0;
	}

	return NativeDiscImage_CheckRawSectorHeader(sector);
}

internal int NativeDiscImage_ReadDataSector(u32 lba, u8 *payload)
{
	u8 sector[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];

	if (!NativeDiscImage_ReadRawSector(lba, sector))
	{
		return 0;
	}

	memcpy(payload, &sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET], NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);
	return 1;
}

internal int NativeDiscImage_ParseDirRecord(const u8 *src, size_t available, struct NativeDiscImageDirRecord *record)
{
	if ((src == NULL) || (record == NULL) || (available < 34))
	{
		return 0;
	}

	u8 length = src[0];
	if ((length == 0) || (length > available) || (length < 34))
	{
		return 0;
	}

	record->lba = NativeDiscImage_ReadLE32(&src[2]);
	record->size = NativeDiscImage_ReadLE32(&src[10]);
	record->flags = src[25];
	record->nameLen = src[32];
	record->name = &src[33];

	if ((u32)record->nameLen + 33u > length)
	{
		return 0;
	}

	return 1;
}

internal int NativeDiscImage_NameEquals(const struct NativeDiscImageDirRecord *record, NativeStr8 component)
{
	if ((record == NULL) || (record->name == NULL) || (component.ptr == NULL))
	{
		return 0;
	}

	size_t recordLen = record->nameLen;
	size_t componentLen = component.len;

	while ((recordLen > 0) && (record->name[recordLen - 1u] == ' '))
	{
		recordLen--;
	}

	if ((recordLen > 2) && (record->name[recordLen - 2u] == ';') && (record->name[recordLen - 1u] == '1'))
	{
		recordLen -= 2;
	}

	if ((componentLen > 2) && (component.ptr[componentLen - 2u] == ';') && (component.ptr[componentLen - 1u] == '1'))
	{
		componentLen -= 2;
	}

	if (recordLen != componentLen)
	{
		return 0;
	}

	for (size_t i = 0; i < componentLen; i++)
	{
		if (NativeStr8_ToUpperAscii(record->name[i]) != NativeStr8_ToUpperAscii(component.ptr[i]))
		{
			return 0;
		}
	}

	return 1;
}

internal NativeStr8 NativeDiscImage_NextPathComponent(NativeStr8 *path)
{
	NativeStr8 result = *path;

	while ((result.len != 0) && NativePath_IsSeparator(result.ptr[0]))
	{
		result = NativeStr8_Skip(result, 1);
	}

	for (size_t i = 0; i < result.len; i++)
	{
		if (NativePath_IsSeparator(result.ptr[i]))
		{
			NativeStr8 component = {result.ptr, i};
			*path = NativeStr8_Skip(result, i + 1u);
			return component;
		}
	}

	*path = NativeStr8_Skip(result, result.len);
	return result;
}

internal u32 NativeDiscImage_DataSectorCount(u32 size)
{
	return (size + (NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1u)) / NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
}

internal u32 NativeDiscImage_RawSectorCount(u32 size)
{
	if ((size != 0) && ((size % NATIVE_DISC_IMAGE_MODE2_USER_SIZE) == 0))
	{
		return size / NATIVE_DISC_IMAGE_MODE2_USER_SIZE;
	}

	return NativeDiscImage_DataSectorCount(size);
}

internal int NativeDiscImage_ReadDirectoryBytes(const struct NativeDiscImageFile *dir, u8 **dataOut, int *sizeOut)
{
	u8 *buf;

	*dataOut = NULL;
	*sizeOut = 0;

	if ((dir == NULL) || (dir->size == 0) || (dir->size > 0x7fffffff))
	{
		return 0;
	}

	u32 sectorCount = NativeDiscImage_DataSectorCount(dir->size);
	if (sectorCount == 0)
	{
		return 0;
	}

	buf = (u8 *)malloc((size_t)sectorCount * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);
	if (buf == NULL)
	{
		return 0;
	}

	for (u32 sector = 0; sector < sectorCount; sector++)
	{
		if (!NativeDiscImage_ReadDataSector(dir->lba + sector, &buf[sector * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE]))
		{
			free(buf);
			return 0;
		}
	}

	*dataOut = buf;
	*sizeOut = (int)dir->size;
	return 1;
}

internal int NativeDiscImage_FindInDirectory(const struct NativeDiscImageFile *dir, NativeStr8 component, struct NativeDiscImageFile *fileOut, u8 *flagsOut)
{
	u8 *buf;
	int size;
	int found = 0;

	if (!NativeDiscImage_ReadDirectoryBytes(dir, &buf, &size))
	{
		return 0;
	}

	int offset = 0;
	while (offset < size)
	{
		struct NativeDiscImageDirRecord record;
		u8 length = buf[offset];

		if (length == 0)
		{
			offset = (offset + (int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE) & ~((int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1);
			continue;
		}

		if ((length < 34) || (offset + length > size))
		{
			break;
		}

		if (NativeDiscImage_ParseDirRecord(&buf[offset], (size_t)(size - offset), &record) && NativeDiscImage_NameEquals(&record, component))
		{
			fileOut->lba = record.lba;
			fileOut->size = record.size;
			*flagsOut = record.flags;
			found = 1;
			break;
		}

		offset += length;
	}

	free(buf);
	return found;
}

internal int NativeDiscImage_LoadRoot(void)
{
	u8 sector[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];
	struct NativeDiscImageDirRecord root;

	if (!NativeDiscImage_ReadRawSector(NATIVE_DISC_IMAGE_PVD_LBA, sector))
	{
		return 0;
	}

	if ((memcmp(&sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + 1], "CD001", 5) != 0) || (sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET] != 1) ||
	    (sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + 6] != 1))
	{
		return 0;
	}

	if (!NativeDiscImage_ParseDirRecord(&sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + NATIVE_DISC_IMAGE_PVD_ROOT_RECORD],
	                                    NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - NATIVE_DISC_IMAGE_PVD_ROOT_RECORD, &root))
	{
		return 0;
	}

	s_nativeDiscImageRoot.lba = root.lba;
	s_nativeDiscImageRoot.size = root.size;
	return 1;
}

// Open, read the root, remember the path. Split out of the boot path below
// because the first start opens an image the user just pointed at, and the two
// have to fail in exactly the same ways - an image accepted by one and refused
// by the other is a bug report nobody can reproduce.
internal int NativeDiscImage_OpenImageFile(const char *path)
{
	s_nativeDiscImageFile = fopen(path, "rb");
	if (s_nativeDiscImageFile == NULL)
	{
		return 0;
	}

	if (!NativeDiscImage_LoadRoot())
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
		return 0;
	}

	if (!NativePath_NormalizeSlashes(s_nativeDiscImagePath, sizeof(s_nativeDiscImagePath), NativeStr8_FromCString(path)))
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
		return 0;
	}

	s_nativeDiscImageAvailable = 1;
	return 1;
}

int NativeDiscImage_Init(const char *assetsDir)
{
	char path[NATIVE_DISC_IMAGE_PATH_MAX];

	s_nativeDiscImageAvailable = 0;
	s_nativeDiscImagePath[0] = '\0';

	if (s_nativeDiscImageFile != NULL)
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
	}

	if ((assetsDir == NULL) || !NativeDiscImage_FindHostImagePath(path, sizeof(path), NativeStr8_FromCString(assetsDir)))
	{
		return 0;
	}

	return NativeDiscImage_OpenImageFile(path);
}

int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut)
{
	NativeStr8 remaining = NativePath_SkipLeadingSeparators(NativeStr8_FromCString(path));
	struct NativeDiscImageFile current = s_nativeDiscImageRoot;
	u8 flags = NATIVE_DISC_IMAGE_DIRECTORY_FLAG;

	if (!s_nativeDiscImageAvailable || (path == NULL) || (fileOut == NULL))
	{
		return 0;
	}

	while (remaining.len != 0)
	{
		NativeStr8 component = NativeDiscImage_NextPathComponent(&remaining);

		if (component.len == 0)
		{
			continue;
		}

		if ((flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) == 0)
		{
			return 0;
		}

		if (!NativeDiscImage_FindInDirectory(&current, component, &current, &flags))
		{
			return 0;
		}
	}

	if ((flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) != 0)
	{
		return 0;
	}

	*fileOut = current;
	return 1;
}

internal int NativeDiscImage_ReadDataBytes(const struct NativeDiscImageFile *file, u32 offset, void *dst, size_t size)
{
	u8 sector[NATIVE_DISC_IMAGE_FORM1_DATA_SIZE];
	u8 *out = (u8 *)dst;

	if ((file == NULL) || (dst == NULL) || ((u64)offset + size > file->size))
	{
		return 0;
	}

	while (size != 0)
	{
		u32 sectorIndex = offset / NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
		u32 sectorOffset = offset % NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
		size_t copySize = NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - sectorOffset;

		if (copySize > size)
		{
			copySize = size;
		}

		if (!NativeDiscImage_ReadDataSector(file->lba + sectorIndex, sector))
		{
			return 0;
		}

		memcpy(out, &sector[sectorOffset], copySize);
		NativeDiscImage_NoteRead(file, copySize);
		out += copySize;
		offset += (u32)copySize;
		size -= copySize;
	}

	return 1;
}

int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	u8 *out = (u8 *)dst;

	if ((file == NULL) || (dst == NULL))
	{
		return 0;
	}

	u32 fileSectorCount = NativeDiscImage_DataSectorCount(file->size);
	if ((sector > fileSectorCount) || (sectorCount > fileSectorCount - sector))
	{
		return 0;
	}

	for (u32 i = 0; i < sectorCount; i++)
	{
		if (!NativeDiscImage_ReadDataSector(file->lba + sector + i, &out[i * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE]))
		{
			return 0;
		}
	}

	NativeDiscImage_NoteRead(file, (u64)sectorCount * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);
	return 1;
}

int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	u8 raw[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];
	u8 *out = (u8 *)dst;

	if ((file == NULL) || (dst == NULL))
	{
		return 0;
	}

	u32 fileSectorCount = NativeDiscImage_RawSectorCount(file->size);
	if ((sector > fileSectorCount) || (sectorCount > fileSectorCount - sector))
	{
		return 0;
	}

	for (u32 i = 0; i < sectorCount; i++)
	{
		if (!NativeDiscImage_ReadRawSector(file->lba + sector + i, raw))
		{
			return 0;
		}

		memcpy(&out[i * NATIVE_DISC_IMAGE_MODE2_USER_SIZE], &raw[NATIVE_DISC_IMAGE_MODE2_USER_OFFSET], NATIVE_DISC_IMAGE_MODE2_USER_SIZE);
	}

	NativeDiscImage_NoteRead(file, (u64)sectorCount * NATIVE_DISC_IMAGE_MODE2_USER_SIZE);
	return 1;
}

int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut)
{
	struct NativeDiscImageFile file;
	u32 sectorCount;
	u32 size;

	*dataOut = NULL;
	*sizeOut = 0;

	if (!NativeDiscImage_FindFile(path, &file))
	{
		return 0;
	}

	if (rawSectors)
	{
		sectorCount = NativeDiscImage_RawSectorCount(file.size);
		size = sectorCount * NATIVE_DISC_IMAGE_MODE2_USER_SIZE;
	}
	else
	{
		sectorCount = NativeDiscImage_DataSectorCount(file.size);
		size = file.size;
	}

	if ((sectorCount == 0) || (size > 0x7fffffffu))
	{
		return 0;
	}

	u8 *buf = (u8 *)malloc((size_t)size);
	if (buf == NULL)
	{
		return 0;
	}

	if (rawSectors)
	{
		if (!NativeDiscImage_ReadRawSectors(&file, 0, sectorCount, buf))
		{
			free(buf);
			return 0;
		}
	}
	else if (!NativeDiscImage_ReadDataBytes(&file, 0, buf, size))
	{
		free(buf);
		return 0;
	}

	*dataOut = buf;
	*sizeOut = (int)size;
	return 1;
}

//----------------------------------------------------------------------------------------
// THE FIRST START - CHECK AND EXTRACT THE IMAGE
//
// The reader above answers "give me this file". Everything below answers the two
// questions the first start asks instead: IS this the right disc, and WRITE all
// of it out.
//
// It sits in this file rather than beside the window that shows it, because it
// is disc knowledge - sector forms, boot records, ISO directory shapes - and
// none of that belongs next to SDL calls.

#define NATIVE_DISC_IMAGE_SYSTEM_CNF     "SYSTEM.CNF"
#define NATIVE_DISC_IMAGE_SUBMODE_OFFSET 18u
#define NATIVE_DISC_IMAGE_SUBMODE_FORM2  0x20u
#define NATIVE_DISC_IMAGE_DIR_DEPTH_MAX  8
#define NATIVE_DISC_IMAGE_SELF_RECORD    0x00u
#define NATIVE_DISC_IMAGE_PARENT_RECORD  0x01u

// THE IMAGE IS FOREIGN INPUT, AND THE WALK BELOW WRITES WHERE IT SAYS.
//
// A directory record is a name, a start sector and a size, all of them the
// image's word. Nothing stops an image from naming an entry "..", "C:x" or
// "..\..\x", or from pointing a directory record back at the root or at one of
// its own parents. The limits below turn each of those into a refusal with the
// reason in the log - never a write outside the assets folder, never a walk
// that does not end.
//
// Every limit is far above what a real disc holds: the retail disc unpacks 36
// files from a shallow tree whose directories fit in a few sectors each, and
// it passes the walk exactly as before (same 36 files, same byte count). A CD
// cannot hold more than about 360 000 sectors, so no real directory tree comes
// near 1024 directories or 65 536 entries.
#define NATIVE_DISC_IMAGE_DIRS_MAX      1024
#define NATIVE_DISC_IMAGE_ENTRIES_MAX   65536u
#define NATIVE_DISC_IMAGE_DIR_BYTES_MAX (512u * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE)
#define NATIVE_DISC_IMAGE_WHY_MAX       320

// The reason of the last refusal of the walk, for the self-test. The same text
// goes to the log.
global_variable char s_nativeDiscImageWhy[NATIVE_DISC_IMAGE_WHY_MAX];

// WHAT STAYS ON THE DISC WHEN EXTRACTING
//
// Exactly one item, and it is measured, not estimated. The read counter
// further down found 33 files as "not read in this run", 211.1 MB -
// but "not read" is no reason to leave something out. TEST.STR stood at the
// very top of the same list with 86.4 MB and is REQUIRED: native_assets.c checks
// it at startup. Decided by size, it would have been the first to go.
//
// So only what has proof in the code is dropped here. The
// Spyro 2 demo hangs solely on MainKillGame_LaunchSpyro2, and there the
// tree says: "Native cannot chain-load the Spyro executable." The path there is
// dead by design on this port, so the demo is unreachable.
//
// LAST.BIN, 9.8 MB, is NOT included, even though it is named nowhere in the code.
// "Named nowhere" does not mean "never read" - a name can
// be assembled - and ten megabytes are no argument against a
// broken installation. BIGFILE.BIG is not touched either: it is an
// archive, and the counter works at file level. We know nothing about its
// insides, so we take nothing out.
//
// Whoever wants to add something here needs the same proof: a place in the
// code that shows it is unreachable. Not a line in the
// read report.
static const char *const s_nativeDiscImageSkip[] = {
    "SPYRO2",     // directory: DEMO2.STR, SOURCE.TRD, WAD.WAD
    "SPYRO2.EXE", // the demo EXE itself, in the root directory
};

#define NATIVE_DISC_IMAGE_SKIP_COUNT ((int)(sizeof(s_nativeDiscImageSkip) / sizeof(s_nativeDiscImageSkip[0])))

// Applies to the entry itself and, if it is a directory, to everything
// below it - the walk then does not descend into it at all.
internal int NativeDiscImage_SkipOnExtract(const char *path)
{
	int i;

	for (i = 0; i < NATIVE_DISC_IMAGE_SKIP_COUNT; i++)
	{
		const char *skip = s_nativeDiscImageSkip[i];
		size_t length = strlen(skip);

		if ((strncmp(path, skip, length) == 0) && ((path[length] == '\0') || (path[length] == '/')))
		{
			return 1;
		}
	}

	return 0;
}

struct NativeDiscImageWalk
{
	NativeDiscImageProgressFn progress;
	void *user;

	const char *destDir;

	u32 fileCount;
	u64 byteCount;

	u32 filesDone;
	u64 bytesDone;

	// Counting pass or writing pass. One walk, two uses: the count has to be
	// exact before the first byte is written, or the progress line lies.
	int writing;

	int result;
	char failedPath[NATIVE_DISC_IMAGE_PATH_MAX];

	// The loop guard: the start sector of every directory entered in this
	// pass. A second entry into the same sector is a record pointing back at
	// the root or at a parent, and refused - a depth limit alone would still
	// let a directory with many such records fan out into billions of paths.
	u32 visited[NATIVE_DISC_IMAGE_DIRS_MAX];
	int visitedCount;
	u32 entryCount;

	char why[NATIVE_DISC_IMAGE_WHY_MAX];
};

// Says why, once, in the log's refusal form, and keeps the text for the
// self-test. Only the first reason of a walk counts; whatever breaks after it
// is a consequence.
internal void NativeDiscImage_WalkRefuse(struct NativeDiscImageWalk *walk, const char *reason)
{
	if (walk->why[0] != '\0')
	{
		return;
	}

	NativeStr8_CopyToCString(walk->why, sizeof(walk->why), NativeStr8_FromCString(reason));
	NativeStr8_CopyToCString(s_nativeDiscImageWhy, sizeof(s_nativeDiscImageWhy), NativeStr8_FromCString(reason));
	Platform_Log("[CTR Disc] image REJECTED - %s\n", reason);
}

// A name as it can be printed: the bytes that make a name dangerous are exactly
// the ones that make it unreadable in a log line.
internal void NativeDiscImage_PrintableName(char *dst, size_t dstSize, const char *parentPath, const u8 *name, size_t nameLength)
{
	size_t at = 0;
	size_t i;

	if (dstSize == 0)
	{
		return;
	}

	for (i = 0; (parentPath[i] != '\0') && ((at + 1u) < dstSize); i++)
	{
		dst[at++] = parentPath[i];
	}

	if ((at != 0) && ((at + 1u) < dstSize))
	{
		dst[at++] = '/';
	}

	for (i = 0; (i < nameLength) && ((at + 5u) < dstSize); i++)
	{
		u8 byte = name[i];

		if ((byte < 0x20u) || (byte >= 0x7fu))
		{
			static const char hex[] = "0123456789abcdef";

			dst[at++] = '\\';
			dst[at++] = 'x';
			dst[at++] = hex[byte >> 4];
			dst[at++] = hex[byte & 0x0fu];
		}
		else
		{
			dst[at++] = (char)byte;
		}
	}

	dst[at] = '\0';
}

// WHAT A NAME ON THE DISC MAY BE: ONE PLAIN PATH COMPONENT.
//
// Checked after the version suffix and the padding are gone, i.e. on exactly
// the bytes that end up in the host path. Every character that could turn one
// component into several, into a parent, into a drive, a stream or a device is
// refused - the retail disc uses capital letters, digits, '_' and '.', nothing
// else. NULL means the name is fine.
internal const char *NativeDiscImage_EntryNameProblem(const u8 *name, size_t nameLength)
{
	static const char *const devices[] = {"CON", "PRN", "AUX", "NUL"};
	NativeStr8 firstThree;
	size_t stem;
	size_t i;
	int onlyDots = 1;

	if (nameLength == 0)
	{
		return "empty name";
	}

	for (i = 0; i < nameLength; i++)
	{
		u8 byte = name[i];

		if (byte == 0x00u)
		{
			return "NUL byte in the name";
		}
		if ((byte == '/') || (byte == '\\'))
		{
			return "path separator in the name";
		}
		if (byte == ':')
		{
			return "':' in the name (drive letter or stream)";
		}
		if ((byte < 0x20u) || (byte == 0x7fu))
		{
			return "control character in the name";
		}
		if ((byte == '<') || (byte == '>') || (byte == '"') || (byte == '|') || (byte == '?') || (byte == '*'))
		{
			return "character Windows does not allow in a name";
		}
		if ((byte != '.') && (byte != ' '))
		{
			onlyDots = 0;
		}
	}

	// ".", "..", and everything Windows folds into them ("...", ". .").
	if (onlyDots)
	{
		return "name is only dots (\".\" or \"..\")";
	}

	// CON, NUL, COM1 ... are devices in every folder, with any extension.
	for (stem = 0; (stem < nameLength) && (name[stem] != '.'); stem++)
	{
	}
	while ((stem > 0) && (name[stem - 1u] == ' '))
	{
		stem--;
	}

	firstThree.ptr = name;
	firstThree.len = 3;

	for (i = 0; i < (sizeof(devices) / sizeof(devices[0])); i++)
	{
		if ((stem == 3) && NativeStr8_EqualsIgnoreCaseAscii(firstThree, NativeStr8_FromCString(devices[i])))
		{
			return "Windows device name";
		}
	}

	if ((stem == 4) && (name[3] >= '0') && (name[3] <= '9') &&
	    (NativeStr8_EqualsIgnoreCaseAscii(firstThree, NATIVE_STR8_LIT("COM")) || NativeStr8_EqualsIgnoreCaseAscii(firstThree, NATIVE_STR8_LIT("LPT"))))
	{
		return "Windows device name";
	}

	return NULL;
}

#if !defined(_WIN32)
// "." and ".." resolved by text, for the prefix check below. The path does not
// exist yet, so realpath() cannot be asked.
internal int NativeDiscImage_LexicalPath(char *dst, size_t dstSize, const char *src)
{
	char work[NATIVE_DISC_IMAGE_PATH_MAX];
	size_t out = 1;
	const char *at;

	if (src[0] == '/')
	{
		NativeStr8_CopyToCString(work, sizeof(work), NativeStr8_FromCString(src));
	}
	else
	{
		char cwd[NATIVE_DISC_IMAGE_PATH_MAX];

		if ((getcwd(cwd, sizeof(cwd)) == NULL) || (snprintf(work, sizeof(work), "%s/%s", cwd, src) >= (int)sizeof(work)))
		{
			return 0;
		}
	}

	if (dstSize < 2)
	{
		return 0;
	}

	dst[0] = '/';
	at = work;

	while (*at != '\0')
	{
		const char *start;
		size_t length;

		while (*at == '/')
		{
			at++;
		}

		start = at;
		while ((*at != '\0') && (*at != '/'))
		{
			at++;
		}

		length = (size_t)(at - start);

		if ((length == 0) || ((length == 1) && (start[0] == '.')))
		{
			continue;
		}

		if ((length == 2) && (start[0] == '.') && (start[1] == '.'))
		{
			while ((out > 1) && (dst[out - 1u] != '/'))
			{
				out--;
			}
			if (out > 1)
			{
				out--;
			}
			continue;
		}

		if ((out + length + 2u) > dstSize)
		{
			return 0;
		}

		if (out > 1)
		{
			dst[out++] = '/';
		}

		memcpy(&dst[out], start, length);
		out += length;
	}

	dst[out] = '\0';
	return 1;
}
#endif

// THE LAST WORD BEFORE ANYTHING IS CREATED: IS THIS PATH INSIDE THE FOLDER?
//
// The name check above already refuses every component that could leave the
// folder. This compares the finished host path against the folder itself, both
// normalized by the same rules the file system applies, so a way out that the
// name check did not think of still ends here. 1 = strictly below root.
internal int NativeDiscImage_PathInside(const char *root, const char *path)
{
	char rootFull[NATIVE_DISC_IMAGE_PATH_MAX];
	char pathFull[NATIVE_DISC_IMAGE_PATH_MAX];
	size_t rootLength;
	size_t i;

	if ((root == NULL) || (path == NULL) || (root[0] == '\0') || (path[0] == '\0'))
	{
		return 0;
	}

#if defined(_WIN32)
	{
		DWORD length = GetFullPathNameA(root, (DWORD)sizeof(rootFull), rootFull, NULL);

		if ((length == 0) || (length >= sizeof(rootFull)))
		{
			return 0;
		}

		length = GetFullPathNameA(path, (DWORD)sizeof(pathFull), pathFull, NULL);

		if ((length == 0) || (length >= sizeof(pathFull)))
		{
			return 0;
		}
	}
#else
	if (!NativeDiscImage_LexicalPath(rootFull, sizeof(rootFull), root) || !NativeDiscImage_LexicalPath(pathFull, sizeof(pathFull), path))
	{
		return 0;
	}
#endif

	rootLength = strlen(rootFull);
	while ((rootLength > 0) && NativePath_IsSeparator((u8)rootFull[rootLength - 1u]))
	{
		rootLength--;
	}

	if ((rootLength == 0) || (strlen(pathFull) <= rootLength))
	{
		return 0;
	}

	for (i = 0; i < rootLength; i++)
	{
#if defined(_WIN32)
		u8 left = NativeStr8_ToUpperAscii((u8)rootFull[i]);
		u8 right = NativeStr8_ToUpperAscii((u8)pathFull[i]);
#else
		u8 left = (u8)rootFull[i];
		u8 right = (u8)pathFull[i];
#endif

		if (NativePath_IsSeparator(left) && NativePath_IsSeparator(right))
		{
			continue;
		}

		if (left != right)
		{
			return 0;
		}
	}

	if (!NativePath_IsSeparator((u8)pathFull[rootLength]))
	{
		return 0;
	}

	for (i = rootLength; pathFull[i] != '\0'; i++)
	{
		if (!NativePath_IsSeparator((u8)pathFull[i]))
		{
			return 1;
		}
	}

	return 0;
}

internal int NativeDiscImage_MakeDirectory(const char *path)
{
#if defined(_WIN32)
	if (CreateDirectoryA(path, NULL) != 0)
	{
		return 1;
	}

	return (GetLastError() == ERROR_ALREADY_EXISTS);
#else
	if (mkdir(path, 0777) == 0)
	{
		return 1;
	}

	return (errno == EEXIST);
#endif
}

// Every directory on the way, not just the last one. The ISO hands out paths
// like XA/ENG/GAME and the host has none of them yet.
int NativeDiscImage_EnsureDirectory(const char *path)
{
	char work[NATIVE_DISC_IMAGE_PATH_MAX];
	size_t length;
	size_t i;

	if (!NativePath_NormalizeSlashes(work, sizeof(work), NativeStr8_FromCString(path)))
	{
		return 0;
	}

	length = strlen(work);

	for (i = 1; i < length; i++)
	{
		if (work[i] != '/')
		{
			continue;
		}

		work[i] = '\0';

		// A drive letter's root is not a directory anybody creates.
		if (!((i == 2) && (work[1] == ':')))
		{
			if (!NativeDiscImage_MakeDirectory(work))
			{
				return 0;
			}
		}

		work[i] = '/';
	}

	return NativeDiscImage_MakeDirectory(work);
}

// FORM1 OR FORM2, ASKED OF THE DISC RATHER THAN OF THE FILE NAME.
//
// The XA tracks and the FMV are Mode 2 Form 2: 2336 bytes of user area per
// sector, subheader and EDC included, because that is what the game's raw reader
// hands to the decoders. Everything else is Form 1: 2048 bytes of payload, and
// the size the directory record states.
//
// Which one a file is stands in the sector's own subheader, bit 5 of the submode
// byte. Reading it there rather than matching on ".XA" means a disc that puts
// something unexpected in Form 2 still comes out byte-identical to what the
// reader above would have produced from the image.
internal int NativeDiscImage_FileIsForm2(const struct NativeDiscImageFile *file, int *isForm2)
{
	u8 sector[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];

	*isForm2 = 0;

	if ((file == NULL) || (file->size == 0))
	{
		return 1;
	}

	if (!NativeDiscImage_ReadRawSector(file->lba, sector))
	{
		return 0;
	}

	*isForm2 = ((sector[NATIVE_DISC_IMAGE_SUBMODE_OFFSET] & NATIVE_DISC_IMAGE_SUBMODE_FORM2) != 0);
	return 1;
}

internal int NativeDiscImage_WriteFile(struct NativeDiscImageWalk *walk, const struct NativeDiscImageFile *file, const char *relativePath)
{
	char hostPath[NATIVE_DISC_IMAGE_PATH_MAX];
	char parentPath[NATIVE_DISC_IMAGE_PATH_MAX];
	u8 buffer[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];
	FILE *out;
	u32 sectorIndex;
	u32 sectorCount;
	int isForm2 = 0;

	if (!NativePath_Join(hostPath, sizeof(hostPath), NativeStr8_FromCString(walk->destDir), NativeStr8_FromCString(relativePath)))
	{
		walk->result = NATIVE_DISC_IMAGE_ERR_CREATE;
		return 0;
	}

	// Before the first directory or file is created. The walk refused every
	// dangerous name already; this is the check that does not depend on having
	// thought of every one.
	if (!NativeDiscImage_PathInside(walk->destDir, hostPath))
	{
		char reason[NATIVE_DISC_IMAGE_WHY_MAX];

		snprintf(reason, sizeof(reason), "entry \"%s\" would be written outside the assets folder", relativePath);
		NativeDiscImage_WalkRefuse(walk, reason);
		walk->result = NATIVE_DISC_IMAGE_ERR_READ;
		return 0;
	}

	if (!NativeDiscImage_FileIsForm2(file, &isForm2))
	{
		walk->result = NATIVE_DISC_IMAGE_ERR_READ;
		return 0;
	}

	if (NativePath_Parent(parentPath, sizeof(parentPath), NativeStr8_FromCString(hostPath)))
	{
		if (!NativeDiscImage_EnsureDirectory(parentPath))
		{
			walk->result = NATIVE_DISC_IMAGE_ERR_CREATE;
			return 0;
		}
	}

	out = fopen(hostPath, "wb");
	if (out == NULL)
	{
		walk->result = NATIVE_DISC_IMAGE_ERR_CREATE;
		return 0;
	}

	sectorCount = isForm2 ? NativeDiscImage_RawSectorCount(file->size) : NativeDiscImage_DataSectorCount(file->size);

	for (sectorIndex = 0; sectorIndex < sectorCount; sectorIndex++)
	{
		size_t chunk;

		if (isForm2)
		{
			if (!NativeDiscImage_ReadRawSectors(file, sectorIndex, 1, buffer))
			{
				fclose(out);
				remove(hostPath);
				walk->result = NATIVE_DISC_IMAGE_ERR_READ;
				return 0;
			}

			chunk = NATIVE_DISC_IMAGE_MODE2_USER_SIZE;
		}
		else
		{
			u32 remaining = file->size - (sectorIndex * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);

			if (!NativeDiscImage_ReadDataSector(file->lba + sectorIndex, buffer))
			{
				fclose(out);
				remove(hostPath);
				walk->result = NATIVE_DISC_IMAGE_ERR_READ;
				return 0;
			}

			// The last sector of a Form 1 file is padded on the disc; the
			// directory record says how much of it is the file.
			chunk = (remaining < NATIVE_DISC_IMAGE_FORM1_DATA_SIZE) ? remaining : NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
		}

		if (fwrite(buffer, 1, chunk, out) != chunk)
		{
			fclose(out);

			// Nothing half-written stays behind. A file that exists but is short
			// is worse than one that is missing: the validator would find it and
			// the game would fail somewhere else entirely.
			remove(hostPath);
			walk->result = NATIVE_DISC_IMAGE_ERR_WRITE;
			return 0;
		}

		walk->bytesDone += chunk;
	}

	if (fclose(out) != 0)
	{
		remove(hostPath);
		walk->result = NATIVE_DISC_IMAGE_ERR_WRITE;
		return 0;
	}

	return 1;
}

internal int NativeDiscImage_WalkDirectory(struct NativeDiscImageWalk *walk, const struct NativeDiscImageFile *dir, char *pathBuffer, size_t pathLength,
                                           size_t pathCapacity, int depth)
{
	u8 *buf;
	int size;
	int offset = 0;
	int ok = 1;
	int i;
	char reason[NATIVE_DISC_IMAGE_WHY_MAX];

	// This directory's own path for the reasons below; pathBuffer holds it up
	// to pathLength for the whole call.
	const char *dirName = (pathBuffer[0] != '\0') ? pathBuffer : "(root)";

	// Refused rather than cut off. Cutting off silently was the old loop
	// guard, and it still let a directory full of records pointing at the root
	// fan out into more paths than the walk could ever finish.
	if (depth > NATIVE_DISC_IMAGE_DIR_DEPTH_MAX)
	{
		snprintf(reason, sizeof(reason), "directory \"%s\" is nested deeper than %d levels", dirName, NATIVE_DISC_IMAGE_DIR_DEPTH_MAX);
		NativeDiscImage_WalkRefuse(walk, reason);
		walk->result = NATIVE_DISC_IMAGE_ERR_READ;
		return 0;
	}

	for (i = 0; i < walk->visitedCount; i++)
	{
		if (walk->visited[i] == dir->lba)
		{
			snprintf(reason, sizeof(reason), "directory \"%s\" points back at sector %u, a directory already walked (a loop to the root or a parent)",
			         dirName, (unsigned int)dir->lba);
			NativeDiscImage_WalkRefuse(walk, reason);
			walk->result = NATIVE_DISC_IMAGE_ERR_READ;
			return 0;
		}
	}

	if (walk->visitedCount >= NATIVE_DISC_IMAGE_DIRS_MAX)
	{
		snprintf(reason, sizeof(reason), "more than %d directories", NATIVE_DISC_IMAGE_DIRS_MAX);
		NativeDiscImage_WalkRefuse(walk, reason);
		walk->result = NATIVE_DISC_IMAGE_ERR_READ;
		return 0;
	}

	walk->visited[walk->visitedCount++] = dir->lba;

	if (dir->size > NATIVE_DISC_IMAGE_DIR_BYTES_MAX)
	{
		snprintf(reason, sizeof(reason), "directory \"%s\" claims %u bytes (at most %u)", dirName, (unsigned int)dir->size,
		         (unsigned int)NATIVE_DISC_IMAGE_DIR_BYTES_MAX);
		NativeDiscImage_WalkRefuse(walk, reason);
		walk->result = NATIVE_DISC_IMAGE_ERR_READ;
		return 0;
	}

	if (!NativeDiscImage_ReadDirectoryBytes(dir, &buf, &size))
	{
		snprintf(reason, sizeof(reason), "directory \"%s\" at sector %u cannot be read", dirName, (unsigned int)dir->lba);
		NativeDiscImage_WalkRefuse(walk, reason);
		walk->result = NATIVE_DISC_IMAGE_ERR_READ;
		return 0;
	}

	while ((offset < size) && ok)
	{
		struct NativeDiscImageDirRecord record;
		struct NativeDiscImageFile entry;
		const char *problem;
		size_t nameLength;
		size_t writeAt;
		size_t n;
		u8 length = buf[offset];

		if (length == 0)
		{
			// Records never straddle a sector; the rest of this one is padding.
			offset = (offset + (int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE) & ~((int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1);
			continue;
		}

		if (!NativeDiscImage_ParseDirRecord(&buf[offset], (size_t)(size - offset), &record))
		{
			snprintf(reason, sizeof(reason), "directory \"%s\" has a broken record at byte %d", dirName, offset);
			NativeDiscImage_WalkRefuse(walk, reason);
			walk->result = NATIVE_DISC_IMAGE_ERR_READ;
			ok = 0;
			break;
		}

		if (++walk->entryCount > NATIVE_DISC_IMAGE_ENTRIES_MAX)
		{
			snprintf(reason, sizeof(reason), "more than %u directory entries", (unsigned int)NATIVE_DISC_IMAGE_ENTRIES_MAX);
			NativeDiscImage_WalkRefuse(walk, reason);
			walk->result = NATIVE_DISC_IMAGE_ERR_READ;
			ok = 0;
			break;
		}

		offset += length;

		// "." and ".." are one byte, 0x00 and 0x01. Following either is how a
		// directory walk becomes an endless one.
		if ((record.nameLen == 1) && ((record.name[0] == NATIVE_DISC_IMAGE_SELF_RECORD) || (record.name[0] == NATIVE_DISC_IMAGE_PARENT_RECORD)))
		{
			continue;
		}

		nameLength = record.nameLen;
		while ((nameLength > 0) && (record.name[nameLength - 1u] == ' '))
		{
			nameLength--;
		}
		if ((nameLength > 2) && (record.name[nameLength - 2u] == ';') && (record.name[nameLength - 1u] == '1'))
		{
			nameLength -= 2;
		}

		// In both passes, so the counting pass - the one the first-start
		// screen runs before it removes anything - already refuses the image.
		problem = NativeDiscImage_EntryNameProblem(record.name, nameLength);
		if (problem != NULL)
		{
			char printable[NATIVE_DISC_IMAGE_PATH_MAX];

			NativeDiscImage_PrintableName(printable, sizeof(printable), pathBuffer, record.name, record.nameLen);
			snprintf(reason, sizeof(reason), "entry \"%s\": %s", printable, problem);
			NativeDiscImage_WalkRefuse(walk, reason);
			walk->result = NATIVE_DISC_IMAGE_ERR_READ;
			ok = 0;
			break;
		}

		if ((pathLength + nameLength + 2u) >= pathCapacity)
		{
			snprintf(reason, sizeof(reason), "a path below \"%s\" is longer than %u bytes", dirName, (unsigned int)pathCapacity);
			NativeDiscImage_WalkRefuse(walk, reason);
			walk->result = NATIVE_DISC_IMAGE_ERR_READ;
			ok = 0;
			break;
		}

		if (pathLength != 0)
		{
			pathBuffer[pathLength] = '/';
		}

		writeAt = pathLength + ((pathLength != 0) ? 1u : 0u);

		for (n = 0; n < nameLength; n++)
		{
			pathBuffer[writeAt + n] = (char)record.name[n];
		}

		pathBuffer[writeAt + nameLength] = '\0';

		entry.lba = record.lba;
		entry.size = record.size;

		// Before the branch, so it applies in BOTH passes. Counting
		// and writing run through the same function, and if only the
		// write pass skipped, the total would be too large and the
		// progress would never arrive - a finished installation would then look
		// aborted.
		if (NativeDiscImage_SkipOnExtract(pathBuffer))
		{
			pathBuffer[pathLength] = '\0';
			continue;
		}

		if ((record.flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) != 0)
		{
			ok = NativeDiscImage_WalkDirectory(walk, &entry, pathBuffer, writeAt + nameLength, pathCapacity, depth + 1);
		}
		else if (!walk->writing)
		{
			walk->fileCount++;
			walk->byteCount += entry.size;
		}
		else
		{
			if ((walk->progress != NULL) && !walk->progress(walk->user, pathBuffer, walk->filesDone, walk->fileCount, walk->bytesDone, walk->byteCount))
			{
				walk->result = NATIVE_DISC_IMAGE_ERR_CANCELLED;
				ok = 0;
			}
			else if (!NativeDiscImage_WriteFile(walk, &entry, pathBuffer))
			{
				NativeStr8_CopyToCString(walk->failedPath, sizeof(walk->failedPath), NativeStr8_FromCString(pathBuffer));
				ok = 0;
			}
			else
			{
				walk->filesDone++;
			}
		}

		pathBuffer[pathLength] = '\0';
	}

	free(buf);
	return ok;
}

int NativeDiscImage_IsAvailable(void)
{
	return s_nativeDiscImageAvailable;
}

const char *NativeDiscImage_GetPath(void)
{
	return s_nativeDiscImagePath;
}

// The same open the boot path does, but from a path the user handed over rather
// than from the assets directory. Whatever was open is closed first, so a
// rejected image cannot stay half-mounted behind the next attempt.
int NativeDiscImage_OpenImagePath(const char *path)
{
	if (s_nativeDiscImageFile != NULL)
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
	}

	s_nativeDiscImageAvailable = 0;
	s_nativeDiscImagePath[0] = '\0';

	if ((path == NULL) || (path[0] == '\0'))
	{
		return 0;
	}

	return NativeDiscImage_OpenImageFile(path);
}

// THE SERIAL, OUT OF SYSTEM.CNF.
//
// Every PlayStation disc carries a boot record naming its executable, and that
// name IS the serial: `BOOT = cdrom:\SCUS_944.26;1`. It is the only region and
// title check that does not involve guessing from file names - a PAL disc has
// the same directory layout as an NTSC one, so a layout check would wave it
// through and the game would then fail somewhere deep inside a level load.
//
// Handed back in the printed form, `SCUS-94426`, because that is what somebody
// can compare against the label on their own disc.
//
// Scanned for rather than parsed as a line: SYSTEM.CNF is hand-written on a fair
// number of discs, with and without spaces around the equals sign, with either
// slash after `cdrom:`, and occasionally with the whole thing in lower case.
int NativeDiscImage_ReadBootSerial(char *dst, size_t dstSize)
{
	struct NativeDiscImageFile file;
	u8 *buf = NULL;
	int size = 0;
	int i;
	int found = 0;

	if ((dst == NULL) || (dstSize < 12))
	{
		return 0;
	}

	dst[0] = '\0';

	if (!NativeDiscImage_FindFile(NATIVE_DISC_IMAGE_SYSTEM_CNF, &file))
	{
		return 0;
	}

	if (!NativeDiscImage_ReadFileBytes(NATIVE_DISC_IMAGE_SYSTEM_CNF, 0, &buf, &size) || (size <= 0))
	{
		return 0;
	}

	for (i = 0; (i + 10) <= size; i++)
	{
		const u8 *at = &buf[i];
		int letters;
		int digits;
		int scan;
		size_t writeAt = 0;

		for (letters = 0; letters < 4; letters++)
		{
			u8 byte = NativeStr8_ToUpperAscii(at[letters]);

			if ((byte < 'A') || (byte > 'Z'))
			{
				break;
			}
		}

		if (letters != 4)
		{
			continue;
		}

		if ((at[4] != '_') && (at[4] != '-'))
		{
			continue;
		}

		digits = 0;
		for (scan = 5; ((i + scan) < size) && (digits < 5); scan++)
		{
			u8 byte = at[scan];

			if ((byte >= '0') && (byte <= '9'))
			{
				digits++;
			}
			else if (byte != '.')
			{
				break;
			}
		}

		if (digits != 5)
		{
			continue;
		}

		for (scan = 0; scan < 4; scan++)
		{
			dst[writeAt++] = (char)NativeStr8_ToUpperAscii(at[scan]);
		}
		dst[writeAt++] = '-';

		digits = 0;
		for (scan = 5; (digits < 5) && ((writeAt + 1u) < dstSize); scan++)
		{
			u8 byte = at[scan];

			if ((byte >= '0') && (byte <= '9'))
			{
				dst[writeAt++] = (char)byte;
				digits++;
			}
		}

		dst[writeAt] = '\0';
		found = 1;
		break;
	}

	free(buf);
	return found;
}

int NativeDiscImage_Measure(u32 *fileCountOut, u64 *byteCountOut)
{
	struct NativeDiscImageWalk walk;
	char pathBuffer[NATIVE_DISC_IMAGE_PATH_MAX];

	s_nativeDiscImageWhy[0] = '\0';

	if (!s_nativeDiscImageAvailable)
	{
		return 0;
	}

	memset(&walk, 0, sizeof(walk));
	pathBuffer[0] = '\0';

	if (!NativeDiscImage_WalkDirectory(&walk, &s_nativeDiscImageRoot, pathBuffer, 0, sizeof(pathBuffer), 0))
	{
		return 0;
	}

	if (fileCountOut != NULL)
	{
		*fileCountOut = walk.fileCount;
	}
	if (byteCountOut != NULL)
	{
		*byteCountOut = walk.byteCount;
	}

	return 1;
}

// TWO WALKS, AND THE FIRST ONE IS NOT WASTE.
//
// The count has to be exact before the first byte lands, or the progress line is
// a guess - and a progress line that jumps backwards is how somebody decides the
// unpacker has hung and kills it. The counting walk touches directory sectors
// only, so it costs a fraction of the writing one.
int NativeDiscImage_Extract(const char *destDir, NativeDiscImageProgressFn progress, void *user, int *resultOut, char *failedPath, size_t failedPathSize,
                            u32 *filesWrittenOut, u64 *bytesWrittenOut)
{
	struct NativeDiscImageWalk walk;
	char pathBuffer[NATIVE_DISC_IMAGE_PATH_MAX];
	int ok;

	if (resultOut != NULL)
	{
		*resultOut = NATIVE_DISC_IMAGE_ERR_READ;
	}
	if ((failedPath != NULL) && (failedPathSize != 0))
	{
		failedPath[0] = '\0';
	}

	s_nativeDiscImageWhy[0] = '\0';

	if (!s_nativeDiscImageAvailable || (destDir == NULL))
	{
		return 0;
	}

	memset(&walk, 0, sizeof(walk));
	pathBuffer[0] = '\0';

	if (!NativeDiscImage_WalkDirectory(&walk, &s_nativeDiscImageRoot, pathBuffer, 0, sizeof(pathBuffer), 0))
	{
		if (resultOut != NULL)
		{
			*resultOut = walk.result;
		}
		return 0;
	}

	if (!NativeDiscImage_EnsureDirectory(destDir))
	{
		if (resultOut != NULL)
		{
			*resultOut = NATIVE_DISC_IMAGE_ERR_CREATE;
		}
		return 0;
	}

	// The second pass walks the same tree again, so the loop guard starts over.
	walk.visitedCount = 0;
	walk.entryCount = 0;
	walk.writing = 1;
	walk.destDir = destDir;
	walk.progress = progress;
	walk.user = user;
	walk.result = NATIVE_DISC_IMAGE_OK;
	pathBuffer[0] = '\0';

	ok = NativeDiscImage_WalkDirectory(&walk, &s_nativeDiscImageRoot, pathBuffer, 0, sizeof(pathBuffer), 0);

	if (resultOut != NULL)
	{
		*resultOut = ok ? NATIVE_DISC_IMAGE_OK : walk.result;
	}
	if ((failedPath != NULL) && (failedPathSize != 0))
	{
		NativeStr8_CopyToCString(failedPath, failedPathSize, NativeStr8_FromCString(walk.failedPath));
	}
	if (filesWrittenOut != NULL)
	{
		*filesWrittenOut = walk.filesDone;
	}
	if (bytesWrittenOut != NULL)
	{
		*bytesWrittenOut = walk.bytesDone;
	}

	return ok;
}

//----------------------------------------------------------------------------------------
// WHAT IS REALLY READ
//
// This part asks which of the forty files on this disc the game actually touches
// over a full race and through every menu. Nothing is dropped from the unpacker
// until that has been counted: a file removed on a hunch goes missing three
// menus deep, weeks later, and nobody connects the two.
//
// COUNTED AT THE READ, NOT AT THE LOOKUP, AND THAT IS THE WHOLE DESIGN.
//
// The obvious place would have been NativeDiscImage_FindFile - one function, and
// every path in the game goes through it. It would also have been wrong: the
// asset validator probes all thirty XA tracks by name at boot to check they
// exist, so a lookup counter would report every one of them as used before the
// title screen appears, and the measurement would say "everything is needed".
//
// So the counter sits on the functions that move bytes, and it is keyed by the
// file's start LBA - the disc's own identity for a file, no string needed. Names
// are resolved once, at report time, by walking the directory. A file that is
// only asked about never shows up as read.

#define NATIVE_DISC_IMAGE_READ_SLOTS 64

struct NativeDiscImageReadSlot
{
	u32 lba;
	u32 reads;
	u64 bytes;
};

global_variable int g_cfg_discReport;
global_variable struct NativeDiscImageReadSlot s_nativeDiscImageReads[NATIVE_DISC_IMAGE_READ_SLOTS];
global_variable int s_nativeDiscImageReadCount;
global_variable u64 s_nativeDiscImageReadBytes;
global_variable int s_nativeDiscImageReadOverflow;

void NativeDiscImage_SetReport(int enabled)
{
	g_cfg_discReport = (enabled != 0);
}

internal void NativeDiscImage_NoteRead(const struct NativeDiscImageFile *file, u64 bytes)
{
	int i;

	if (!g_cfg_discReport || (file == NULL))
	{
		return;
	}

	s_nativeDiscImageReadBytes += bytes;

	for (i = 0; i < s_nativeDiscImageReadCount; i++)
	{
		if (s_nativeDiscImageReads[i].lba == file->lba)
		{
			s_nativeDiscImageReads[i].reads++;
			s_nativeDiscImageReads[i].bytes += bytes;
			return;
		}
	}

	if (s_nativeDiscImageReadCount >= NATIVE_DISC_IMAGE_READ_SLOTS)
	{
		// Said out loud in the report rather than dropped quietly. This disc has
		// forty files so it cannot happen here - but a silent cap is how a
		// measurement stops being one without anybody noticing.
		s_nativeDiscImageReadOverflow++;
		return;
	}

	i = s_nativeDiscImageReadCount++;
	s_nativeDiscImageReads[i].lba = file->lba;
	s_nativeDiscImageReads[i].reads = 1;
	s_nativeDiscImageReads[i].bytes = bytes;
}

internal const struct NativeDiscImageReadSlot *NativeDiscImage_FindReadSlot(u32 lba)
{
	int i;

	for (i = 0; i < s_nativeDiscImageReadCount; i++)
	{
		if (s_nativeDiscImageReads[i].lba == lba)
		{
			return &s_nativeDiscImageReads[i];
		}
	}

	return NULL;
}

// ONE WALK, BOTH SIDES OF THE LEDGER.
//
// Printed by walking the directory rather than by printing the counter table,
// so the untouched files list themselves. A list built from what WAS read leaves
// the reader to do the subtraction, and the whole point of the exercise is the
// files nobody thought of.
//
// dirBudget bounds the directories this walk enters. The depth limit alone does
// not: a directory holding many records that point back at the root would fan
// out into more paths than the report could ever print.
internal void NativeDiscImage_ReportDirectory(const struct NativeDiscImageFile *dir, char *pathBuffer, size_t pathLength, size_t pathCapacity, int depth,
                                              u64 *unusedBytes, int *unusedFiles, int *dirBudget)
{
	u8 *buf;
	int size;
	int offset = 0;

	if ((depth > NATIVE_DISC_IMAGE_DIR_DEPTH_MAX) || (*dirBudget <= 0) || (dir->size > NATIVE_DISC_IMAGE_DIR_BYTES_MAX))
	{
		return;
	}

	(*dirBudget)--;

	if (!NativeDiscImage_ReadDirectoryBytes(dir, &buf, &size))
	{
		return;
	}

	while (offset < size)
	{
		struct NativeDiscImageDirRecord record;
		struct NativeDiscImageFile entry;
		size_t nameLength;
		size_t writeAt;
		size_t i;
		u8 length = buf[offset];

		if (length == 0)
		{
			offset = (offset + (int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE) & ~((int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1);
			continue;
		}

		if (!NativeDiscImage_ParseDirRecord(&buf[offset], (size_t)(size - offset), &record))
		{
			break;
		}

		offset += length;

		if ((record.nameLen == 1) && ((record.name[0] == NATIVE_DISC_IMAGE_SELF_RECORD) || (record.name[0] == NATIVE_DISC_IMAGE_PARENT_RECORD)))
		{
			continue;
		}

		nameLength = record.nameLen;
		while ((nameLength > 0) && (record.name[nameLength - 1u] == ' '))
		{
			nameLength--;
		}
		if ((nameLength > 2) && (record.name[nameLength - 2u] == ';') && (record.name[nameLength - 1u] == '1'))
		{
			nameLength -= 2;
		}

		if ((pathLength + nameLength + 2u) >= pathCapacity)
		{
			continue;
		}

		if (pathLength != 0)
		{
			pathBuffer[pathLength] = '/';
		}

		writeAt = pathLength + ((pathLength != 0) ? 1u : 0u);
		for (i = 0; i < nameLength; i++)
		{
			pathBuffer[writeAt + i] = (char)record.name[i];
		}
		pathBuffer[writeAt + nameLength] = '\0';

		entry.lba = record.lba;
		entry.size = record.size;

		if ((record.flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) != 0)
		{
			NativeDiscImage_ReportDirectory(&entry, pathBuffer, writeAt + nameLength, pathCapacity, depth + 1, unusedBytes, unusedFiles, dirBudget);
		}
		else
		{
			const struct NativeDiscImageReadSlot *slot = NativeDiscImage_FindReadSlot(entry.lba);

			if (slot != NULL)
			{
				Platform_Log("[CTR Disc]   READ        %-24s %10u B on the disc, %llu B in %u access(es)\n", pathBuffer, entry.size,
				             (unsigned long long)slot->bytes, slot->reads);
			}
			else
			{
				Platform_Log("[CTR Disc]   UNUSED      %-24s %10u B on the disc\n", pathBuffer, entry.size);
				*unusedBytes += entry.size;
				*unusedFiles += 1;
			}
		}

		pathBuffer[pathLength] = '\0';
	}

	free(buf);
}

// Printed once, at exit. It is the list meant to be seen BEFORE anything is
// dropped, so it stays a report and changes nothing on its own.
void NativeDiscImage_PrintReport(void)
{
	char pathBuffer[NATIVE_DISC_IMAGE_PATH_MAX];
	u64 unusedBytes = 0;
	int unusedFiles = 0;
	int dirBudget = NATIVE_DISC_IMAGE_DIRS_MAX;

	// Once, no matter by how many paths someone ends up here. The report is a
	// list by which something gets dropped from the unpacker - printed twice it
	// reads like two runs.
	static int alreadyPrinted = 0;

	if (!g_cfg_discReport || !s_nativeDiscImageAvailable || alreadyPrinted)
	{
		return;
	}

	alreadyPrinted = 1;

	Platform_Log("[CTR Disc] ---- what was read from the image ----\n");

	pathBuffer[0] = '\0';
	NativeDiscImage_ReportDirectory(&s_nativeDiscImageRoot, pathBuffer, 0, sizeof(pathBuffer), 0, &unusedBytes, &unusedFiles, &dirBudget);

	Platform_Log("[CTR Disc]   %d file(s) touched, %llu byte(s) read\n", s_nativeDiscImageReadCount, (unsigned long long)s_nativeDiscImageReadBytes);
	Platform_Log("[CTR Disc]   %d file(s) unused, %llu byte(s) on the disc (%llu MB)\n", unusedFiles, (unsigned long long)unusedBytes,
	             (unsigned long long)(unusedBytes / (1024u * 1024u)));

	if (s_nativeDiscImageReadOverflow != 0)
	{
		Platform_Log("[CTR Disc]   WARNING: %d file(s) did not fit the table, the list is incomplete\n", s_nativeDiscImageReadOverflow);
	}

	Platform_Log("[CTR Disc]   ONE run is not a measurement - unused here only means: not read in THIS run.\n");
}

//----------------------------------------------------------------------------------------
// THE SELF-TEST (--selftest-disc)
//
// A disc image is foreign input, and the first start writes what it says. So
// the unpacker is run against small made-up images (tools/tests/make_bad_disc.c)
// whose directory records name "..", "C:x", "a/..", point back at the root and
// so on, and the test checks from the outside - by listing folders - that
// nothing lands anywhere but the assets folder it was given.
//
// THE SAME PATH AS THE FIRST-START SCREEN, CALL FOR CALL.
//
// NativeSetup_TryImage in main.c does: open, boot serial, serial == SCUS-94426,
// measure (the counting walk), extract (counting walk + writing walk). This
// does the same calls in the same order, with the same walk and the same
// WriteFile - no copy of any of it. The identity check is not skipped either:
// the made-up images carry a SYSTEM.CNF that names SCUS_944.26, and the reader
// knows no other check of identity (no whole-image hash, no size table).
// Left out are only the parts of the screen that are not about the image: the
// progress callback (NULL here, the walk allows it) and the "unpacked" marker
// main.c writes into the game folder afterwards.

#define NATIVE_DISC_IMAGE_SERIAL_NTSC_U "SCUS-94426" // the same as NATIVE_SETUP_SERIAL_NTSC_U in main.c

// Everything the self-test opened is closed again, so the game's own image
// (none, this early) is not left pointing at a test file.
internal void NativeDiscImage_CloseImage(void)
{
	if (s_nativeDiscImageFile != NULL)
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
	}

	s_nativeDiscImageAvailable = 0;
	s_nativeDiscImagePath[0] = '\0';
	memset(&s_nativeDiscImageRoot, 0, sizeof(s_nativeDiscImageRoot));
}

int NativeDiscImage_SelfTestExtract(const char *imagePath, const char *outDir, char *why, int whyBytes)
{
	char local[NATIVE_DISC_IMAGE_WHY_MAX + NATIVE_DISC_IMAGE_PATH_MAX];
	char serial[32];
	char failedPath[NATIVE_DISC_IMAGE_PATH_MAX];
	u32 files = 0;
	u64 bytes = 0;
	u32 filesWritten = 0;
	u64 bytesWritten = 0;
	int result = NATIVE_DISC_IMAGE_OK;
	int ok = 0;

	local[0] = '\0';
	failedPath[0] = '\0';

	if ((imagePath == NULL) || (outDir == NULL) || (outDir[0] == '\0'))
	{
		snprintf(local, sizeof(local), "no image or no output folder given");
	}
	else if (!NativeDiscImage_OpenImagePath(imagePath))
	{
		snprintf(local, sizeof(local), "not a readable PlayStation disc image (no MODE2/2352 ISO 9660 volume at sector 16)");
	}
	else if (!NativeDiscImage_ReadBootSerial(serial, sizeof(serial)))
	{
		snprintf(local, sizeof(local), "no boot record (SYSTEM.CNF missing or it names no serial)");
	}
	else if (strcmp(serial, NATIVE_DISC_IMAGE_SERIAL_NTSC_U) != 0)
	{
		snprintf(local, sizeof(local), "serial %s, not %s", serial, NATIVE_DISC_IMAGE_SERIAL_NTSC_U);
	}
	else if (!NativeDiscImage_Measure(&files, &bytes))
	{
		snprintf(local, sizeof(local), "file table refused: %s", (s_nativeDiscImageWhy[0] != '\0') ? s_nativeDiscImageWhy : "cannot be read");
	}
	else if (files == 0)
	{
		snprintf(local, sizeof(local), "the image holds no files");
	}
	else if (!NativeDiscImage_Extract(outDir, NULL, NULL, &result, failedPath, sizeof(failedPath), &filesWritten, &bytesWritten))
	{
		snprintf(local, sizeof(local), "extraction stopped (result %d) at \"%s\": %s", result, failedPath,
		         (s_nativeDiscImageWhy[0] != '\0') ? s_nativeDiscImageWhy : "read or write failed");
	}
	else if (filesWritten != files)
	{
		snprintf(local, sizeof(local), "wrote %u of %u files", (unsigned int)filesWritten, (unsigned int)files);
	}
	else
	{
		snprintf(local, sizeof(local), "%u file(s), %llu byte(s)", (unsigned int)filesWritten, (unsigned long long)bytesWritten);
		ok = 1;
	}

	NativeDiscImage_CloseImage();

	if ((why != NULL) && (whyBytes > 0))
	{
		NativeStr8_CopyToCString(why, (size_t)whyBytes, NativeStr8_FromCString(local));
	}

	return ok;
}

// A folder's entries, "." and ".." left out, sorted by name.
struct NativeDiscImageNameList
{
	char **names;
	int count;
	int capacity;
};

internal int NativeDiscImage_NameListAdd(struct NativeDiscImageNameList *list, const char *name)
{
	size_t length = strlen(name);
	char *copy;

	if (list->count == list->capacity)
	{
		int capacity = (list->capacity == 0) ? 32 : (list->capacity * 2);
		char **grown = (char **)realloc(list->names, (size_t)capacity * sizeof(char *));

		if (grown == NULL)
		{
			return 0;
		}

		list->names = grown;
		list->capacity = capacity;
	}

	copy = (char *)malloc(length + 1u);
	if (copy == NULL)
	{
		return 0;
	}

	memcpy(copy, name, length + 1u);
	list->names[list->count++] = copy;
	return 1;
}

internal void NativeDiscImage_NameListFree(struct NativeDiscImageNameList *list)
{
	int i;

	for (i = 0; i < list->count; i++)
	{
		free(list->names[i]);
	}

	free(list->names);
	list->names = NULL;
	list->count = 0;
	list->capacity = 0;
}

internal int NativeDiscImage_NameCompare(const void *left, const void *right)
{
	return strcmp(*(const char *const *)left, *(const char *const *)right);
}

internal int NativeDiscImage_NameListHas(const struct NativeDiscImageNameList *list, const char *name)
{
	int i;

	for (i = 0; i < list->count; i++)
	{
		if (strcmp(list->names[i], name) == 0)
		{
			return 1;
		}
	}

	return 0;
}

// 1 = listed (an empty or missing folder lists as empty), 0 = out of memory.
internal int NativeDiscImage_ListFolder(const char *path, struct NativeDiscImageNameList *list)
{
#if defined(_WIN32)
	char pattern[NATIVE_DISC_IMAGE_PATH_MAX];
	WIN32_FIND_DATAA found;
	HANDLE handle;
	int ok = 1;

	if (snprintf(pattern, sizeof(pattern), "%s/*", path) >= (int)sizeof(pattern))
	{
		return 0;
	}

	handle = FindFirstFileA(pattern, &found);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return 1;
	}

	do
	{
		if ((strcmp(found.cFileName, ".") == 0) || (strcmp(found.cFileName, "..") == 0))
		{
			continue;
		}

		if (!NativeDiscImage_NameListAdd(list, found.cFileName))
		{
			ok = 0;
			break;
		}
	} while (FindNextFileA(handle, &found));

	FindClose(handle);
#else
	DIR *dir = opendir(path);
	struct dirent *entry;
	int ok = 1;

	if (dir == NULL)
	{
		return 1;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
		{
			continue;
		}

		if (!NativeDiscImage_NameListAdd(list, entry->d_name))
		{
			ok = 0;
			break;
		}
	}

	closedir(dir);
#endif

	if (list->count > 1)
	{
		qsort(list->names, (size_t)list->count, sizeof(char *), NativeDiscImage_NameCompare);
	}

	return ok;
}

// Removes an earlier run's out-<name> folder. Links are removed as links,
// never followed: this deletes inside the test folder and nowhere else.
internal int NativeDiscImage_RemoveTree(const char *path, int depth)
{
	struct NativeDiscImageNameList list = {0};
	char child[NATIVE_DISC_IMAGE_PATH_MAX];
	int ok = 1;
	int i;

#if defined(_WIN32)
	DWORD attributes = GetFileAttributesA(path);

	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		return 1;
	}

	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
		return DeleteFileA(path) != 0;
	}

	if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		return RemoveDirectoryA(path) != 0;
	}
#else
	struct stat info;

	if (lstat(path, &info) != 0)
	{
		return 1;
	}

	if (!S_ISDIR(info.st_mode))
	{
		return unlink(path) == 0;
	}
#endif

	if ((depth > 16) || !NativeDiscImage_ListFolder(path, &list))
	{
		NativeDiscImage_NameListFree(&list);
		return 0;
	}

	for (i = 0; (i < list.count) && ok; i++)
	{
		if (snprintf(child, sizeof(child), "%s/%s", path, list.names[i]) >= (int)sizeof(child))
		{
			ok = 0;
			break;
		}

		ok = NativeDiscImage_RemoveTree(child, depth + 1);
	}

	NativeDiscImage_NameListFree(&list);

	if (!ok)
	{
		return 0;
	}

#if defined(_WIN32)
	return RemoveDirectoryA(path) != 0;
#else
	return rmdir(path) == 0;
#endif
}

internal int NativeDiscImage_FileExists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (file == NULL)
	{
		return 0;
	}

	fclose(file);
	return 1;
}

// The prefix check on its own, with paths the name check would never let
// through - so the second line of defence is tested too, not only the first.
internal int NativeDiscImage_SelfTestPathInside(void)
{
	static const struct
	{
		const char *path;
		int inside;
	} cases[] = {
	    {"selftest-root/assets/A", 1},
	    {"selftest-root/assets/A/B.TXT", 1},
	    {"selftest-root/assets/A/../B", 1},
	    {"selftest-root/assets", 0},
	    {"selftest-root/assets/", 0},
	    {"selftest-root/assets/..", 0},
	    {"selftest-root/assets/../x", 0},
	    {"selftest-root/assets/A/../../x", 0},
	    {"selftest-root/assetsEVIL/x", 0},
	    {"selftest-root/x", 0},
	    {"/x", 0},
	};
	int failed = 0;
	int i;

	for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
	{
		if (NativeDiscImage_PathInside("selftest-root/assets", cases[i].path) != cases[i].inside)
		{
			printf("[selftest] path check \"%s\": expected %s - UNEXPECTED\n", cases[i].path, cases[i].inside ? "inside" : "outside");
			failed++;
		}
	}

	if (failed == 0)
	{
		printf("[selftest] path check: %d case(s) - OK\n", i);
	}

	return failed;
}

int NativeDiscImage_SelfTestDir(const char *dir)
{
	struct NativeDiscImageNameList before = {0};
	struct NativeDiscImageNameList after = {0};
	struct NativeDiscImageNameList images = {0};
	int unexpected = 0;
	int good = 0;
	int bad = 0;
	int i;

	if ((dir == NULL) || (dir[0] == '\0') || !NativeDiscImage_ListFolder(dir, &before))
	{
		printf("[selftest] disc: cannot list the folder\n");
		return 1;
	}

	for (i = 0; i < before.count; i++)
	{
		const char *name = before.names[i];
		size_t length = strlen(name);

		if ((length > 4) && NativeStr8_EqualsIgnoreCaseAscii(NativeStr8_FromCString(&name[length - 4u]), NATIVE_STR8_LIT(".bin")))
		{
			if (!NativeDiscImage_NameListAdd(&images, name))
			{
				unexpected++;
			}
		}
	}

	unexpected += NativeDiscImage_SelfTestPathInside();

	for (i = 0; i < images.count; i++)
	{
		struct NativeDiscImageNameList outList = {0};
		const char *name = images.names[i];
		char base[NATIVE_DISC_IMAGE_PATH_MAX];
		char imagePath[NATIVE_DISC_IMAGE_PATH_MAX];
		char outRoot[NATIVE_DISC_IMAGE_PATH_MAX];
		char outAssets[NATIVE_DISC_IMAGE_PATH_MAX];
		char probe[NATIVE_DISC_IMAGE_PATH_MAX];
		char why[NATIVE_DISC_IMAGE_WHY_MAX + NATIVE_DISC_IMAGE_PATH_MAX];
		int expectGood = (strncmp(name, "good-", 5) == 0);
		int expectBad = (strncmp(name, "bad-", 4) == 0);
		int extracted;
		int asExpected;
		int j;

		NativeStr8_CopyToCString(base, sizeof(base), NativeStr8_FromCString(name));
		base[strlen(base) - 4u] = '\0';

		if ((snprintf(imagePath, sizeof(imagePath), "%s/%s", dir, name) >= (int)sizeof(imagePath)) ||
		    (snprintf(outRoot, sizeof(outRoot), "%s/out-%s", dir, base) >= (int)sizeof(outRoot)) ||
		    (snprintf(outAssets, sizeof(outAssets), "%s/assets", outRoot) >= (int)sizeof(outAssets)) ||
		    (snprintf(probe, sizeof(probe), "%s/SYSTEM.CNF", outAssets) >= (int)sizeof(probe)))
		{
			printf("[selftest] %s: path too long - UNEXPECTED\n", name);
			unexpected++;
			continue;
		}

		// A fresh folder per image: whatever an earlier run left there would
		// otherwise count as written by this one.
		if (!NativeDiscImage_RemoveTree(outRoot, 0) || !NativeDiscImage_EnsureDirectory(outRoot))
		{
			printf("[selftest] %s: cannot prepare %s - UNEXPECTED\n", name, outRoot);
			unexpected++;
			continue;
		}

		why[0] = '\0';
		extracted = NativeDiscImage_SelfTestExtract(imagePath, outAssets, why, (int)sizeof(why));

		if (expectGood)
		{
			// Extracted, and extracted INTO the folder: the boot record has to be
			// there where the game will look for it.
			asExpected = extracted && NativeDiscImage_FileExists(probe);
			good++;
		}
		else
		{
			asExpected = expectBad && !extracted;
			bad += expectBad;
		}

		printf("[selftest] %s: %s (%s) - %s\n", name, extracted ? "extracted" : "refused", why, asExpected ? "OK" : "UNEXPECTED");

		if (!asExpected)
		{
			unexpected++;
		}

		// In BOTH cases: out-<name> may hold the assets folder and nothing else.
		if (!NativeDiscImage_ListFolder(outRoot, &outList))
		{
			printf("[selftest] %s: cannot list %s - UNEXPECTED\n", name, outRoot);
			unexpected++;
		}

		for (j = 0; j < outList.count; j++)
		{
			if (strcmp(outList.names[j], "assets") != 0)
			{
				printf("[selftest] %s: \"%s\" written beside the assets folder - UNEXPECTED\n", name, outList.names[j]);
				unexpected++;
			}
		}

		NativeDiscImage_NameListFree(&outList);
	}

	// And one level further out: the test folder may have gained the out-*
	// folders and nothing else.
	if (!NativeDiscImage_ListFolder(dir, &after))
	{
		printf("[selftest] disc: cannot list the folder again - UNEXPECTED\n");
		unexpected++;
	}

	for (i = 0; i < after.count; i++)
	{
		const char *name = after.names[i];
		char expected[NATIVE_DISC_IMAGE_PATH_MAX];
		int known = NativeDiscImage_NameListHas(&before, name);
		int j;

		for (j = 0; (j < images.count) && !known; j++)
		{
			NativeStr8_CopyToCString(expected, sizeof(expected), NativeStr8_FromCString(images.names[j]));
			expected[strlen(expected) - 4u] = '\0';

			if ((strncmp(name, "out-", 4) == 0) && (strcmp(&name[4], expected) == 0))
			{
				known = 1;
			}
		}

		if (!known)
		{
			printf("[selftest] disc: \"%s\" appeared in the test folder - UNEXPECTED\n", name);
			unexpected++;
		}
	}

	if ((good == 0) || (bad == 0))
	{
		printf("[selftest] disc: needs at least one good-*.bin and one bad-*.bin, found %d and %d - UNEXPECTED\n", good, bad);
		unexpected++;
	}

	printf("[selftest] disc: %d image(s), %d good, %d bad, %d unexpected\n", images.count, good, bad, unexpected);
	fflush(stdout);

	NativeDiscImage_NameListFree(&before);
	NativeDiscImage_NameListFree(&after);
	NativeDiscImage_NameListFree(&images);

	return (unexpected == 0) ? 0 : 1;
}
