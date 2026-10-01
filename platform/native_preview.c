// ===========================================================================
// TRACK PREVIEW FOR CONTAINER TRACKS.
//
// The menu is itself a level and cannot load a track. The preview is
// therefore recorded beforehand - 10 s from the driver camera behind an
// invisible kart that the AI drives alone, without a driving path with the
// path camera from the respawn points (game/native_flyin.c) - and loaded in the
// track screen as a sequence of frames into the preview window, at the
// place where retail puts its STR video (MM_TrackSelect.c:186-203).
//
// STORAGE (not in the container; "vorschau" is the folder name on disk):
//   tracks\vorschau\<container without .rldtrack>.rldprev
//   0x00  "RLDPREV1"
//   0x08  u32 version 1
//   0x0C  u16 width 170, u16 height 71, u16 frames 150, u16 frames/s 15
//   0x14  32 bytes SHA-256 of the raw LEVD bytes (as in the container, RLD_DIR_HASH)
//   0x34  frames, each 170 x 71 u16 RGB555 (PSX: r | g << 5 | b << 10)
// If the hash no longer matches the container, the preview is invalid.
//
// MEMORY. One frame 24,140 bytes, the file 3,621,052 bytes. Playback
// reads one frame from the file per frame shown (one buffer, 24 KB). The
// recording holds all 150 frames (3.6 MB) and writes them at the end in
// one go.
//
// BLACK. 0x0000 is transparent in a 16-bit texture; a black
// preview pixel therefore becomes 0x0421 (1/31 per channel).
//
// ALWAYS A FINAL LINE. Reload Studio (recording by itself after "Build
// container") reads exactly one line:
// "[CTR Preview] written <path> (<n> frames)" or
// "[CTR Preview] FAILED: <reason>". A manual test once ended twice
// without either: the window was closed (SDL_EVENT_WINDOW_CLOSE_REQUESTED,
// exit(0) in native_platform.c, without a log line of its own), once after about 130
// of 150 frames. Therefore:
//   - a final report (Platform_AtExitReport) writes the FAILED line with the state
//     of the recording when the game ends without a result;
//   - a watchdog ends the recording with FAILED if it has not begun after 60 s
//     (track not loaded, no race) or no frame came
//     for 30 s (pause);
//   - every 30 frames a progress line, and the window title says that
//     the window closes by itself.
// The game window is hidden with --record-preview
// (g_cfg_windowHidden, native_renderer.c): nobody can
// close it any more. An end before the result then comes from outside (process
// killed, crash); that is why the final report no longer says "keep the window
// open". The title stays for runs by hand without Reload Studio.
// ===========================================================================

#include <platform/native_preview.h>

#define NATIVE_PREVIEW_MAGIC "RLDPREV1"
#define NATIVE_PREVIEW_VERSION 1
#define NATIVE_PREVIEW_HEADER 0x34
#define NATIVE_PREVIEW_FRAME_PIXELS (NATIVE_PREVIEW_WIDTH * NATIVE_PREVIEW_HEIGHT)

// Watchdog, in VBlanks (60 per second): until the first frame, between two frames.
#define NATIVE_PREVIEW_START_LIMIT (60 * 60)
#define NATIVE_PREVIEW_STALL_LIMIT (30 * 60)
#define NATIVE_PREVIEW_PROGRESS_STEP 30

int g_cfg_recordPreview = 0;

global_variable struct
{
	int due;       // next frame, or -1
	int captured;  // recorded frames
	u16 *frames;   // NATIVE_PREVIEW_FRAMES frames
	u8 *readback;  // main frame, BGRA
	int readbackBytes;
	int armed;       // final report registered, window title set
	int ended;       // final line (written or FAILED) already written
	int armVBlank;   // VBlank at the first frame of the game
	int lastVBlank;  // VBlank at the last recorded frame
} s_prevRec = {-1, 0, NULL, NULL, 0, 0, 0, 0, 0};

global_variable struct
{
	FILE *file;
	int trackIndex;
	u16 pixels[NATIVE_PREVIEW_FRAME_PIXELS];
} s_prevPlay = {NULL, -1};

internal void NativePreview_Put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

internal u32 NativePreview_Get16(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8);
}

void NativePreview_RecordFail(const char *why)
{
	s_prevRec.ended = 1;
	Platform_Log("[CTR Preview] FAILED: %s\n", why);
	Platform_QuitGame("--record-preview failed");
}

// Final report: the game ends with --record-preview without written or
// FAILED having been written - window closed, Ctrl+Q, QUIT in the menu.
internal void NativePreview_RecordExitReport(void)
{
	if (!g_cfg_recordPreview || s_prevRec.ended)
	{
		return;
	}
	s_prevRec.ended = 1;

	if (s_prevRec.captured > 0)
	{
		Platform_Log("[CTR Preview] FAILED: the game was closed before the preview was finished (%d of %d frames recorded) - the game was ended from "
		             "outside\n",
		             s_prevRec.captured, NATIVE_PREVIEW_FRAMES);
	}
	else
	{
		Platform_Log("[CTR Preview] FAILED: the game was closed before the recording started (at vblank %d)\n", Platform_GetVBlankCount());
	}
}

// At the first frame with --record-preview: register the final report, set the title.
internal void NativePreview_RecordArm(void)
{
	s_prevRec.armed = 1;
	s_prevRec.armVBlank = Platform_GetVBlankCount();
	Platform_AtExitReport(NativePreview_RecordExitReport);

	if (g_window != NULL)
	{
		SDL_SetWindowTitle(g_window, "CTR Reload - recording the track preview, the window closes by itself");
	}
}

// Watchdog, per frame: the recording does not begin or stands still.
internal void NativePreview_RecordWatch(void)
{
	int now = Platform_GetVBlankCount();
	char why[160];

	if (s_prevRec.ended || (s_prevRec.captured >= NATIVE_PREVIEW_FRAMES))
	{
		return;
	}

	if ((s_prevRec.captured == 0) && ((now - s_prevRec.armVBlank) > NATIVE_PREVIEW_START_LIMIT))
	{
		NativePreview_RecordFail("the recording did not start within 60 s - the track did not start as a race (see the [CTR Debug] and [CTR Tracks] "
		                         "lines in the game log)");
		return;
	}

	if ((s_prevRec.captured > 0) && ((now - s_prevRec.lastVBlank) > NATIVE_PREVIEW_STALL_LIMIT))
	{
		snprintf(why, sizeof(why), "the recording stopped for 30 s after %d of %d frames - was the game paused?", s_prevRec.captured,
		         NATIVE_PREVIEW_FRAMES);
		NativePreview_RecordFail(why);
	}
}

void NativePreview_RecordFrameDue(int frameIndex)
{
	s_prevRec.due = frameIndex;
}

internal void NativePreview_RecordWrite(void)
{
	int index = NativeTrack_LoadedIndex();
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	char path[1024];
	char dirPath[1024];
	u8 header[NATIVE_PREVIEW_HEADER];
	FILE *f;
	size_t pixelBytes = (size_t)NATIVE_PREVIEW_FRAMES * NATIVE_PREVIEW_FRAME_PIXELS * 2;

	if ((entry == NULL) || !entry->levdShaOk)
	{
		NativePreview_RecordFail("the loaded container has no LEVD hash");
		return;
	}

	if (!NativeTrack_PreviewPath(index, path, sizeof(path), dirPath, sizeof(dirPath)))
	{
		NativePreview_RecordFail("no path for the preview file");
		return;
	}

#if defined(_WIN32)
	CreateDirectoryA(dirPath, NULL);
#else
	mkdir(dirPath, 0755);
#endif

	memset(header, 0, sizeof(header));
	memcpy(header, NATIVE_PREVIEW_MAGIC, 8);
	header[8] = NATIVE_PREVIEW_VERSION;
	NativePreview_Put16(&header[0x0C], NATIVE_PREVIEW_WIDTH);
	NativePreview_Put16(&header[0x0E], NATIVE_PREVIEW_HEIGHT);
	NativePreview_Put16(&header[0x10], NATIVE_PREVIEW_FRAMES);
	NativePreview_Put16(&header[0x12], NATIVE_PREVIEW_FPS);
	memcpy(&header[0x14], entry->levdSha, 32);

	f = fopen(path, "wb");
	if ((f == NULL) || (fwrite(header, 1, sizeof(header), f) != sizeof(header)) || (fwrite(s_prevRec.frames, 1, pixelBytes, f) != pixelBytes))
	{
		if (f != NULL)
		{
			fclose(f);
		}
		NativePreview_RecordFail("cannot write the preview file");
		return;
	}
	fclose(f);

	Platform_Log("[CTR Preview] %s: %d frames %dx%d at %d/s, %d bytes\n", entry->file, NATIVE_PREVIEW_FRAMES, NATIVE_PREVIEW_WIDTH, NATIVE_PREVIEW_HEIGHT,
	             NATIVE_PREVIEW_FPS, (int)(sizeof(header) + pixelBytes));
	s_prevRec.ended = 1;
	Platform_Log("[CTR Preview] written %s (%d frames)\n", path, NATIVE_PREVIEW_FRAMES);
	Platform_QuitGame("--record-preview done");
}

// From the main frame (BGRA, bottom to top, NativeRenderer_ReadMainTarget)
// cut out the middle in the aspect ratio 170:71 and average per target pixel.
internal void NativePreview_Downsample(const u8 *src, int w, int h, u16 *dst)
{
	int cropW = w;
	int cropH = h;
	int x0, y0;
	int ox, oy;

	if ((w * NATIVE_PREVIEW_HEIGHT) > (h * NATIVE_PREVIEW_WIDTH))
	{
		cropW = (h * NATIVE_PREVIEW_WIDTH) / NATIVE_PREVIEW_HEIGHT;
	}
	else
	{
		cropH = (w * NATIVE_PREVIEW_HEIGHT) / NATIVE_PREVIEW_WIDTH;
	}
	x0 = (w - cropW) / 2;
	y0 = (h - cropH) / 2;

	for (oy = 0; oy < NATIVE_PREVIEW_HEIGHT; oy++)
	{
		int sy0 = y0 + (oy * cropH) / NATIVE_PREVIEW_HEIGHT;
		int sy1 = y0 + ((oy + 1) * cropH) / NATIVE_PREVIEW_HEIGHT;

		if (sy1 <= sy0)
		{
			sy1 = sy0 + 1;
		}

		for (ox = 0; ox < NATIVE_PREVIEW_WIDTH; ox++)
		{
			int sx0 = x0 + (ox * cropW) / NATIVE_PREVIEW_WIDTH;
			int sx1 = x0 + ((ox + 1) * cropW) / NATIVE_PREVIEW_WIDTH;
			u32 sum[3] = {0, 0, 0};
			u32 n = 0;
			int sx, sy;
			u32 r, g, b;
			u16 px;

			if (sx1 <= sx0)
			{
				sx1 = sx0 + 1;
			}

			for (sy = sy0; sy < sy1; sy++)
			{
				// Row 0 of the result is the bottom one.
				const u8 *row = src + (size_t)(h - 1 - sy) * w * 4;

				for (sx = sx0; sx < sx1; sx++)
				{
					sum[0] += row[sx * 4 + 0];
					sum[1] += row[sx * 4 + 1];
					sum[2] += row[sx * 4 + 2];
					n++;
				}
			}

			b = (sum[0] / n) >> 3;
			g = (sum[1] / n) >> 3;
			r = (sum[2] / n) >> 3;
			px = (u16)(r | (g << 5) | (b << 10));
			dst[oy * NATIVE_PREVIEW_WIDTH + ox] = (px == 0) ? 0x0421 : px;
		}
	}
}

// In Platform_EndFrame, when the frame is finished (next to --shot).
void NativePreview_EndFrame(void)
{
	int w = 0, h = 0;
	int bytes;
	int frameIndex = s_prevRec.due;

	if (g_cfg_recordPreview)
	{
		if (!s_prevRec.armed)
		{
			NativePreview_RecordArm();
		}

		if (frameIndex < 0)
		{
			NativePreview_RecordWatch();
		}
	}

	if (frameIndex < 0)
	{
		return;
	}
	s_prevRec.due = -1;

	if ((frameIndex >= NATIVE_PREVIEW_FRAMES) || (frameIndex != s_prevRec.captured))
	{
		NativePreview_RecordFail("frames out of order");
		return;
	}

	if (s_prevRec.frames == NULL)
	{
		s_prevRec.frames = (u16 *)malloc((size_t)NATIVE_PREVIEW_FRAMES * NATIVE_PREVIEW_FRAME_PIXELS * 2);
		if (s_prevRec.frames == NULL)
		{
			NativePreview_RecordFail("out of memory");
			return;
		}
	}

	NativeRenderer_GetMainTargetSize(&w, &h);
	bytes = w * h * 4;
	if ((w <= 0) || (h <= 0))
	{
		NativePreview_RecordFail("no main target");
		return;
	}

	if (bytes > s_prevRec.readbackBytes)
	{
		free(s_prevRec.readback);
		s_prevRec.readback = (u8 *)malloc((size_t)bytes);
		s_prevRec.readbackBytes = (s_prevRec.readback != NULL) ? bytes : 0;
		if (s_prevRec.readback == NULL)
		{
			NativePreview_RecordFail("out of memory");
			return;
		}
	}

	if (!NativeRenderer_ReadMainTarget(s_prevRec.readback, s_prevRec.readbackBytes, &w, &h))
	{
		NativePreview_RecordFail("the main target could not be read");
		return;
	}

	NativePreview_Downsample(s_prevRec.readback, w, h, &s_prevRec.frames[(size_t)frameIndex * NATIVE_PREVIEW_FRAME_PIXELS]);
	s_prevRec.captured++;
	s_prevRec.lastVBlank = Platform_GetVBlankCount();

	if (s_prevRec.captured == 1)
	{
		Platform_Log("[CTR Preview] recording from a %dx%d main target\n", w, h);
	}
	else if (((s_prevRec.captured % NATIVE_PREVIEW_PROGRESS_STEP) == 0) && (s_prevRec.captured < NATIVE_PREVIEW_FRAMES))
	{
		// Progress for Reload Studio (Track_PreviewLine) and the log.
		Platform_Log("[CTR Preview] %d of %d frames recorded\n", s_prevRec.captured, NATIVE_PREVIEW_FRAMES);
	}

	if (s_prevRec.captured == NATIVE_PREVIEW_FRAMES)
	{
		NativePreview_RecordWrite();
	}
}

void NativePreview_Close(void)
{
	if (s_prevPlay.file != NULL)
	{
		fclose(s_prevPlay.file);
	}
	s_prevPlay.file = NULL;
	s_prevPlay.trackIndex = -1;
}

int NativePreview_Open(int trackIndex)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(trackIndex);
	char path[1024];
	char dirPath[1024];
	u8 header[NATIVE_PREVIEW_HEADER];
	FILE *f;

	if ((s_prevPlay.file != NULL) && (s_prevPlay.trackIndex == trackIndex))
	{
		return 1;
	}

	NativePreview_Close();

	if ((entry == NULL) || !NativeTrack_PreviewPath(trackIndex, path, sizeof(path), dirPath, sizeof(dirPath)))
	{
		return 0;
	}

	f = fopen(path, "rb");
	if (f == NULL)
	{
		Platform_Log("[CTR Preview] %s: no preview (%s) - placeholder\n", entry->file, path);
		return 0;
	}

	if ((fread(header, 1, sizeof(header), f) != sizeof(header)) || (memcmp(header, NATIVE_PREVIEW_MAGIC, 8) != 0) ||
	    (header[8] != NATIVE_PREVIEW_VERSION) || (NativePreview_Get16(&header[0x0C]) != NATIVE_PREVIEW_WIDTH) ||
	    (NativePreview_Get16(&header[0x0E]) != NATIVE_PREVIEW_HEIGHT) || (NativePreview_Get16(&header[0x10]) != NATIVE_PREVIEW_FRAMES))
	{
		Platform_Log("[CTR Preview] %s: %s is not a preview of this version - placeholder\n", entry->file, path);
		fclose(f);
		return 0;
	}

	if (!entry->levdShaOk || (memcmp(&header[0x14], entry->levdSha, 32) != 0))
	{
		Platform_Log("[CTR Preview] %s: preview is stale, the LEVD hash differs from the container - placeholder\n", entry->file);
		fclose(f);
		return 0;
	}

	s_prevPlay.file = f;
	s_prevPlay.trackIndex = trackIndex;
	Platform_Log("[CTR Preview] %s: playing %s\n", entry->file, path);
	return 1;
}

int NativePreview_Upload(int frameIndex, int dstX, int dstY)
{
	RECT16 rect;
	long offset;

	if (s_prevPlay.file == NULL)
	{
		return 0;
	}

	frameIndex %= NATIVE_PREVIEW_FRAMES;
	offset = NATIVE_PREVIEW_HEADER + (long)frameIndex * NATIVE_PREVIEW_FRAME_PIXELS * 2;

	if ((fseek(s_prevPlay.file, offset, SEEK_SET) != 0) || (fread(s_prevPlay.pixels, 2, NATIVE_PREVIEW_FRAME_PIXELS, s_prevPlay.file) != NATIVE_PREVIEW_FRAME_PIXELS))
	{
		NativePreview_Close();
		return 0;
	}

	// Like NativeSTR_UploadNextFrame (native_str.c:803-827): into VRAM, then
	// immediately visible to the draw path of the same frame.
	rect.x = (s16)dstX;
	rect.y = (s16)dstY;
	rect.w = NATIVE_PREVIEW_WIDTH;
	rect.h = NATIVE_PREVIEW_HEIGHT;
	LoadImage(&rect, (u32 *)s_prevPlay.pixels);
	NativeRenderer_UpdateVRAM();
	return 1;
}
