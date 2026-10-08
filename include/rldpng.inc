// rldpng.inc - a PNG reader for rldpack and the game, written in-house
//
// AN INCLUDE, NOT A MODULE. include/rldchar.inc includes it, and through it
// tools/rldpack.c and the game (platform/native_chars.c); u8, u16, u32 and u64
// come from the including file. rldpack reads the portrait an author gives
// make-char with --icon (tools/rldpack_char.inc turns it into the CICN chunk)
// and the textures of CTXT; the game reads only the textures of CTXT, only
// with the native preview (NATIVE DRIVERS set to PREVIEW, or --native-preview).
// No zlib and no other library - the
// reason is at the head of tools/rldpack.c. The inflate below is the one of RFC 1951, inside the zlib
// wrapper of RFC 1950, read by the rules of the PNG specification (W3C,
// second edition).
//
// FOREIGN INPUT. The file comes from anyone. Every length, size and index is
// checked against the bytes that are there before it is used:
//   * chunk lengths against the rest of the file, every CRC, the Adler-32
//   * the image size against fixed limits BEFORE anything is allocated, the
//     compressed data (IDAT) against what such an image can need
//   * the inflated data against the exact size the header promises - not one
//     byte more is written, a byte less is an error
//   * every Huffman table against over-subscription, every distance against
//     the data written so far, every palette index against the palette
// A bad file gives a fixed English reason, never a crash and never a guess.
//
// WHAT IS READ: color types 0 (gray), 2 (RGB), 3 (palette), 4 (gray + alpha)
// and 6 (RGBA) in the bit depths the specification allows per type (1, 2, 4,
// 8, 16), tRNS for types 0, 2 and 3. Ancillary chunks (gAMA, pHYs, text, ...)
// are skipped; an unknown critical chunk is refused. Interlaced images (Adam7,
// PNG section 8.2) are read pass by pass: seven reduced images, each with
// filters of its own, whose pixels land on their places in the whole picture -
// the same pixels as the image saved without interlacing.
//
// THE RESULT is 8-bit RGBA, rows top to bottom, no color management. 16-bit
// samples are rounded to 8 bits, smaller depths widened (v x 255 / max). All in
// integers: the same file gives the same pixels everywhere.

#ifndef RLDPNG_INC
#define RLDPNG_INC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RLDPNG_SIDE_MAX 4096u                  // width and height each
#define RLDPNG_PIXELS_MAX (4096u * 4096u)      // width x height
#define RLDPNG_PALETTE_MAX 256u

enum
{
	RLDPNG_OK = 0,
	RLDPNG_BAD,          // not a PNG rldpack can read; the reason says why
	RLDPNG_TOO_LARGE,    // wider or higher than RLDPNG_SIDE_MAX, or more than RLDPNG_PIXELS_MAX pixels
	RLDPNG_MEMORY        // out of memory
};

struct RldPngImage
{
	u32 width;
	u32 height;
	u32 colorType;   // as in IHDR
	u32 bitDepth;    // as in IHDR
	u32 interlace;   // as in IHDR: 0 none, 1 Adam7
	u8 *rgba;        // width x height x 4 bytes, NULL unless RldPng_Decode said RLDPNG_OK
};

static void RldPng_Free(struct RldPngImage *image)
{
	free(image->rgba);
	memset(image, 0, sizeof(*image));
}

static u32 RldPng_BE32(const u8 *at)
{
	return ((u32)at[0] << 24) | ((u32)at[1] << 16) | ((u32)at[2] << 8) | (u32)at[3];
}

//========================================================================================
// CHECKSUMS
//========================================================================================

// CRC-32 of the PNG chunks (ISO 3309, reflected polynomial 0xEDB88320). The
// table is made on first use; it is the same table every time.
static u32 s_rldPngCrcTable[256];
static int s_rldPngCrcReady;

static u32 RldPng_Crc(const u8 *bytes, size_t size)
{
	u32 crc = 0xFFFFFFFFu;
	size_t i;

	if (!s_rldPngCrcReady)
	{
		u32 n;

		for (n = 0; n < 256u; n++)
		{
			u32 c = n;
			int k;

			for (k = 0; k < 8; k++)
			{
				c = ((c & 1u) != 0u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			}
			s_rldPngCrcTable[n] = c;
		}
		s_rldPngCrcReady = 1;
	}

	for (i = 0; i < size; i++)
	{
		crc = s_rldPngCrcTable[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
	}

	return crc ^ 0xFFFFFFFFu;
}

// Adler-32 of the zlib stream (RFC 1950). 5552 bytes are the most that can be
// summed before the 32-bit sums could overflow.
static u32 RldPng_Adler(const u8 *bytes, size_t size)
{
	u32 a = 1;
	u32 b = 0;

	while (size != 0u)
	{
		size_t n = (size < 5552u) ? size : 5552u;

		size -= n;
		while (n-- != 0u)
		{
			a += *bytes++;
			b += a;
		}
		a %= 65521u;
		b %= 65521u;
	}

	return (b << 16) | a;
}

//========================================================================================
// INFLATE (RFC 1951)
//========================================================================================
//
// Bits come least significant first; a Huffman code is read one bit at a time
// and decoded canonically from the number of codes per length (the code of
// each length starts where the shorter ones ended). Slow next to zlib, but a
// portrait is a few kilobytes, and every step is checked.

#define RLDPNG_LENGTH_CODES 29
#define RLDPNG_DISTANCE_CODES 30
#define RLDPNG_MAX_BITS 15

static const u16 s_rldPngLengthBase[RLDPNG_LENGTH_CODES] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 s_rldPngLengthExtra[RLDPNG_LENGTH_CODES] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 s_rldPngDistanceBase[RLDPNG_DISTANCE_CODES] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 s_rldPngDistanceExtra[RLDPNG_DISTANCE_CODES] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// The order in which a dynamic block lists the lengths of the code length code.
static const u8 s_rldPngCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct RldPngHuffman
{
	u16 count[RLDPNG_MAX_BITS + 1];   // codes per length; count[0] = symbols without a code
	u16 symbol[320];                  // the symbols in code order
};

struct RldPngInflate
{
	const u8 *in;
	size_t inSize;
	size_t inAt;
	u32 bits;          // not yet used bits, the next one lowest
	u32 bitCount;      // always below 8 between two reads
	int overrun;       // a read went past the end of the input
	u8 *out;
	size_t outSize;    // exactly the size the header promises
	size_t outAt;
	const char *why;   // the first reason, fixed text
};

static int RldPng_InflateFail(struct RldPngInflate *s, const char *why)
{
	if (s->why == NULL)
	{
		s->why = why;
	}
	return 0;
}

// need <= 16. Past the end: 0 and the overrun flag, which every caller checks.
static u32 RldPng_Bits(struct RldPngInflate *s, u32 need)
{
	u32 value;

	while (s->bitCount < need)
	{
		if (s->inAt >= s->inSize)
		{
			s->overrun = 1;
			return 0;
		}
		s->bits |= (u32)s->in[s->inAt++] << s->bitCount;
		s->bitCount += 8u;
	}

	value = s->bits & ((1u << need) - 1u);
	s->bits >>= need;
	s->bitCount -= need;
	return value;
}

// The table of a canonical code from its code lengths (each 0..15). Returns 0
// for a complete code, > 0 for an incomplete one, < 0 for an over-subscribed
// one (more codes than bits). A code without any lengths is complete: every
// attempt to decode with it fails.
static int RldPng_Build(struct RldPngHuffman *h, const u8 *lengths, u32 n)
{
	u16 offset[RLDPNG_MAX_BITS + 1];
	int left = 1;
	u32 length;
	u32 symbol;

	memset(h->count, 0, sizeof(h->count));
	for (symbol = 0; symbol < n; symbol++)
	{
		h->count[lengths[symbol]]++;
	}

	if (h->count[0] == n)
	{
		return 0;
	}

	for (length = 1; length <= RLDPNG_MAX_BITS; length++)
	{
		left <<= 1;
		left -= (int)h->count[length];
		if (left < 0)
		{
			return left;
		}
	}

	offset[1] = 0;
	for (length = 1; length < RLDPNG_MAX_BITS; length++)
	{
		offset[length + 1u] = (u16)(offset[length] + h->count[length]);
	}

	for (symbol = 0; symbol < n; symbol++)
	{
		if (lengths[symbol] != 0u)
		{
			h->symbol[offset[lengths[symbol]]++] = (u16)symbol;
		}
	}

	return left;
}

// One symbol, -1 = no code fits (or the input ended).
static int RldPng_Symbol(struct RldPngInflate *s, const struct RldPngHuffman *h)
{
	int code = 0;    // the bits so far
	int first = 0;   // the first code of this length
	int index = 0;   // the index of that first code in symbol[]
	u32 length;

	for (length = 1; length <= RLDPNG_MAX_BITS; length++)
	{
		const int count = (int)h->count[length];

		code |= (int)RldPng_Bits(s, 1u);
		if (s->overrun)
		{
			return -1;
		}

		if ((code - count) < first)
		{
			return (int)h->symbol[index + (code - first)];
		}

		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}

	return -1;
}

// The data of one Huffman block, up to its end code.
static int RldPng_Codes(struct RldPngInflate *s, const struct RldPngHuffman *literal, const struct RldPngHuffman *distance)
{
	for (;;)
	{
		int symbol = RldPng_Symbol(s, literal);

		if (symbol < 0)
		{
			return RldPng_InflateFail(s, s->overrun ? "the image data ends early" : "the image data holds an unknown code");
		}

		if (symbol < 256)
		{
			if (s->outAt >= s->outSize)
			{
				return RldPng_InflateFail(s, "the image data holds more bytes than the image needs");
			}
			s->out[s->outAt++] = (u8)symbol;
		}
		else if (symbol == 256)
		{
			return 1;
		}
		else
		{
			u32 length;
			u32 back;
			int d;

			symbol -= 257;
			if (symbol >= RLDPNG_LENGTH_CODES)
			{
				return RldPng_InflateFail(s, "the image data holds an invalid length code");
			}
			length = (u32)s_rldPngLengthBase[symbol] + RldPng_Bits(s, s_rldPngLengthExtra[symbol]);

			d = RldPng_Symbol(s, distance);
			if ((d < 0) || (d >= RLDPNG_DISTANCE_CODES))
			{
				return RldPng_InflateFail(s, s->overrun ? "the image data ends early" : "the image data holds an invalid distance code");
			}
			back = (u32)s_rldPngDistanceBase[d] + RldPng_Bits(s, s_rldPngDistanceExtra[d]);

			if (s->overrun)
			{
				return RldPng_InflateFail(s, "the image data ends early");
			}
			if ((size_t)back > s->outAt)
			{
				return RldPng_InflateFail(s, "the image data refers back before its start");
			}
			if ((size_t)length > (s->outSize - s->outAt))
			{
				return RldPng_InflateFail(s, "the image data holds more bytes than the image needs");
			}

			// Byte by byte: the copy may overlap itself (distance < length).
			while (length-- != 0u)
			{
				s->out[s->outAt] = s->out[s->outAt - back];
				s->outAt++;
			}
		}
	}
}

static int RldPng_Stored(struct RldPngInflate *s)
{
	u32 length;
	u32 inverse;

	// To the next byte: the bits left belong to the byte already read.
	s->bits = 0;
	s->bitCount = 0;

	if ((s->inSize - s->inAt) < 4u)
	{
		return RldPng_InflateFail(s, "the image data ends early");
	}

	length = (u32)s->in[s->inAt] | ((u32)s->in[s->inAt + 1u] << 8);
	inverse = (u32)s->in[s->inAt + 2u] | ((u32)s->in[s->inAt + 3u] << 8);
	s->inAt += 4u;

	if (length != (~inverse & 0xFFFFu))
	{
		return RldPng_InflateFail(s, "a stored block of the image data has a wrong length");
	}
	if ((size_t)length > (s->inSize - s->inAt))
	{
		return RldPng_InflateFail(s, "the image data ends early");
	}
	if ((size_t)length > (s->outSize - s->outAt))
	{
		return RldPng_InflateFail(s, "the image data holds more bytes than the image needs");
	}

	memcpy(&s->out[s->outAt], &s->in[s->inAt], length);
	s->outAt += length;
	s->inAt += length;
	return 1;
}

static int RldPng_Fixed(struct RldPngInflate *s)
{
	struct RldPngHuffman literal;
	struct RldPngHuffman distance;
	u8 lengths[288];
	u32 i;

	for (i = 0; i < 288u; i++)
	{
		lengths[i] = (u8)((i < 144u) ? 8u : ((i < 256u) ? 9u : ((i < 280u) ? 7u : 8u)));
	}
	RldPng_Build(&literal, lengths, 288u);

	for (i = 0; i < (u32)RLDPNG_DISTANCE_CODES; i++)
	{
		lengths[i] = 5u;
	}
	RldPng_Build(&distance, lengths, (u32)RLDPNG_DISTANCE_CODES);

	return RldPng_Codes(s, &literal, &distance);
}

static int RldPng_Dynamic(struct RldPngInflate *s)
{
	struct RldPngHuffman lengthCode;
	struct RldPngHuffman literal;
	struct RldPngHuffman distance;
	u8 lengths[320];
	u32 literals;
	u32 distances;
	u32 codes;
	u32 index;
	int left;

	literals = RldPng_Bits(s, 5u) + 257u;
	distances = RldPng_Bits(s, 5u) + 1u;
	codes = RldPng_Bits(s, 4u) + 4u;
	if (s->overrun)
	{
		return RldPng_InflateFail(s, "the image data ends early");
	}
	if ((literals > 286u) || (distances > 30u))
	{
		return RldPng_InflateFail(s, "a dynamic block of the image data has too many codes");
	}

	memset(lengths, 0, sizeof(lengths));
	for (index = 0; index < codes; index++)
	{
		lengths[s_rldPngCodeLengthOrder[index]] = (u8)RldPng_Bits(s, 3u);
	}
	if (s->overrun)
	{
		return RldPng_InflateFail(s, "the image data ends early");
	}

	// The code length code has to be complete.
	if (RldPng_Build(&lengthCode, lengths, 19u) != 0)
	{
		return RldPng_InflateFail(s, "a dynamic block of the image data has a broken code table");
	}

	index = 0;
	while (index < (literals + distances))
	{
		int symbol = RldPng_Symbol(s, &lengthCode);
		u32 repeat;
		u8 value = 0;

		if (symbol < 0)
		{
			return RldPng_InflateFail(s, s->overrun ? "the image data ends early" : "a dynamic block of the image data has a broken code table");
		}

		if (symbol < 16)
		{
			lengths[index++] = (u8)symbol;
			continue;
		}

		if (symbol == 16)
		{
			if (index == 0u)
			{
				return RldPng_InflateFail(s, "a dynamic block of the image data repeats a length before the first one");
			}
			value = lengths[index - 1u];
			repeat = 3u + RldPng_Bits(s, 2u);
		}
		else if (symbol == 17)
		{
			repeat = 3u + RldPng_Bits(s, 3u);
		}
		else
		{
			repeat = 11u + RldPng_Bits(s, 7u);
		}

		if (s->overrun)
		{
			return RldPng_InflateFail(s, "the image data ends early");
		}
		if ((index + repeat) > (literals + distances))
		{
			return RldPng_InflateFail(s, "a dynamic block of the image data has too many code lengths");
		}

		while (repeat-- != 0u)
		{
			lengths[index++] = value;
		}
	}

	if (lengths[256] == 0u)
	{
		return RldPng_InflateFail(s, "a dynamic block of the image data has no end code");
	}

	// Incomplete codes only in the one form the format allows: a single code of one bit.
	left = RldPng_Build(&literal, lengths, literals);
	if ((left < 0) || ((left > 0) && (literals != (u32)(literal.count[0] + literal.count[1]))))
	{
		return RldPng_InflateFail(s, "a dynamic block of the image data has a broken literal table");
	}

	left = RldPng_Build(&distance, &lengths[literals], distances);
	if ((left < 0) || ((left > 0) && (distances != (u32)(distance.count[0] + distance.count[1]))))
	{
		return RldPng_InflateFail(s, "a dynamic block of the image data has a broken distance table");
	}

	return RldPng_Codes(s, &literal, &distance);
}

// A zlib stream (RFC 1950) into exactly outSize bytes. NULL = sound, else the reason.
static const char *RldPng_Inflate(const u8 *in, size_t inSize, u8 *out, size_t outSize)
{
	struct RldPngInflate s;
	u32 last = 0;

	memset(&s, 0, sizeof(s));
	s.in = in;
	s.inSize = inSize;
	s.out = out;
	s.outSize = outSize;

	if (inSize < 6u)
	{
		return "the image data is too short";
	}

	// CMF: method 8 (deflate), window at most 32 KB; FLG: the check, no preset dictionary.
	if (((in[0] & 0x0Fu) != 8u) || ((in[0] >> 4) > 7u) || (((((u32)in[0]) << 8) | (u32)in[1]) % 31u) != 0u || ((in[1] & 0x20u) != 0u))
	{
		return "the image data is not a zlib stream";
	}
	s.inAt = 2;

	while (!last)
	{
		u32 type;
		int ok;

		last = RldPng_Bits(&s, 1u);
		type = RldPng_Bits(&s, 2u);
		if (s.overrun)
		{
			return "the image data ends early";
		}

		if (type == 0u)
		{
			ok = RldPng_Stored(&s);
		}
		else if (type == 1u)
		{
			ok = RldPng_Fixed(&s);
		}
		else if (type == 2u)
		{
			ok = RldPng_Dynamic(&s);
		}
		else
		{
			ok = RldPng_InflateFail(&s, "the image data uses a reserved block type");
		}

		if (!ok)
		{
			return (s.why != NULL) ? s.why : "the image data cannot be read";
		}
	}

	if (s.outAt != s.outSize)
	{
		return "the image data holds fewer bytes than the image needs";
	}

	// The Adler-32 follows on the next byte boundary.
	if ((s.inSize - s.inAt) < 4u)
	{
		return "the image data ends before its checksum";
	}
	if (RldPng_BE32(&in[s.inAt]) != RldPng_Adler(out, outSize))
	{
		return "the checksum (Adler-32) of the image data does not match - the file is damaged";
	}

	return NULL;
}

//========================================================================================
// THE PNG
//========================================================================================

static const u8 s_rldPngSignature[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};

// Samples per pixel by color type, 0 = not a color type.
static u32 RldPng_Channels(u32 colorType)
{
	switch (colorType)
	{
	case 0:
		return 1u;
	case 2:
		return 3u;
	case 3:
		return 1u;
	case 4:
		return 2u;
	case 6:
		return 4u;
	default:
		return 0u;
	}
}

// The bit depths the specification allows per color type.
static int RldPng_DepthAllowed(u32 colorType, u32 depth)
{
	switch (colorType)
	{
	case 0:
		return (depth == 1u) || (depth == 2u) || (depth == 4u) || (depth == 8u) || (depth == 16u);
	case 3:
		return (depth == 1u) || (depth == 2u) || (depth == 4u) || (depth == 8u);
	case 2:
	case 4:
	case 6:
		return (depth == 8u) || (depth == 16u);
	default:
		return 0;
	}
}

static int RldPng_Abs(int value)
{
	return (value < 0) ? -value : value;
}

// The filters of the scanlines, undone in place (PNG section 9). bpp = bytes
// per complete pixel, at least 1.
static const char *RldPng_Unfilter(u8 *raw, u32 height, size_t rowBytes, size_t bpp)
{
	const u8 *previous = NULL;
	u32 y;

	for (y = 0; y < height; y++)
	{
		u8 *line = &raw[(size_t)y * (rowBytes + 1u)];
		u8 *row = &line[1];
		size_t i;

		switch (line[0])
		{
		case 0:
			break;

		case 1:
			for (i = bpp; i < rowBytes; i++)
			{
				row[i] = (u8)(row[i] + row[i - bpp]);
			}
			break;

		case 2:
			for (i = 0; (previous != NULL) && (i < rowBytes); i++)
			{
				row[i] = (u8)(row[i] + previous[i]);
			}
			break;

		case 3:
			for (i = 0; i < rowBytes; i++)
			{
				const u32 a = (i >= bpp) ? row[i - bpp] : 0u;
				const u32 b = (previous != NULL) ? previous[i] : 0u;

				row[i] = (u8)(row[i] + ((a + b) >> 1));
			}
			break;

		case 4:
			for (i = 0; i < rowBytes; i++)
			{
				const int a = (i >= bpp) ? row[i - bpp] : 0;
				const int b = (previous != NULL) ? previous[i] : 0;
				const int c = ((i >= bpp) && (previous != NULL)) ? previous[i - bpp] : 0;
				const int p = a + b - c;
				const int pa = RldPng_Abs(p - a);
				const int pb = RldPng_Abs(p - b);
				const int pc = RldPng_Abs(p - c);

				row[i] = (u8)(row[i] + (((pa <= pb) && (pa <= pc)) ? a : ((pb <= pc) ? b : c)));
			}
			break;

		default:
			return "a row of the image uses an unknown filter";
		}

		previous = row;
	}

	return NULL;
}

// Sample `index` of a row at `depth` bits, as stored.
static u32 RldPng_Sample(const u8 *row, u32 index, u32 depth)
{
	if (depth == 8u)
	{
		return row[index];
	}
	if (depth == 16u)
	{
		return ((u32)row[2u * index] << 8) | (u32)row[2u * index + 1u];
	}

	{
		const u32 bit = index * depth;
		const u32 shift = 8u - depth - (bit & 7u);

		return ((u32)row[bit >> 3] >> shift) & ((1u << depth) - 1u);
	}
}

// A sample at `depth` bits to 8 bits: widened exactly, 16 bits rounded.
static u8 RldPng_To8(u32 value, u32 depth)
{
	switch (depth)
	{
	case 1:
		return (u8)(value * 255u);
	case 2:
		return (u8)(value * 85u);
	case 4:
		return (u8)(value * 17u);
	case 16:
		return (u8)((value * 255u + 32767u) / 65535u);
	default:
		return (u8)value;
	}
}

// Adam7 (PNG section 8.2): per pass the first column and row and the steps
// between them. A picture without interlacing is one pass {0, 0, 1, 1}.
static const u8 s_rldPngAdam7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
static const u8 s_rldPngOnePass[4] = {0, 0, 1, 1};

// The size of pass `pass` (0..6 with interlacing, else only 0): its pixels per
// row and its rows, 0 for a pass without pixels - such a pass has no bytes in
// the data, not even the filter bytes. Returns the pass's geometry.
static const u8 *RldPng_Pass(const struct RldPngImage *image, u32 pass, u32 *w, u32 *h)
{
	const u8 *g = (image->interlace != 0u) ? s_rldPngAdam7[pass] : s_rldPngOnePass;

	*w = (image->width > g[0]) ? ((image->width - g[0] + g[2] - 1u) / g[2]) : 0u;
	*h = (image->height > g[1]) ? ((image->height - g[1] + g[3] - 1u) / g[3]) : 0u;
	return g;
}

// A PNG file in memory -> 8-bit RGBA in out->rgba. RLDPNG_OK, or a code and
// *why (fixed text, or a text in a static buffer that the next call overwrites).
static int RldPng_Decode(const u8 *png, size_t size, struct RldPngImage *out, const char **why)
{
	static char text[160];
	u8 palette[RLDPNG_PALETTE_MAX * 3u];
	u8 paletteAlpha[RLDPNG_PALETTE_MAX];
	u32 paletteCount = 0;
	u32 trnsKey[3] = {0, 0, 0};
	int hasTrnsKey = 0;
	int seenHeader = 0;
	int seenEnd = 0;
	int seenData = 0;
	u32 channels = 0;
	u8 *idat = NULL;
	size_t idatSize = 0;
	u8 *raw = NULL;
	size_t rawSize = 0;
	size_t idatMax = 0;
	size_t at = 8;
	const char *bad;
	size_t rawAt = 0;
	u32 passes = 1;
	u32 pass;
	u32 x;
	u32 y;

	memset(out, 0, sizeof(*out));
	memset(paletteAlpha, 0xFF, sizeof(paletteAlpha));
	memset(palette, 0, sizeof(palette));
	*why = NULL;

	if ((png == NULL) || (size < 8u) || (memcmp(png, s_rldPngSignature, 8u) != 0))
	{
		*why = "it does not start with the PNG signature - this is not a PNG file";
		return RLDPNG_BAD;
	}

	while (!seenEnd)
	{
		const u8 *type;
		const u8 *body;
		u32 length;
		int k;

		if ((size - at) < 12u)
		{
			*why = "the file ends before its last chunk (IEND) - it was not saved completely";
			free(idat);
			return RLDPNG_BAD;
		}

		length = RldPng_BE32(&png[at]);
		type = &png[at + 4u];
		body = &png[at + 8u];

		if ((length > 0x7FFFFFFFu) || ((size_t)length > (size - at - 12u)))
		{
			*why = "a chunk is longer than the rest of the file - the file is cut off or damaged";
			free(idat);
			return RLDPNG_BAD;
		}

		for (k = 0; k < 4; k++)
		{
			const u8 c = type[k];

			if (!(((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'))))
			{
				*why = "a chunk has a name that is not four letters - the file is damaged";
				free(idat);
				return RLDPNG_BAD;
			}
		}

		if (RldPng_Crc(type, (size_t)length + 4u) != RldPng_BE32(&body[length]))
		{
			snprintf(text, sizeof(text), "the checksum (CRC) of chunk %.4s does not match - the file is damaged", (const char *)type);
			*why = text;
			free(idat);
			return RLDPNG_BAD;
		}

		if (!seenHeader && (memcmp(type, "IHDR", 4u) != 0))
		{
			*why = "the first chunk is not IHDR";
			free(idat);
			return RLDPNG_BAD;
		}

		if (memcmp(type, "IHDR", 4u) == 0)
		{
			if (seenHeader || (length != 13u))
			{
				*why = "the header chunk IHDR is repeated or not 13 bytes long";
				free(idat);
				return RLDPNG_BAD;
			}
			seenHeader = 1;

			out->width = RldPng_BE32(&body[0]);
			out->height = RldPng_BE32(&body[4]);
			out->bitDepth = body[8];
			out->colorType = body[9];
			channels = RldPng_Channels(out->colorType);

			if ((out->width == 0u) || (out->height == 0u) || (out->width > 0x7FFFFFFFu) || (out->height > 0x7FFFFFFFu))
			{
				*why = "the image has no pixels, or a size outside the PNG rules";
				return RLDPNG_BAD;
			}
			if ((channels == 0u) || !RldPng_DepthAllowed(out->colorType, out->bitDepth))
			{
				snprintf(text, sizeof(text), "color type %u with bit depth %u is not a valid PNG", out->colorType, out->bitDepth);
				*why = text;
				return RLDPNG_BAD;
			}
			if ((body[10] != 0u) || (body[11] != 0u))
			{
				*why = "it uses an unknown compression or filter method";
				return RLDPNG_BAD;
			}
			if (body[12] > 1u)
			{
				*why = "it uses an unknown interlace method";
				return RLDPNG_BAD;
			}
			out->interlace = body[12];
			passes = (out->interlace != 0u) ? 7u : 1u;
			if ((out->width > RLDPNG_SIDE_MAX) || (out->height > RLDPNG_SIDE_MAX) || (((u64)out->width * (u64)out->height) > (u64)RLDPNG_PIXELS_MAX))
			{
				snprintf(text, sizeof(text), "the image is %u x %u pixels; rldpack reads at most %u x %u", out->width, out->height, RLDPNG_SIDE_MAX, RLDPNG_SIDE_MAX);
				*why = text;
				return RLDPNG_TOO_LARGE;
			}

			// The sizes, in 64 bits first: at most 4096 x 64 bits per row, a
			// filter byte per row of each pass. The compressed data of an image
			// is never much larger than the image: stored blocks add 5 bytes per
			// 65535. Generous room on top, and never more than the file holds.
			{
				u64 total = 0;

				for (pass = 0; pass < passes; pass++)
				{
					u32 w;
					u32 h;

					(void)RldPng_Pass(out, pass, &w, &h);
					if ((w != 0u) && (h != 0u))
					{
						total += (((u64)w * (u64)channels * (u64)out->bitDepth + 7u) / 8u + 1u) * (u64)h;
					}
				}
				rawSize = (size_t)total;
				idatMax = rawSize + rawSize / 8u + 4096u;
				idatMax = (idatMax < size) ? idatMax : size;
			}
		}
		else if (memcmp(type, "PLTE", 4u) == 0)
		{
			if (seenData || (paletteCount != 0u) || (length == 0u) || ((length % 3u) != 0u) || ((length / 3u) > RLDPNG_PALETTE_MAX))
			{
				*why = "the palette (PLTE) is misplaced, repeated or of a wrong size";
				free(idat);
				return RLDPNG_BAD;
			}
			if ((out->colorType == 3u) && ((length / 3u) > (1u << out->bitDepth)))
			{
				*why = "the palette (PLTE) has more entries than the bit depth can address";
				free(idat);
				return RLDPNG_BAD;
			}
			paletteCount = length / 3u;
			memcpy(palette, body, length);
		}
		else if (memcmp(type, "tRNS", 4u) == 0)
		{
			// The transparent color (types 0 and 2) or the alpha of the palette
			// entries (type 3). Types 4 and 6 have alpha of their own; there
			// it is left alone.
			if (out->colorType == 0u)
			{
				if (length != 2u)
				{
					*why = "the transparency chunk (tRNS) has a wrong size";
					free(idat);
					return RLDPNG_BAD;
				}
				trnsKey[0] = ((u32)body[0] << 8) | (u32)body[1];
				hasTrnsKey = 1;
			}
			else if (out->colorType == 2u)
			{
				if (length != 6u)
				{
					*why = "the transparency chunk (tRNS) has a wrong size";
					free(idat);
					return RLDPNG_BAD;
				}
				for (k = 0; k < 3; k++)
				{
					trnsKey[k] = ((u32)body[2 * k] << 8) | (u32)body[2 * k + 1];
				}
				hasTrnsKey = 1;
			}
			else if (out->colorType == 3u)
			{
				if ((paletteCount == 0u) || (length > paletteCount))
				{
					*why = "the transparency chunk (tRNS) does not fit the palette";
					free(idat);
					return RLDPNG_BAD;
				}
				memcpy(paletteAlpha, body, length);
			}
		}
		else if (memcmp(type, "IDAT", 4u) == 0)
		{
			if ((out->colorType == 3u) && (paletteCount == 0u))
			{
				*why = "the image data comes before the palette (PLTE)";
				free(idat);
				return RLDPNG_BAD;
			}

			if ((size_t)length > (idatMax - idatSize))
			{
				*why = "the image data is far larger than the image needs - the file is damaged";
				free(idat);
				return RLDPNG_BAD;
			}

			// All IDAT together: at most idatMax bytes, never more than the file.
			if (idat == NULL)
			{
				idat = (u8 *)malloc(idatMax + 1u);
				if (idat == NULL)
				{
					*why = "out of memory";
					return RLDPNG_MEMORY;
				}
			}
			memcpy(&idat[idatSize], body, length);
			idatSize += length;
			seenData = 1;
		}
		else if (memcmp(type, "IEND", 4u) == 0)
		{
			seenEnd = 1;
		}
		else if ((type[0] & 0x20u) == 0u)
		{
			// Upper case first letter: critical - a reader that does not know it must stop.
			snprintf(text, sizeof(text), "it uses the chunk %.4s, which rldpack does not read", (const char *)type);
			*why = text;
			free(idat);
			return RLDPNG_BAD;
		}

		at += 12u + (size_t)length;
	}

	if (!seenData)
	{
		*why = "it has no image data (IDAT)";
		free(idat);
		return RLDPNG_BAD;
	}

	// Deflate gives at most 258 bytes per two bits, 1032 per byte: data that
	// cannot fill the image is refused before the image's memory is taken.
	if ((u64)rawSize > ((u64)idatSize * 1032u))
	{
		*why = "the image data is far too short for the image - the file is cut off or damaged";
		free(idat);
		return RLDPNG_BAD;
	}

	raw = (u8 *)malloc(rawSize);
	out->rgba = (u8 *)malloc((size_t)out->width * (size_t)out->height * 4u);
	if ((raw == NULL) || (out->rgba == NULL))
	{
		free(idat);
		free(raw);
		RldPng_Free(out);
		*why = "out of memory";
		return RLDPNG_MEMORY;
	}

	bad = RldPng_Inflate(idat, idatSize, raw, rawSize);
	free(idat);
	if (bad == NULL)
	{
		const size_t bitsPerPixel = (size_t)channels * out->bitDepth;
		size_t passAt = 0;

		// Each pass has filters of its own: its first row has no row above.
		for (pass = 0; (bad == NULL) && (pass < passes); pass++)
		{
			u32 w;
			u32 h;
			size_t rowBytes;

			(void)RldPng_Pass(out, pass, &w, &h);
			if ((w == 0u) || (h == 0u))
			{
				continue;
			}
			rowBytes = ((size_t)w * bitsPerPixel + 7u) / 8u;
			bad = RldPng_Unfilter(&raw[passAt], h, rowBytes, (bitsPerPixel >= 8u) ? (bitsPerPixel / 8u) : 1u);
			passAt += (rowBytes + 1u) * h;
		}
	}
	if (bad != NULL)
	{
		free(raw);
		RldPng_Free(out);
		*why = bad;
		return RLDPNG_BAD;
	}

	// The pixels of each pass onto their places.
	for (pass = 0; pass < passes; pass++)
	{
		u32 w;
		u32 h;
		const u8 *g = RldPng_Pass(out, pass, &w, &h);
		const size_t rowBytes = ((size_t)w * channels * out->bitDepth + 7u) / 8u;

		if ((w == 0u) || (h == 0u))
		{
			continue;
		}

		for (y = 0; y < h; y++)
		{
			const u8 *row = &raw[rawAt + (size_t)y * (rowBytes + 1u) + 1u];
			const size_t outY = (size_t)g[1] + (size_t)y * g[3];

			for (x = 0; x < w; x++)
			{
				u8 *pixel = &out->rgba[(outY * out->width + (size_t)g[0] + (size_t)x * g[2]) * 4u];
				const u32 depth = out->bitDepth;
				u32 s[4];
				u32 c;

				for (c = 0; c < channels; c++)
				{
					s[c] = RldPng_Sample(row, x * channels + c, depth);
				}

				switch (out->colorType)
				{
				case 0:
					pixel[0] = pixel[1] = pixel[2] = RldPng_To8(s[0], depth);
					pixel[3] = (hasTrnsKey && (s[0] == trnsKey[0])) ? 0u : 255u;
					break;

				case 2:
					pixel[0] = RldPng_To8(s[0], depth);
					pixel[1] = RldPng_To8(s[1], depth);
					pixel[2] = RldPng_To8(s[2], depth);
					pixel[3] = (hasTrnsKey && (s[0] == trnsKey[0]) && (s[1] == trnsKey[1]) && (s[2] == trnsKey[2])) ? 0u : 255u;
					break;

				case 3:
					if (s[0] >= paletteCount)
					{
						snprintf(text, sizeof(text), "a pixel uses palette entry %u, but the palette has %u", s[0], paletteCount);
						free(raw);
						RldPng_Free(out);
						*why = text;
						return RLDPNG_BAD;
					}
					pixel[0] = palette[3u * s[0] + 0u];
					pixel[1] = palette[3u * s[0] + 1u];
					pixel[2] = palette[3u * s[0] + 2u];
					pixel[3] = paletteAlpha[s[0]];
					break;

				case 4:
					pixel[0] = pixel[1] = pixel[2] = RldPng_To8(s[0], depth);
					pixel[3] = RldPng_To8(s[1], depth);
					break;

				default:
					pixel[0] = RldPng_To8(s[0], depth);
					pixel[1] = RldPng_To8(s[1], depth);
					pixel[2] = RldPng_To8(s[2], depth);
					pixel[3] = RldPng_To8(s[3], depth);
					break;
				}
			}
		}
		rawAt += (rowBytes + 1u) * h;
	}

	free(raw);
	return RLDPNG_OK;
}

#endif // RLDPNG_INC
