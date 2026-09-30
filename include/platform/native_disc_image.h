#ifndef NATIVE_DISC_IMAGE_H
#define NATIVE_DISC_IMAGE_H

#include <macros.h>

#include <stddef.h>

struct NativeDiscImageFile
{
	u32 lba;
	u32 size;
};

int NativeDiscImage_Init(const char *assetsDir);
int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut);
int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut);

//----------------------------------------------------------------------------------------
// THE FIRST START
//
// Everything below exists for one moment: the first time somebody runs this who
// has a disc image and nothing else. It reads the image, works out whether it is
// the right game and the right region, and writes the files out beside the
// executable. After that it is never entered again.

enum NativeDiscImageResult
{
	NATIVE_DISC_IMAGE_OK = 0,

	// The image could not be read where it should have been readable. A short
	// download and a scratched rip both land here, which is why the message
	// names the file it stopped on.
	NATIVE_DISC_IMAGE_ERR_READ,

	// A directory or a file could not be created. On Windows this is what
	// "Program Files" produces.
	NATIVE_DISC_IMAGE_ERR_CREATE,

	// A write returned short. There is no portable way to tell a full disc from
	// a failing one, so the message says both.
	NATIVE_DISC_IMAGE_ERR_WRITE,

	NATIVE_DISC_IMAGE_ERR_MEMORY,

	// The window was closed while unpacking ran.
	NATIVE_DISC_IMAGE_ERR_CANCELLED,
};

// Returning zero cancels. Called once per file before it is written, so a
// cancelled run stops between files and never leaves a half-written one.
typedef int (*NativeDiscImageProgressFn)(void *user, const char *path, u32 fileIndex, u32 fileCount, u64 bytesDone, u64 bytesTotal);

int NativeDiscImage_OpenImagePath(const char *path);
int NativeDiscImage_IsAvailable(void);
const char *NativeDiscImage_GetPath(void);
int NativeDiscImage_ReadBootSerial(char *dst, size_t dstSize);
int NativeDiscImage_Measure(u32 *fileCountOut, u64 *byteCountOut);
int NativeDiscImage_Extract(const char *destDir, NativeDiscImageProgressFn progress, void *user, int *resultOut, char *failedPath, size_t failedPathSize,
                            u32 *filesWrittenOut, u64 *bytesWrittenOut);

// Creates a directory and every directory on the way to it. It lives here
// because the unpacker is what needed it first; everything else borrows this one
// rather than growing a second copy, which is how two answers to one question
// get into a tree.
int NativeDiscImage_EnsureDirectory(const char *path);

// --disc-report. Counts bytes actually READ per file, so the asset validator's
// existence probes do not count as use, and prints both sides of the ledger at
// exit: what was read, and what was never touched.
void NativeDiscImage_SetReport(int enabled);
void NativeDiscImage_PrintReport(void);

// THE SELF-TEST OF THE UNPACKER.
//
// NativeDiscImage_SelfTestExtract runs the first-start screen's calls in its
// order - open, boot serial (must be SCUS-94426), measure, extract - with the
// same walk and the same writer, into outDir. 1 = extracted, 0 = refused; why
// gets the reason (on success: files and bytes). Every entry name is checked
// to be one plain path component and every host path to lie strictly inside
// outDir before anything is created, so nothing is ever written outside it.
// The image is closed again afterwards.
int NativeDiscImage_SelfTestExtract(const char *imagePath, const char *outDir, char *why, int whyBytes);

// --selftest-disc <dir>: every *.bin in dir, sorted, extracted into a fresh
// dir/out-<name>/assets. good-* must be extracted, bad-* refused, and in both
// cases out-<name> may hold nothing but the assets folder and dir nothing new
// but the out-* folders (checked by listing). Prints one [selftest] line per
// image; returns the exit code: 0 only if everything is as expected and at
// least one good- and one bad- image exist.
int NativeDiscImage_SelfTestDir(const char *dir);

#endif
