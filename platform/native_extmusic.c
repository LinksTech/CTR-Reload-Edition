// THE EXTERNAL TRACK'S MUSIC (--ext-track, see platform/engine_exttrack.c).
// Declared in include/platform/engine.h (EXTERNAL TRACK, the parts' entry points).
//
// WHAT. The track folder's music file (exttrack.json section "music", key
// "file", default music.wav: PCM 16 bit, stereo, 44 100 Hz = the mixer's rate,
// platform/native_audio.c; the converter resamples, the game never does) is
// read whole into memory at the track's level load and played by the mixer's
// music place (NativeAudio_Music*), looping over the whole file.
//
// WHEN IT SOUNDS. It follows the level song the game itself plays, read only:
// it sounds while the song pool entry with id CSEQ_SONG_LEVEL is playing and not
// paused (struct Song flags: 1 playing, 2 paused - include/namespace_Howl.h,
// struct Song; CseqMusic_Pause sets 2, game/HOWL/HOWL_CseqMusic.c:44-69) and the
// game's audio pause is off (ExtTrack_AudioPaused, from MainFrame_TogglePauseAudio). So the
// Aku/Uka mask music (songs 1 and 2, game/HOWL/HOWL_Music.c:468-486) silences it
// as it silences the level song, and the pause pauses it. When the level song
// starts over (its timeSpentPlaying falls back, game/HOWL/HOWL_SongPool.c:76 -
// a restart of the race) the track starts over at frame 0.
// Volume: vol_Music << CDSYS_XA_VOLUME_SHIFT, as the game's own CD music
// (game/CDSYS.c:667), times the level song's vol_Curr / 255 - what
// Music_LowerVolume / Music_RaiseVolume change around the last lap
// (game/HOWL/HOWL_Music.c:489-526) and what the song's fades change.
// The state goes to the mixer once per logic frame; it holds half a second, so
// leaving the race (no more logic frames of this track) silences the place.
//
// THE DONOR'S SONG. game/HOWL/HOWL_CseqOpcode.c (cseq_opcode05_noteon) asks
// ExtMusic_MutesSong and starts no voice for a note of the level song while the
// external track holds the loaded level. The sequencer and every state the game
// keeps run on unchanged. Only with --ext-track: without it the first compare
// returns 0, on every other level too (ExtTrack_ActiveForLevel).
//
// LOG. "[CTR ExtMusic] opened ...", "refused: ..." once per run; at exit
// "[CTR ExtMusic] at exit: frames mixed M, loops L, silent frames S; level song
// notes muted N" (gate D9).

#define EXTMUSIC_RATE      44100
#define EXTMUSIC_MAX_BYTES (64u * 1024u * 1024u)

global_variable s16 *s_extMusic;          // interleaved L R, the mixer reads it
global_variable int s_extMusicFrames;
global_variable int s_extMusicStarted;    // handed to the mixer
global_variable int s_extMusicTried;      // the file was looked at (once per run)
global_variable int s_extMusicReported;   // the exit line is registered
global_variable u32 s_extMusicLastTime;   // the level song's timeSpentPlaying last frame
global_variable s64 s_extMusicNotesMuted;
global_variable char s_extMusicFile[ENGINE_EXTTRACK_PATH_MAX];

internal u32 ExtMusic_U32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

internal u32 ExtMusic_U16(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8);
}

// RIFF/WAVE, format 1 (PCM), 2 channels, 16 bit, 44 100 Hz; every other chunk is
// skipped. Returns the frame count and *pcm; 0 with *why.
internal int ExtMusic_ParseWav(const u8 *b, u32 size, const u8 **pcm, const char **why)
{
	u32 o = 12;
	int haveFmt = 0;

	*pcm = NULL;
	*why = "is not a RIFF/WAVE file";
	if ((size < 12) || (memcmp(b, "RIFF", 4) != 0) || (memcmp(b + 8, "WAVE", 4) != 0))
	{
		return 0;
	}
	while ((o <= size) && ((size - o) >= 8))
	{
		const u32 len = ExtMusic_U32(b + o + 4);
		const u8 *body = b + o + 8;

		if (len > (size - o - 8))
		{
			*why = "has a chunk that runs past the end of the file";
			return 0;
		}
		if (memcmp(b + o, "fmt ", 4) == 0)
		{
			if (len < 16)
			{
				*why = "has a fmt chunk that is too short";
				return 0;
			}
			if ((ExtMusic_U16(body) != 1) || (ExtMusic_U16(body + 2) != 2) || (ExtMusic_U32(body + 4) != EXTMUSIC_RATE) || (ExtMusic_U16(body + 14) != 16))
			{
				*why = "is not PCM 16 bit stereo 44100 Hz";
				return 0;
			}
			haveFmt = 1;
		}
		else if (memcmp(b + o, "data", 4) == 0)
		{
			if (!haveFmt)
			{
				*why = "has its data chunk before the fmt chunk";
				return 0;
			}
			if (len < 4u)
			{
				*why = "has no sample frames";
				return 0;
			}
			*pcm = body;
			*why = NULL;
			return (int)(len / 4u);
		}
		o += 8u + len + (len & 1u);
	}
	*why = "has no data chunk";
	return 0;
}

internal void ExtMusic_AtExit(void)
{
	u64 mixed = 0;
	u64 silent = 0;
	u32 loops = 0;

	NativeAudio_MusicStats(&mixed, &loops, &silent);
	Platform_Log("[CTR ExtMusic] at exit: frames mixed %llu, loops %u, silent frames %llu; level song notes muted %lld\n", (unsigned long long)mixed, loops,
	             (unsigned long long)silent, s_extMusicNotesMuted);
}

// Reads the file named by the data into s_extMusic. Once per run.
internal void ExtMusic_Load(void)
{
	const ExtJsonDoc *doc = ExtTrack_Json();
	const char *name = "music.wav";
	const char *why = NULL;
	const u8 *pcm = NULL;
	FILE *file;
	u8 *bytes = NULL;
	long size;
	int frames;

	s_extMusicTried = 1;
	if (!s_extMusicReported)
	{
		s_extMusicReported = 1;
		Platform_AtExitReport(ExtMusic_AtExit);
	}
	if (doc != NULL)
	{
		name = ExtJson_String(doc, ExtJson_Get(doc, ExtTrack_Section("music"), "file"), name);
	}
	if (!ExtTrack_FilePath(name, s_extMusicFile, (int)sizeof(s_extMusicFile)))
	{
		Platform_Log("[CTR ExtMusic] refused: the path of '%s' is too long - the track stays without music\n", name);
		return;
	}
	file = fopen(s_extMusicFile, "rb");
	if (file == NULL)
	{
		Platform_Log("[CTR ExtMusic] refused: %s not found - the track stays without music\n", s_extMusicFile);
		return;
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if ((size > 0) && ((u32)size <= EXTMUSIC_MAX_BYTES))
	{
		bytes = (u8 *)malloc((size_t)size);
	}
	if ((bytes == NULL) || (fread(bytes, 1, (size_t)size, file) != (size_t)size))
	{
		fclose(file);
		free(bytes);
		Platform_Log("[CTR ExtMusic] refused: %s cannot be read (%ld bytes, at most %u) - the track stays without music\n", s_extMusicFile, size,
		             EXTMUSIC_MAX_BYTES);
		return;
	}
	fclose(file);

	frames = ExtMusic_ParseWav(bytes, (u32)size, &pcm, &why);
	if (frames > 0)
	{
		s_extMusic = (s16 *)malloc((size_t)frames * 4u);
	}
	if ((frames <= 0) || (s_extMusic == NULL))
	{
		free(bytes);
		Platform_Log("[CTR ExtMusic] refused: %s %s - the track stays without music\n", s_extMusicFile, (why != NULL) ? why : "does not fit in memory");
		return;
	}
	// Little endian in the file and in memory (every target of the game).
	memcpy(s_extMusic, pcm, (size_t)frames * 4u);
	free(bytes);
	s_extMusicFrames = frames;
	Platform_Log("[CTR ExtMusic] opened %s: %d frames, %d Hz, 2 ch, %.3f s\n", name, frames, EXTMUSIC_RATE, (double)frames / (double)EXTMUSIC_RATE);
}

// From the dispatcher, once per level load of the active external track.
void ExtMusic_OnLevelLoaded(struct Level *lev)
{
	(void)lev;
	if (!s_extMusicTried)
	{
		ExtMusic_Load();
	}
	// Every load of the track starts the music at frame 0.
	if (s_extMusicStarted)
	{
		NativeAudio_MusicRewind();
	}
	s_extMusicLastTime = 0;
}

// The pool entry of the level song, NULL when none is taken.
internal const struct Song *ExtMusic_LevelSong(void)
{
	int i;

	for (i = 0; i < 2; i++)
	{
		const struct Song *song = &sdata->songPool[i];

		if (((song->flags & 1) != 0) && (song->id == CSEQ_SONG_LEVEL))
		{
			return song;
		}
	}
	return NULL;
}

// From the dispatcher once per logic frame (also in the pause: the main loop
// runs the hook whenever no load is in progress, game/MAIN/MainMain.c).
void ExtMusic_OnLogicFrame(void)
{
	const struct Song *song;
	int playing = 0;
	int volume = 0;

	if (s_extMusic == NULL)
	{
		return;
	}
	if (!s_extMusicStarted)
	{
		// The mixer runs once the game's sound is up (it is, long before a race).
		s_extMusicStarted = NativeAudio_MusicStart(s_extMusic, s_extMusicFrames, EXTMUSIC_RATE);
		if (!s_extMusicStarted)
		{
			return;
		}
	}

	song = ExtMusic_LevelSong();
	if (song != NULL)
	{
		const u32 timeNow = (u32)song->timeSpentPlaying;

		if (timeNow < s_extMusicLastTime)
		{
			NativeAudio_MusicRewind();
		}
		s_extMusicLastTime = timeNow;
		// Paused twice over: the song's own bit (CseqMusic_Pause) and the game's
		// audio pause (MainFrame_TogglePauseAudio -> ExtTrack_OnAudioPause).
		playing = ((song->flags & 2) == 0) && !ExtTrack_AudioPaused();
		volume = (int)(((s64)sdata->vol_Music << CDSYS_XA_VOLUME_SHIFT) * (s64)song->vol_Curr / 255);
	}
	NativeAudio_MusicSetState(playing, volume);
}

// game/HOWL/HOWL_CseqOpcode.c, cseq_opcode05_noteon: 1 = start no voice for this
// note of the level song.
int ExtMusic_MutesSong(int songPoolIndex)
{
	const struct GameTracker *gGT;

	if (!ExtTrack_Active())
	{
		return 0;
	}
	gGT = sdata->gGT;
	if ((gGT == NULL) || (songPoolIndex < 0) || (songPoolIndex > 1) || !ExtTrack_ActiveForLevel((int)gGT->levelID))
	{
		return 0;
	}
	if (sdata->songPool[songPoolIndex].id != CSEQ_SONG_LEVEL)
	{
		return 0;
	}
	s_extMusicNotesMuted++;
	return 1;
}

// --ext-selftest music: the WAV reader on made-up files and the mixer's music
// place on a made-up buffer; no window, no data. Prints "ext selftest music
// passed: N checks, 0 failures" (or FAILED); returns 0 / 1.
int ExtMusic_Selftest(void)
{
	// 44 bytes of header and 2 frames.
	static const u8 good[52] = {'R', 'I', 'F', 'F', 44, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0,    0,    0, 1, 0, 2, 0, 0x44, 0xAC,
	                            0,   0,   0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 8,   0,   0,    0, 1, 0, 2, 0, 3, 0, 4,    0};
	u8 buf[80];
	const u8 *pcm = NULL;
	const char *why = NULL;
	int checks = 0;
	int failures = 0;

#define EXTMUSIC_CHECK(cond)                                                                                                                         \
	do                                                                                                                                               \
	{                                                                                                                                                \
		checks++;                                                                                                                                    \
		if (!(cond))                                                                                                                                 \
		{                                                                                                                                            \
			failures++;                                                                                                                              \
			printf("ext selftest music: FAILED %s (line %d)\n", #cond, __LINE__);                                                                    \
		}                                                                                                                                            \
	} while (0)

	// A good file: 2 frames, the samples right behind the data header.
	EXTMUSIC_CHECK((ExtMusic_ParseWav(good, sizeof(good), &pcm, &why) == 2) && (pcm == good + 44) && (why == NULL));
	EXTMUSIC_CHECK((pcm != NULL) && (pcm[0] == 1) && (pcm[6] == 4));

	// 48 000 Hz is refused (the converter resamples, the game does not).
	memcpy(buf, good, sizeof(good));
	buf[24] = 0x80;
	buf[25] = 0xBB;
	EXTMUSIC_CHECK(ExtMusic_ParseWav(buf, sizeof(good), &pcm, &why) == 0);
	// Mono is refused.
	memcpy(buf, good, sizeof(good));
	buf[22] = 1;
	EXTMUSIC_CHECK(ExtMusic_ParseWav(buf, sizeof(good), &pcm, &why) == 0);
	// 8 bit is refused.
	memcpy(buf, good, sizeof(good));
	buf[34] = 8;
	EXTMUSIC_CHECK(ExtMusic_ParseWav(buf, sizeof(good), &pcm, &why) == 0);
	// Not PCM (format 3, float) is refused.
	memcpy(buf, good, sizeof(good));
	buf[20] = 3;
	EXTMUSIC_CHECK(ExtMusic_ParseWav(buf, sizeof(good), &pcm, &why) == 0);
	// Cut off inside the data chunk: refused, nothing read past the end.
	EXTMUSIC_CHECK(ExtMusic_ParseWav(good, 48, &pcm, &why) == 0);
	// Not RIFF, too short.
	EXTMUSIC_CHECK(ExtMusic_ParseWav(good + 1, sizeof(good) - 1, &pcm, &why) == 0);
	EXTMUSIC_CHECK(ExtMusic_ParseWav(good, 8, &pcm, &why) == 0);
	// A LIST chunk of odd length (with its pad byte) before fmt is skipped.
	memcpy(buf, good, 12);
	memcpy(buf + 12, "LIST", 4);
	buf[16] = 3;
	buf[17] = 0;
	buf[18] = 0;
	buf[19] = 0;
	memset(buf + 20, 'x', 4);
	memcpy(buf + 24, good + 12, sizeof(good) - 12);
	EXTMUSIC_CHECK((ExtMusic_ParseWav(buf, 24 + sizeof(good) - 12, &pcm, &why) == 2) && (pcm == buf + 24 + 32));
	// A data chunk before the fmt chunk is refused.
	memcpy(buf, good, 12);
	memcpy(buf + 12, good + 36, 16);
	memcpy(buf + 28, good + 12, 24);
	EXTMUSIC_CHECK(ExtMusic_ParseWav(buf, sizeof(good), &pcm, &why) == 0);

	// Without --ext-track the donor song is never muted.
	EXTMUSIC_CHECK(ExtMusic_MutesSong(0) == 0);
	EXTMUSIC_CHECK(ExtMusic_MutesSong(1) == 0);
	EXTMUSIC_CHECK(s_extMusicNotesMuted == 0);

	// The mixer's music place.
	NativeAudio_MusicSelftest(&checks, &failures);

#undef EXTMUSIC_CHECK

	if (failures != 0)
	{
		printf("ext selftest music FAILED: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	printf("ext selftest music passed: %d checks, 0 failures\n", checks);
	return 0;
}
