// rldpack_image.c - JPEG, TGA and BMP pictures for rldpack (OBJ textures), and
// the deflate of the PNGs of hires-png
//
// Part of the packer, never of the game. The one place where rldpack and
// Reload Studio carry code they did not write themselves: the JPEG, TGA and
// BMP decoders of stb_image (externals/SDL/src/video/stb_image.h, the copy SDL
// keeps; MIT license, see THIRD_PARTY_NOTICES.md). PNG stays with rldpack's
// own reader (include/rldpng.inc). This file is a translation unit of its own
// for the targets rldpack and ReloadStudio, so that stb_image's names stay out
// of tools/rldpack.c.
//
// Only memory is read (no stdio), only JPEG, TGA and BMP are compiled in, without
// SIMD - the scalar decoder gives the same pixels on every processor - and no
// side may exceed RLDIMG_SIDE_MAX, the limit of the PNG reader.
//
// The compressor of the PNGs rldpack hires-png writes (RldImg_Deflate) lives
// here for the same reason: the deflate part (tdefl) of miniz v1.15
// (externals/SDL/src/video/miniz.h, the copy SDL keeps; public domain, see
// THIRD_PARTY_NOTICES.md). Its inflate, zlib-style and ZIP parts and its stdio
// stay out (that copy switches them off itself); no thread, no time stamp.

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// The copy in SDL's tree takes its integer types and its error call from SDL;
// here they are the plain C ones, and the reason is kept for the caller.
typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef signed short Sint16;
typedef unsigned int Uint32;
typedef signed int Sint32;

static const char *s_rldImgWhy;

#define SDL_SetError(format, text) (s_rldImgWhy = (text), 0)

#define RLDIMG_SIDE_MAX 4096

#define STB_IMAGE_STATIC
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS RLDIMG_SIDE_MAX
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "../externals/SDL/src/video/stb_image.h"

// See the declaration in tools/rldpack_obj.inc: 0 = decoded into *rgba
// (width x height x 4 bytes, rows from the top; free it with
// RldImg_Free), otherwise 1 and *why says why.
int RldImg_Decode(const unsigned char *data, size_t size, unsigned int *width, unsigned int *height, unsigned char **rgba, const char **why)
{
	int w = 0;
	int h = 0;
	int channels = 0;
	unsigned char *pixels;

	*width = 0;
	*height = 0;
	*rgba = NULL;
	*why = NULL;
	s_rldImgWhy = NULL;

	if ((size == 0u) || (size > (size_t)INT_MAX))
	{
		*why = (size == 0u) ? "the file is empty" : "the file is too large";
		return 1;
	}

	pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
	if (pixels == NULL)
	{
		*why = (s_rldImgWhy != NULL) ? s_rldImgWhy : "the picture cannot be decoded";
		return 1;
	}
	if ((w <= 0) || (h <= 0) || (w > RLDIMG_SIDE_MAX) || (h > RLDIMG_SIDE_MAX))
	{
		stbi_image_free(pixels);
		*why = "the picture is larger than 4096 pixels on a side";
		return 1;
	}

	*width = (unsigned int)w;
	*height = (unsigned int)h;
	*rgba = pixels;
	return 0;
}

void RldImg_Free(unsigned char *rgba)
{
	stbi_image_free(rgba);
}

// ---------------------------------------------------------------------------
// miniz's deflate. The settings that would choose another code path on another
// processor are pinned: byte-wise loads (as SDL compiles it), little endian.
// The memory functions are the C ones instead of SDL's.

#define MINIZ_SDL_NOUNUSED
#define MINIZ_LITTLE_ENDIAN 1
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MZ_ASSERT(x) ((void)0)
#define SDL_malloc malloc
#define SDL_free free
#define SDL_realloc realloc
#include "../externals/SDL/src/video/miniz.h"
#undef SDL_malloc
#undef SDL_free
#undef SDL_realloc

// See the declaration in tools/rldpack_obj.inc: 0 = *out holds the zlib stream
// (RFC 1950) of data (free it with free), otherwise 1 (out of memory). zlib's
// default level (6): 128 probes of the match finder, lazy matching. The same
// input gives the same bytes.
int RldImg_Deflate(const unsigned char *data, size_t size, unsigned char **out, size_t *outSize)
{
	tdefl_compressor *c = (tdefl_compressor *)malloc(sizeof(tdefl_compressor));
	tdefl_output_buffer buf;

	*out = NULL;
	*outSize = 0;
	memset(&buf, 0, sizeof(buf));
	buf.m_expandable = MZ_TRUE;
	if (c == NULL)
	{
		return 1;
	}
	if ((tdefl_init(c, tdefl_output_buffer_putter, &buf, 128 | TDEFL_WRITE_ZLIB_HEADER) != TDEFL_STATUS_OKAY) ||
	    (tdefl_compress_buffer(c, data, size, TDEFL_FINISH) != TDEFL_STATUS_DONE))
	{
		free(c);
		free(buf.m_pBuf);
		return 1;
	}
	free(c);
	*out = buf.m_pBuf;
	*outSize = buf.m_size;
	return 0;
}
