#include "platform/native_disc_image.h"

#include <platform/native_path.h>

#include <limits.h>
#if defined(_WIN32)
#include <platform/native_win32.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
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
};

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

	if (depth > NATIVE_DISC_IMAGE_DIR_DEPTH_MAX)
	{
		// Not a limit anybody should reach - it is the loop guard for a
		// directory record that points back at itself.
		return 1;
	}

	if (!NativeDiscImage_ReadDirectoryBytes(dir, &buf, &size))
	{
		walk->result = NATIVE_DISC_IMAGE_ERR_READ;
		return 0;
	}

	while ((offset < size) && ok)
	{
		struct NativeDiscImageDirRecord record;
		struct NativeDiscImageFile entry;
		size_t nameLength;
		size_t writeAt;
		size_t i;
		u8 length = buf[offset];

		if (length == 0)
		{
			// Records never straddle a sector; the rest of this one is padding.
			offset = (offset + (int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE) & ~((int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1);
			continue;
		}

		if (!NativeDiscImage_ParseDirRecord(&buf[offset], (size_t)(size - offset), &record))
		{
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

		if ((pathLength + nameLength + 2u) >= pathCapacity)
		{
			walk->result = NATIVE_DISC_IMAGE_ERR_READ;
			ok = 0;
			break;
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
internal void NativeDiscImage_ReportDirectory(const struct NativeDiscImageFile *dir, char *pathBuffer, size_t pathLength, size_t pathCapacity, int depth,
                                              u64 *unusedBytes, int *unusedFiles)
{
	u8 *buf;
	int size;
	int offset = 0;

	if ((depth > NATIVE_DISC_IMAGE_DIR_DEPTH_MAX) || !NativeDiscImage_ReadDirectoryBytes(dir, &buf, &size))
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
			NativeDiscImage_ReportDirectory(&entry, pathBuffer, writeAt + nameLength, pathCapacity, depth + 1, unusedBytes, unusedFiles);
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
	NativeDiscImage_ReportDirectory(&s_nativeDiscImageRoot, pathBuffer, 0, sizeof(pathBuffer), 0, &unusedBytes, &unusedFiles);

	Platform_Log("[CTR Disc]   %d file(s) touched, %llu byte(s) read\n", s_nativeDiscImageReadCount, (unsigned long long)s_nativeDiscImageReadBytes);
	Platform_Log("[CTR Disc]   %d file(s) unused, %llu byte(s) on the disc (%llu MB)\n", unusedFiles, (unsigned long long)unusedBytes,
	             (unsigned long long)(unusedBytes / (1024u * 1024u)));

	if (s_nativeDiscImageReadOverflow != 0)
	{
		Platform_Log("[CTR Disc]   WARNING: %d file(s) did not fit the table, the list is incomplete\n", s_nativeDiscImageReadOverflow);
	}

	Platform_Log("[CTR Disc]   ONE run is not a measurement - unused here only means: not read in THIS run.\n");
}
