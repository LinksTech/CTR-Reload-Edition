#include <common.h>

// The banner of the main-menu logo: ORIGINAL or REFUELED.
//
// The red banner under the CTR letters is its own model (STATIC_BANNER, born by
// MM_Title_Init with the five other title models) and its lettering is a
// texture: six strips of 64x24 texels at 4 bits, each with a CLUT of its own,
// loaded with the main menu's VRAM file. No other model and no icon of the
// main menu samples those texels or those CLUTs, so rewriting the six texel
// rectangles changes the banner and nothing else on screen.
//
// WHAT IS WRITTEN. Only texel indices. The CLUTs stay as they are: the
// replacement (MM_MenuLogo_Banner.inc, written by tools/refueled_banner.py) was
// quantised to the original sixteen colours of each strip, so the banner keeps
// its own red, white and outline colours.
//
// WHERE. Not from a table of VRAM coordinates. Each load of the main menu asks
// the banner model's own texture layouts where its strips are - six groups of
// one tpage and one CLUT each, in the order of their first texture index - and
// checks that each is the 64x24 4-bit strip the replacement was made for, and
// that the six CLUTs hold the colours it was quantised to (MM_MenuLogo_ClutHash).
// A banner of another shape or other colours (other game data) is left alone and
// said so in the log.
//
// WHEN. On every load of the main menu level (LOAD_TenStages, stage 7), after
// the VRAM file is in and the models are stored. That load is the one moment
// the six rectangles certainly hold the original texels, so it is where they are
// saved. Any other level forgets them: its VRAM is not the menu's, and a switch
// pressed in a race must not write into a race's textures.
//
// THE SWITCH. Debug menu, page VIDEO, row MENU LOGO. Pressed in the main menu it
// writes the other variant at once; pressed anywhere else it is only remembered
// and applied by the next main menu load. It is not saved: every start shows the
// original until the rename is official.

#include "MM_MenuLogo_Banner.inc"

enum
{
	MM_MENU_LOGO_STRIPS = 6,
	MM_MENU_LOGO_STRIP_TEXELS_W = 64,
	MM_MENU_LOGO_STRIP_TEXELS_H = 24,
	MM_MENU_LOGO_STRIP_HALFWORDS_W = MM_MENU_LOGO_STRIP_TEXELS_W / 4, // 4-bit: four texels per halfword
	MM_MENU_LOGO_STRIP_HALFWORDS = MM_MENU_LOGO_STRIP_HALFWORDS_W * MM_MENU_LOGO_STRIP_TEXELS_H,
	MM_MENU_LOGO_COMMANDS_MAX = 4096,
	MM_MENU_LOGO_TEX_INDEX_MAX = 0x1ff,
};

CTR_STATIC_ASSERT(sizeof(s_menuLogoRefueled) == MM_MENU_LOGO_STRIPS * MM_MENU_LOGO_STRIP_HALFWORDS * sizeof(u16));

global_variable int s_menuLogoChoice = MM_MENU_LOGO_ORIGINAL;

// 1 while the six rectangles of the loaded main menu are known and saved.
global_variable int s_menuLogoReady = 0;
global_variable RECT16 s_menuLogoRect[MM_MENU_LOGO_STRIPS];
global_variable u16 s_menuLogoOriginal[MM_MENU_LOGO_STRIPS][MM_MENU_LOGO_STRIP_HALFWORDS];

internal struct Model *MM_MenuLogo_FindBanner(struct Level *lev)
{
	if ((lev == NULL) || (lev->ptrModelsPtrArray == NULL))
	{
		return NULL;
	}

	for (u32 i = 0; i < lev->numModels; i++)
	{
		struct Model *m = lev->ptrModelsPtrArray[i];

		if (m == NULL)
		{
			break;
		}
		if (m->id == STATIC_BANNER)
		{
			return m;
		}
	}

	return NULL;
}

// The six strips of the banner model, as VRAM rectangles, in the order of their
// first texture index. 0 with the reason in *why when the model is not the
// shape the replacement was made for.
internal int MM_MenuLogo_FindStrips(const struct Model *banner, RECT16 rects[MM_MENU_LOGO_STRIPS], u16 cluts[MM_MENU_LOGO_STRIPS], const char **why)
{
	struct
	{
		u16 tpage;
		u16 clut;
		int uMin, uMax, vMin, vMax;
	} group[MM_MENU_LOGO_STRIPS];
	int groups = 0;

	if ((banner->numHeaders < 1) || (banner->headers == NULL))
	{
		*why = "the banner model has no header";
		return 0;
	}

	const struct ModelHeader *h = &banner->headers[0];
	const u32 *cmd = (const u32 *)h->ptrCommandList;

	if ((cmd == NULL) || (h->ptrTexLayout == NULL))
	{
		*why = "the banner model has no command list or no texture layouts";
		return 0;
	}

	// Which texture indices the draw commands use. The command list starts
	// after its first word and ends at 0xFFFFFFFF; a draw command has bits in
	// its upper half and carries the 1-based layout index in its low nine bits
	// (RenderBucket_GetCommandTexture, include/rldkit.inc:806-838).
	u8 used[MM_MENU_LOGO_TEX_INDEX_MAX + 1];
	int maxIndex = 0;

	memset(used, 0, sizeof(used));
	for (int i = 1;; i++)
	{
		if (i > MM_MENU_LOGO_COMMANDS_MAX)
		{
			*why = "the banner's command list does not end";
			return 0;
		}

		const u32 c = cmd[i];

		if (c == 0xffffffffu)
		{
			break;
		}
		if ((c >> 16) != 0u)
		{
			const int ti = (int)(c & MM_MENU_LOGO_TEX_INDEX_MAX);

			used[ti] = 1;
			if (ti > maxIndex)
			{
				maxIndex = ti;
			}
		}
	}

	for (int ti = 1; ti <= maxIndex; ti++)
	{
		if (!used[ti])
		{
			continue;
		}

		const struct TextureLayout *t = h->ptrTexLayout[ti - 1];

		if (t == NULL)
		{
			*why = "a texture layout of the banner is missing";
			return 0;
		}

		int g = 0;

		while ((g < groups) && ((group[g].tpage != t->tpage) || (group[g].clut != t->clut)))
		{
			g++;
		}
		if (g == groups)
		{
			if (groups == MM_MENU_LOGO_STRIPS)
			{
				*why = "the banner uses more than six tpage/CLUT pairs";
				return 0;
			}
			group[g].tpage = t->tpage;
			group[g].clut = t->clut;
			group[g].uMin = group[g].vMin = 255;
			group[g].uMax = group[g].vMax = 0;
			groups++;
		}

		const u8 us[4] = {t->u0, t->u1, t->u2, t->u3};
		const u8 vs[4] = {t->v0, t->v1, t->v2, t->v3};

		for (int k = 0; k < 4; k++)
		{
			group[g].uMin = (us[k] < group[g].uMin) ? us[k] : group[g].uMin;
			group[g].uMax = (us[k] > group[g].uMax) ? us[k] : group[g].uMax;
			group[g].vMin = (vs[k] < group[g].vMin) ? vs[k] : group[g].vMin;
			group[g].vMax = (vs[k] > group[g].vMax) ? vs[k] : group[g].vMax;
		}
	}

	if (groups != MM_MENU_LOGO_STRIPS)
	{
		*why = "the banner does not use six tpage/CLUT pairs";
		return 0;
	}

	for (int g = 0; g < groups; g++)
	{
		// tpage: x in 64-halfword steps (bits 0-3), y in 256-row steps (bit 4),
		// colour depth in bits 7-8 (0 = 4 bit).
		const int pageX = (group[g].tpage & 0xf) * 64;
		const int pageY = ((group[g].tpage >> 4) & 1) * 256;
		const int depth = (group[g].tpage >> 7) & 3;
		const int w = group[g].uMax - group[g].uMin + 1;
		const int hgt = group[g].vMax - group[g].vMin + 1;

		if ((depth != 0) || (w != MM_MENU_LOGO_STRIP_TEXELS_W) || (hgt != MM_MENU_LOGO_STRIP_TEXELS_H) || ((group[g].uMin & 3) != 0))
		{
			*why = "a banner strip is not 64x24 texels at 4 bits";
			return 0;
		}

		rects[g].x = (s16)(pageX + group[g].uMin / 4);
		rects[g].y = (s16)(pageY + group[g].vMin);
		rects[g].w = MM_MENU_LOGO_STRIP_HALFWORDS_W;
		rects[g].h = MM_MENU_LOGO_STRIP_TEXELS_H;
		cluts[g] = group[g].clut;
	}

	return 1;
}

// FNV-1a over the six CLUTs as they are in VRAM, sixteen halfwords each, low
// byte first, in strip order. The replacement holds indices into exactly these
// colours; other game data with other colours would get a picture in the wrong
// ones, so it is compared with the hash the generator wrote and refused if it
// differs.
internal u32 MM_MenuLogo_ClutHash(const u16 cluts[MM_MENU_LOGO_STRIPS])
{
	u32 hash = 0x811c9dc5u;

	for (int s = 0; s < MM_MENU_LOGO_STRIPS; s++)
	{
		RECT16 r;
		u16 colours[16];

		r.x = (s16)((cluts[s] & 0x3f) * 16);
		r.y = (s16)((cluts[s] >> 6) & 0x1ff);
		r.w = 16;
		r.h = 1;
		StoreImage(&r, (u32 *)colours);

		for (int c = 0; c < 16; c++)
		{
			const u8 bytes[2] = {(u8)(colours[c] & 0xff), (u8)(colours[c] >> 8)};

			for (int b = 0; b < 2; b++)
			{
				hash ^= bytes[b];
				hash *= 0x01000193u;
			}
		}
	}

	return hash;
}

internal void MM_MenuLogo_Write(void)
{
	for (int s = 0; s < MM_MENU_LOGO_STRIPS; s++)
	{
		const u16 *src = (s_menuLogoChoice == MM_MENU_LOGO_REFUELED) ? s_menuLogoRefueled[s] : s_menuLogoOriginal[s];

		LoadImage(&s_menuLogoRect[s], (void *)src);
	}
}

// LOAD_TenStages, stage 7, after LibraryOfModels_Store - every level load.
void MM_MenuLogo_LevelLoaded(struct GameTracker *gGT, struct Level *lev)
{
	s_menuLogoReady = 0;

	if (gGT->levelID != MAIN_MENU_LEVEL)
	{
		return;
	}

	const struct Model *banner = MM_MenuLogo_FindBanner(lev);
	const char *why = NULL;
	u16 cluts[MM_MENU_LOGO_STRIPS];

	if (banner == NULL)
	{
		Platform_Log("[CTR Menu] menu logo: no banner model in the main menu - the switch does nothing\n");
		return;
	}
	if (!MM_MenuLogo_FindStrips(banner, s_menuLogoRect, cluts, &why))
	{
		Platform_Log("[CTR Menu] menu logo: %s - the switch does nothing\n", why);
		return;
	}

	const u32 clutHash = MM_MenuLogo_ClutHash(cluts);

	if (clutHash != s_menuLogoClutHash)
	{
		Platform_Log("[CTR Menu] menu logo: the banner's colours are not the ones the replacement was made for (%08x, expected %08x) - the switch does nothing\n",
		             clutHash, s_menuLogoClutHash);
		return;
	}

	for (int s = 0; s < MM_MENU_LOGO_STRIPS; s++)
	{
		StoreImage(&s_menuLogoRect[s], (u32 *)s_menuLogoOriginal[s]);
	}
	s_menuLogoReady = 1;

	Platform_Log("[CTR Menu] menu logo: banner strips at (%d,%d) (%d,%d) (%d,%d) (%d,%d) (%d,%d) (%d,%d), saved - %s\n", s_menuLogoRect[0].x, s_menuLogoRect[0].y,
	             s_menuLogoRect[1].x, s_menuLogoRect[1].y, s_menuLogoRect[2].x, s_menuLogoRect[2].y, s_menuLogoRect[3].x, s_menuLogoRect[3].y,
	             s_menuLogoRect[4].x, s_menuLogoRect[4].y, s_menuLogoRect[5].x, s_menuLogoRect[5].y, MM_MenuLogo_Name());

	// The original is what the file just put there; only the other one needs
	// writing.
	if (s_menuLogoChoice != MM_MENU_LOGO_ORIGINAL)
	{
		MM_MenuLogo_Write();
	}
}

void MM_MenuLogo_Toggle(void)
{
	s_menuLogoChoice = (s_menuLogoChoice == MM_MENU_LOGO_ORIGINAL) ? MM_MENU_LOGO_REFUELED : MM_MENU_LOGO_ORIGINAL;

	if (s_menuLogoReady)
	{
		MM_MenuLogo_Write();
	}

	Platform_Log("[CTR Debug] menu logo %s - %s\n", MM_MenuLogo_Name(), s_menuLogoReady ? "written" : "applied at the next main menu load");
}

const char *MM_MenuLogo_Name(void)
{
	return (s_menuLogoChoice == MM_MENU_LOGO_REFUELED) ? "REFUELED" : "ORIGINAL";
}
