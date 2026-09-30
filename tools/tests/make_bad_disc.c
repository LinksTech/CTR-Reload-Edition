// make_bad_disc - made-up mini disc images for ctr_native --dev --selftest-disc.
//
//   ctr_make_bad_disc <folder>
//
// Writes good-*.bin and bad-*.bin into <folder>. Every image is generated here,
// byte by byte: a few kilobytes of raw MODE2/2352 sectors with an ISO 9660
// volume, a SYSTEM.CNF naming SCUS_944.26 and tiny text files. No game data,
// nothing read from anywhere.
//
// WHAT THE READER NEEDS (platform/native_disc_image.c), AND ONLY THAT:
//   - raw 2352-byte sectors from byte 0, each with the sync pattern and mode 2
//     (no .cue is read);
//   - the primary volume descriptor at sector 16 ("CD001", version 1) with the
//     root directory record at byte 156;
//   - Form 1 payload at byte 24 of a sector; a file is Form 2 when bit 5 of the
//     submode byte (byte 18) of its first sector is set;
//   - SYSTEM.CNF in the root, whose text contains the serial SCUS_944.26.
// EDC/ECC are not checked by the reader and stay zero.
//
// good-* images must be extracted, bad-* must be refused - each bad image
// breaks exactly one thing, named in the table at the bottom. The payload that
// tries to escape is always called SELFTEST_ESCAPED.TXT, so a broken unpacker
// leaves a file with that name beside the assets folder or in <folder>, where
// the self-test's listing finds it.
//
// Plain C, standard library only: it builds without any header of the game.

// fopen is the portable call; MSVC would ask for fopen_s.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define SECTOR_BYTES     2352u
#define DATA_OFFSET      24u
#define DATA_BYTES       2048u
#define USER_OFFSET      16u
#define USER_BYTES       2336u
#define PVD_LBA          16u
#define FIRST_FREE_LBA   18u
#define MAX_SECTORS      512u
#define MAX_NODES        512
#define MAX_KIDS         96
#define SUBMODE_DATA     0x08u
#define SUBMODE_FORM2    0x20u
#define DIRECTORY_FLAG   0x02u

#define NAME(s) (s), (unsigned)(sizeof(s) - 1u)

typedef struct Node Node;

struct Node
{
	char name[256];
	unsigned nameLen;
	int isDir;

	const char *text;   // Form 1 file contents
	unsigned form2Sectors;

	Node *kids[MAX_KIDS];
	int kidCount;
	Node *parent;

	// The record points at this node's extent instead of one of its own: the
	// loop records. Not laid out, only referenced.
	Node *alias;

	// Nonzero: the record claims this size instead of the real one.
	unsigned claimSize;

	// Nonzero: the record's length byte is written as this (a broken record).
	unsigned brokenLength;

	unsigned lba;
	unsigned size;
};

static Node s_nodes[MAX_NODES];
static int s_nodeCount;
static unsigned char s_image[MAX_SECTORS * SECTOR_BYTES];
static unsigned s_sectorCount;

static const char s_escaped[] = "written by make_bad_disc - if this file is outside an assets folder, the unpacker let a name escape\n";

static Node *NewNode(Node *parent, const char *name, unsigned nameLen, int isDir)
{
	Node *node;

	if ((s_nodeCount >= MAX_NODES) || (nameLen > 200u) || ((parent != NULL) && (parent->kidCount >= MAX_KIDS)))
	{
		fprintf(stderr, "make_bad_disc: image description too large\n");
		exit(1);
	}

	node = &s_nodes[s_nodeCount++];
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, nameLen);
	node->nameLen = nameLen;
	node->isDir = isDir;
	node->parent = parent;

	if (parent != NULL)
	{
		parent->kids[parent->kidCount++] = node;
	}

	return node;
}

static Node *Dir(Node *parent, const char *name, unsigned nameLen)
{
	return NewNode(parent, name, nameLen, 1);
}

static Node *File(Node *parent, const char *name, unsigned nameLen, const char *text)
{
	Node *node = NewNode(parent, name, nameLen, 0);

	node->text = text;
	return node;
}

static Node *Form2File(Node *parent, const char *name, unsigned nameLen, unsigned sectors)
{
	Node *node = NewNode(parent, name, nameLen, 0);

	node->form2Sectors = sectors;
	return node;
}

// A directory record that points at an existing directory.
static Node *Alias(Node *parent, const char *name, unsigned nameLen, Node *target)
{
	Node *node = NewNode(parent, name, nameLen, 1);

	node->alias = target;
	return node;
}

static unsigned RecordLength(unsigned nameLen)
{
	return 33u + nameLen + (((nameLen & 1u) == 0u) ? 1u : 0u);
}

static unsigned DirectoryBytes(const Node *dir)
{
	unsigned at = 0;
	int i;

	for (i = -2; i < dir->kidCount; i++)
	{
		unsigned length = RecordLength((i < 0) ? 1u : dir->kids[i]->nameLen);

		if (((at % DATA_BYTES) + length) > DATA_BYTES)
		{
			at = ((at / DATA_BYTES) + 1u) * DATA_BYTES;
		}

		at += length;
	}

	return ((at + DATA_BYTES - 1u) / DATA_BYTES) * DATA_BYTES;
}

static void Layout(Node *node, unsigned *next)
{
	int i;

	if (node->alias != NULL)
	{
		return;
	}

	node->lba = *next;

	if (node->isDir)
	{
		node->size = DirectoryBytes(node);
		*next += node->size / DATA_BYTES;

		for (i = 0; i < node->kidCount; i++)
		{
			Layout(node->kids[i], next);
		}
	}
	else if (node->form2Sectors != 0)
	{
		node->size = node->form2Sectors * DATA_BYTES;
		*next += node->form2Sectors;
	}
	else
	{
		node->size = (unsigned)strlen(node->text);
		*next += (node->size + DATA_BYTES - 1u) / DATA_BYTES;
	}

	if (*next > MAX_SECTORS)
	{
		fprintf(stderr, "make_bad_disc: image larger than %u sectors\n", MAX_SECTORS);
		exit(1);
	}
}

static unsigned char *Sector(unsigned lba)
{
	return &s_image[(size_t)lba * SECTOR_BYTES];
}

static void Le32(unsigned char *at, unsigned value)
{
	at[0] = (unsigned char)(value & 0xffu);
	at[1] = (unsigned char)((value >> 8) & 0xffu);
	at[2] = (unsigned char)((value >> 16) & 0xffu);
	at[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void Be32(unsigned char *at, unsigned value)
{
	at[0] = (unsigned char)((value >> 24) & 0xffu);
	at[1] = (unsigned char)((value >> 16) & 0xffu);
	at[2] = (unsigned char)((value >> 8) & 0xffu);
	at[3] = (unsigned char)(value & 0xffu);
}

static unsigned char Bcd(unsigned value)
{
	return (unsigned char)(((value / 10u) << 4) | (value % 10u));
}

// Sync, header, mode 2, and a subheader - on every sector of the image.
static void FrameSectors(void)
{
	unsigned lba;

	for (lba = 0; lba < s_sectorCount; lba++)
	{
		unsigned char *sector = Sector(lba);
		unsigned address = lba + 150u;

		sector[0] = 0x00;
		memset(&sector[1], 0xff, 10);
		sector[11] = 0x00;
		sector[12] = Bcd(address / (60u * 75u));
		sector[13] = Bcd((address / 75u) % 60u);
		sector[14] = Bcd(address % 75u);
		sector[15] = 0x02;

		if ((sector[18] & SUBMODE_FORM2) == 0)
		{
			sector[18] = SUBMODE_DATA;
			sector[22] = SUBMODE_DATA;
		}
	}
}

static void PutRecord(unsigned char *at, unsigned lba, unsigned size, int isDir, const char *name, unsigned nameLen, unsigned brokenLength)
{
	unsigned length = RecordLength(nameLen);

	at[0] = (unsigned char)((brokenLength != 0) ? brokenLength : length);
	Le32(&at[2], lba);
	Be32(&at[6], lba);
	Le32(&at[10], size);
	Be32(&at[14], size);
	at[18] = 99; // 1999-10-01
	at[19] = 10;
	at[20] = 1;
	at[25] = (unsigned char)(isDir ? DIRECTORY_FLAG : 0u);
	at[28] = 1;
	at[31] = 1;
	at[32] = (unsigned char)nameLen;
	memcpy(&at[33], name, nameLen);
}

static void WriteData(unsigned lba, const unsigned char *data, unsigned size)
{
	unsigned done = 0;

	while (done < size)
	{
		unsigned chunk = ((size - done) < DATA_BYTES) ? (size - done) : DATA_BYTES;

		memcpy(&Sector(lba)[DATA_OFFSET], &data[done], chunk);
		done += chunk;
		lba++;
	}
}

static void WriteNode(const Node *node)
{
	int i;

	if (node->alias != NULL)
	{
		return;
	}

	if (node->isDir)
	{
		unsigned char buffer[16u * DATA_BYTES];
		unsigned at = 0;
		const Node *parent = (node->parent != NULL) ? node->parent : node;

		if (node->size > sizeof(buffer))
		{
			fprintf(stderr, "make_bad_disc: directory too large\n");
			exit(1);
		}

		memset(buffer, 0, sizeof(buffer));

		for (i = -2; i < node->kidCount; i++)
		{
			const Node *kid = (i < 0) ? NULL : node->kids[i];
			const Node *target = (kid == NULL) ? ((i == -2) ? node : parent) : ((kid->alias != NULL) ? kid->alias : kid);
			const char self = (i == -2) ? '\0' : '\1';
			unsigned nameLen = (kid == NULL) ? 1u : kid->nameLen;
			unsigned length = RecordLength(nameLen);
			unsigned size = ((kid != NULL) && (kid->claimSize != 0)) ? kid->claimSize : target->size;

			if (((at % DATA_BYTES) + length) > DATA_BYTES)
			{
				at = ((at / DATA_BYTES) + 1u) * DATA_BYTES;
			}

			PutRecord(&buffer[at], target->lba, size, (kid == NULL) ? 1 : kid->isDir, (kid == NULL) ? &self : kid->name, nameLen,
			          (kid != NULL) ? kid->brokenLength : 0u);
			at += length;
		}

		WriteData(node->lba, buffer, node->size);

		for (i = 0; i < node->kidCount; i++)
		{
			WriteNode(node->kids[i]);
		}
	}
	else if (node->form2Sectors != 0)
	{
		unsigned s;

		for (s = 0; s < node->form2Sectors; s++)
		{
			unsigned char *sector = Sector(node->lba + s);

			sector[USER_OFFSET + 2u] = SUBMODE_FORM2;
			sector[USER_OFFSET + 6u] = SUBMODE_FORM2;
			memset(&sector[DATA_OFFSET], (int)('a' + (s % 26u)), USER_BYTES - 8u);
		}
	}
	else
	{
		WriteData(node->lba, (const unsigned char *)node->text, node->size);
	}
}

static void WriteVolume(const Node *root)
{
	unsigned char *pvd = &Sector(PVD_LBA)[DATA_OFFSET];
	unsigned char *end = &Sector(PVD_LBA + 1u)[DATA_OFFSET];

	pvd[0] = 1;
	memcpy(&pvd[1], "CD001", 5);
	pvd[6] = 1;
	memset(&pvd[8], ' ', 32);
	memcpy(&pvd[8], "PLAYSTATION", 11);
	memset(&pvd[40], ' ', 32);
	memcpy(&pvd[40], "SELFTEST", 8);
	Le32(&pvd[80], s_sectorCount);
	Be32(&pvd[84], s_sectorCount);
	pvd[128] = 0x00; // logical block size 2048, both byte orders
	pvd[129] = 0x08;
	pvd[130] = 0x08;
	pvd[131] = 0x00;
	PutRecord(&pvd[156], root->lba, root->size, 1, "", 1, 0);

	end[0] = 0xff;
	memcpy(&end[1], "CD001", 5);
	end[6] = 1;
}

static const char s_systemCnf[] = "BOOT = cdrom:\\SCUS_944.26;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n";
static const char s_wrongCnf[] = "BOOT = cdrom:\\SLES_021.05;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n";
static const char s_dummyExe[] = "not an executable - a made-up file of the disc self-test\n";
static const char s_note[] = "a made-up file of the disc self-test\n";

static Node *NewRoot(int withBoot)
{
	Node *root;

	s_nodeCount = 0;
	root = Dir(NULL, "", 0);

	if (withBoot)
	{
		File(root, NAME("SYSTEM.CNF;1"), s_systemCnf);
		File(root, NAME("SCUS_944.26;1"), s_dummyExe);
	}

	return root;
}

// Lays out, frames and writes one image. cutSectors != 0 keeps only the first
// cutSectors sectors (a truncated download).
static int WriteImage(const char *folder, const char *name, Node *root, unsigned cutSectors)
{
	char path[1024];
	unsigned next = FIRST_FREE_LBA;
	size_t bytes;
	FILE *out;

	Layout(root, &next);
	s_sectorCount = next;
	memset(s_image, 0, sizeof(s_image));
	WriteNode(root);
	WriteVolume(root);
	FrameSectors();

	bytes = (size_t)(((cutSectors != 0) && (cutSectors < s_sectorCount)) ? cutSectors : s_sectorCount) * SECTOR_BYTES;

	if (snprintf(path, sizeof(path), "%s/%s.bin", folder, name) >= (int)sizeof(path))
	{
		fprintf(stderr, "make_bad_disc: path too long\n");
		return 0;
	}

	out = fopen(path, "wb");
	if ((out == NULL) || (fwrite(s_image, 1, bytes, out) != bytes) || (fclose(out) != 0))
	{
		fprintf(stderr, "make_bad_disc: cannot write %s\n", path);
		return 0;
	}

	printf("make_bad_disc: %s.bin (%u sectors)\n", name, (unsigned)(bytes / SECTOR_BYTES));
	return 1;
}

int main(int argc, char **argv)
{
	const char *folder;
	Node *root;
	Node *a;
	Node *b;
	char name[32];
	int ok = 1;
	int i;

	if (argc != 2)
	{
		fprintf(stderr, "usage: ctr_make_bad_disc <folder>\n");
		return 2;
	}

	folder = argv[1];

	// The folder itself, if it is not there yet (one level; an existing one is
	// fine). Old images in it are overwritten.
#if defined(_WIN32)
	(void)_mkdir(folder);
#else
	(void)mkdir(folder, 0777);
#endif

	// good-basic: Form 1 files, an empty file, a Form 2 file, two directories.
	root = NewRoot(1);
	File(root, NAME("README.TXT;1"), s_note);
	File(root, NAME("EMPTY.TXT;1"), "");
	a = Dir(root, NAME("DATA"));
	File(a, NAME("NOTE.TXT;1"), s_note);
	a = Dir(root, NAME("XA"));
	Form2File(a, NAME("MUSIC.XA;1"), 2);
	ok &= WriteImage(folder, "good-basic", root, 0);

	// good-nested: three levels, and a directory of 60 entries that spans two
	// sectors (records never straddle one; the reader skips the padding).
	root = NewRoot(1);
	a = Dir(root, NAME("A"));
	b = Dir(a, NAME("B"));
	b = Dir(b, NAME("C"));
	File(b, NAME("DEEP.TXT;1"), s_note);
	a = Dir(root, NAME("MANY"));
	for (i = 0; i < 60; i++)
	{
		snprintf(name, sizeof(name), "FILE%02d.TXT;1", i);
		File(a, name, (unsigned)strlen(name), s_note);
	}
	ok &= WriteImage(folder, "good-nested", root, 0);

	// bad-dotdot: a directory named ".." (the two characters, not the 0x01
	// parent record) - its file would land beside the assets folder.
	root = NewRoot(1);
	a = Dir(root, NAME(".."));
	File(a, NAME("SELFTEST_ESCAPED.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-dotdot", root, 0);

	// bad-dotdot-deep: "../.." - its file would land in <folder> itself.
	root = NewRoot(1);
	a = Dir(root, NAME(".."));
	a = Dir(a, NAME(".."));
	File(a, NAME("SELFTEST_ESCAPED.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-dotdot-deep", root, 0);

	// bad-dotdot-file: a file named "..;1".
	root = NewRoot(1);
	File(root, NAME("..;1"), s_escaped);
	ok &= WriteImage(folder, "bad-dotdot-file", root, 0);

	// bad-dot: a directory named ".".
	root = NewRoot(1);
	a = Dir(root, NAME("."));
	File(a, NAME("FILE.TXT;1"), s_note);
	ok &= WriteImage(folder, "bad-dot", root, 0);

	// bad-dots: "..." - Windows folds trailing dots away.
	root = NewRoot(1);
	a = Dir(root, NAME("..."));
	File(a, NAME("FILE.TXT;1"), s_note);
	ok &= WriteImage(folder, "bad-dots", root, 0);

	// bad-backslash: "\x" in the root.
	root = NewRoot(1);
	File(root, NAME("\\x"), s_escaped);
	ok &= WriteImage(folder, "bad-backslash", root, 0);

	// bad-backslash-escape: "..\..\SELFTEST_ESCAPED.TXT" one level down.
	root = NewRoot(1);
	a = Dir(root, NAME("D"));
	File(a, NAME("..\\..\\SELFTEST_ESCAPED.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-backslash-escape", root, 0);

	// bad-slash: "a/.." in the root.
	root = NewRoot(1);
	File(root, NAME("a/.."), s_escaped);
	ok &= WriteImage(folder, "bad-slash", root, 0);

	// bad-slash-escape: "../../SELFTEST_ESCAPED.TXT" one level down.
	root = NewRoot(1);
	a = Dir(root, NAME("D"));
	File(a, NAME("../../SELFTEST_ESCAPED.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-slash-escape", root, 0);

	// bad-drive: "C:x" - a drive-relative path, or a stream on Windows.
	root = NewRoot(1);
	File(root, NAME("C:x"), s_escaped);
	ok &= WriteImage(folder, "bad-drive", root, 0);

	// bad-absolute-drive: "C:\SELFTEST_ESCAPED.TXT".
	root = NewRoot(1);
	File(root, NAME("C:\\SELFTEST_ESCAPED.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-absolute-drive", root, 0);

	// bad-absolute-root: "/SELFTEST_ESCAPED.TXT" one level down.
	root = NewRoot(1);
	a = Dir(root, NAME("D"));
	File(a, NAME("/SELFTEST_ESCAPED.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-absolute-root", root, 0);

	// bad-absolute-unc: "\\host\share\x".
	root = NewRoot(1);
	File(root, NAME("\\\\host\\share\\x"), s_escaped);
	ok &= WriteImage(folder, "bad-absolute-unc", root, 0);

	// bad-empty: a file record with a name of length 0.
	root = NewRoot(1);
	File(root, "", 0, s_escaped);
	ok &= WriteImage(folder, "bad-empty", root, 0);

	// bad-empty-space: a directory named " " - the reader trims trailing
	// spaces, so what is left is an empty name.
	root = NewRoot(1);
	a = Dir(root, NAME(" "));
	File(a, NAME("FILE.TXT;1"), s_note);
	ok &= WriteImage(folder, "bad-empty-space", root, 0);

	// bad-nul: a NUL byte inside the name.
	root = NewRoot(1);
	File(root, NAME("A\0B.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-nul", root, 0);

	// bad-control: a control character inside the name.
	root = NewRoot(1);
	File(root, NAME("A\aB.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-control", root, 0);

	// bad-device: "NUL.TXT" is the NUL device in every Windows folder.
	root = NewRoot(1);
	File(root, NAME("NUL.TXT;1"), s_escaped);
	ok &= WriteImage(folder, "bad-device", root, 0);

	// bad-loop-root: sixteen directory records pointing back at the root. With
	// only a depth limit that is 16^9 paths - the walk would never end.
	root = NewRoot(1);
	for (i = 0; i < 16; i++)
	{
		snprintf(name, sizeof(name), "LOOP%02d", i);
		Alias(root, name, (unsigned)strlen(name), root);
	}
	ok &= WriteImage(folder, "bad-loop-root", root, 0);

	// bad-loop-parent: A/B/UP points back at A.
	root = NewRoot(1);
	a = Dir(root, NAME("A"));
	b = Dir(a, NAME("B"));
	File(b, NAME("FILE.TXT;1"), s_note);
	Alias(b, NAME("UP"), a);
	ok &= WriteImage(folder, "bad-loop-parent", root, 0);

	// bad-loop-self: A/SELF points at A itself.
	root = NewRoot(1);
	a = Dir(root, NAME("A"));
	Alias(a, NAME("SELF"), a);
	ok &= WriteImage(folder, "bad-loop-self", root, 0);

	// bad-deep: ten nested directories, deeper than any disc goes.
	root = NewRoot(1);
	a = root;
	for (i = 0; i < 10; i++)
	{
		snprintf(name, sizeof(name), "L%d", i);
		a = Dir(a, name, (unsigned)strlen(name));
	}
	File(a, NAME("FILE.TXT;1"), s_note);
	ok &= WriteImage(folder, "bad-deep", root, 0);

	// bad-huge-dir: a directory record claiming almost 2 GB.
	root = NewRoot(1);
	a = Dir(root, NAME("HUGE"));
	a->claimSize = 0x7fff0000u;
	ok &= WriteImage(folder, "bad-huge-dir", root, 0);

	// bad-broken-record: a record whose length byte is below the 34-byte
	// minimum, after the boot files (so the serial check still passes).
	root = NewRoot(1);
	a = File(root, NAME("BROKEN.TXT;1"), s_note);
	a->brokenLength = 20;
	ok &= WriteImage(folder, "bad-broken-record", root, 0);

	// bad-wrong-serial: a PAL boot record. Not a path case - the identity
	// check the first start runs before it walks anything.
	root = NewRoot(0);
	File(root, NAME("SYSTEM.CNF;1"), s_wrongCnf);
	File(root, NAME("README.TXT;1"), s_note);
	ok &= WriteImage(folder, "bad-wrong-serial", root, 0);

	// bad-no-boot: no SYSTEM.CNF at all.
	root = NewRoot(0);
	File(root, NAME("README.TXT;1"), s_note);
	ok &= WriteImage(folder, "bad-no-boot", root, 0);

	// bad-truncated: good-basic cut after the volume descriptors.
	root = NewRoot(1);
	File(root, NAME("README.TXT;1"), s_note);
	ok &= WriteImage(folder, "bad-truncated", root, FIRST_FREE_LBA);

	// bad-not-an-image: only the first sector, no volume descriptor.
	root = NewRoot(1);
	ok &= WriteImage(folder, "bad-not-an-image", root, 1);

	return ok ? 0 : 1;
}
