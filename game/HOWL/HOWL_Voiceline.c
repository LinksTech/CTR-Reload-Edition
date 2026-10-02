#include <common.h>

// From platform/native_chars.c, further down in the translation unit. A seat
// bound to a custom character never speaks with its template's voice
// (NativeChar_SeatSilent 1); with voices of its own (NativeChar_SeatVoiced 1)
// it speaks with those. The two speak calls give the clip's length in the
// unit of CDSYS_XAGetTrackLength, 0 when nothing plays.
int NativeChar_SeatSilent(int seat);
int NativeChar_SeatVoiced(int seat);
int NativeChar_SeatSpeakLine(int seat, u32 voiceSet, u32 pick, int volume);
int NativeChar_SeatSpeakShort(int seat, u32 voiceType, u32 pick, int volumeLeft, int volumeRight);

// does not really touch voiceline
void Voiceline_PoolInit(void)
{
	s32 index;

	sdata->criticalSectionCount = 0;

	sdata->numBackup_ChannelStats = 0;

	sdata->ptrCseqHeader = 0;

	Bank_ResetAllocator();

	Audio_SetDefaults();

	LIST_Clear(&sdata->channelFree);
	LIST_Clear(&sdata->channelTaken);

	LIST_Init(&sdata->channelFree, &sdata->channelStatsPrev[0].link.item, 0x20, 0x18);

	SpuSetReverbVoice(0, 0xffffff);

	// initialize all members in sound list
	for (index = 0; index < 24; index++)
	{
		struct ChannelStats *stats = &sdata->channelStatsPrev[index];
		sdata->ChannelUpdateFlags[index] = 0;

		SpuSetVoiceADSRAttr(index, 0, 0xf, 0x7f, 2, 0xf, 5, 1, 3);

		stats->flags = 0;
		stats->channelID = index;

		stats->ad = 0x80ff;
		stats->sr = 0x1fc2;

		struct ChannelAttr *curr = &sdata->channelAttrCur[index];

		curr->spuStartAddr = (void *)-1;

		curr->ad = 0x80ff;
		curr->sr = 0x1fc2;

		curr->pitch = -1;
		curr->reverb = -1;
		curr->audioL = -1;
		curr->audioR = -1;
	}

	for (index = 0; index < 2; index++)
	{
		struct Song *pool = &sdata->songPool[index];

		// not playing
		pool->flags = 0;

		pool->songPoolIndex = index;
	}

	for (index = 0; index < 24; index++)
	{
		struct SongSeq *seq = &sdata->songSeq[index];

		// not playing
		seq->flags = 0;

		seq->soundID = index;
	}
}

void Voiceline_ClearTimeStamp(void)
{
	for (s32 i = 0; i < 16; i++)
	{
		// Clear audio timestamps arrays
		sdata->timeSet1[i] = 0;
		sdata->timeSet2[i] = 0;
	}
}

void Voiceline_PoolClear(void)
{
	sdata->boolCanPlayWrongWaySFX = false;

	sdata->voicelineCooldown = 0;

	sdata->boolCanPlayVoicelines = false;

	LIST_Clear(&sdata->Voiceline1);

	LIST_Clear(&sdata->Voiceline2);

	// put them all on free list
	LIST_Init(&sdata->Voiceline1, &sdata->voicelinePool[0].item, sizeof(struct VoicelineItem), 8);

	Voiceline_ClearTimeStamp();
}

void Voiceline_StopAll(void)
{
	while (sdata->Voiceline2.last != 0)
	{
		struct Item *voiceLine = sdata->Voiceline2.last;

		LIST_RemoveMember(&sdata->Voiceline2, voiceLine);
		LIST_AddFront(&sdata->Voiceline1, voiceLine);
	}
}

void Voiceline_ToggleEnable(int toggle)
{
	// if this is disabling
	if (toggle == 0)
	{
		sdata->voicelineCooldown = 0;

		Voiceline_StopAll();
	}
	sdata->boolCanPlayVoicelines = toggle;
}

static u32 Voiceline_RequestPlay_NextAudioRNG(void)
{
	sdata->audioRNG = ((sdata->audioRNG >> 3) + sdata->audioRNG * 0x20000000) * 5 + 1;
	return sdata->audioRNG;
}

// A voice names its speaker by character id, not by seat. A custom character is
// bound only where that id belongs to one seat (a one-player arcade race: the
// bots are the other ids, LOAD_Robots1P), so the seat holding the id answers:
// the bound seat, or -1 for a retail speaker. Without a bound seat it is
// always -1; reads only, no state.
static int Voiceline_CustomSpeaker(u32 characterID)
{
	for (int seat = 0; seat < LOAD_CHARACTER_ID_COUNT; seat++)
	{
		if (((u32)data.characterIDs[seat] == characterID) && (NativeChar_SeatSilent(seat) != 0))
		{
			return seat;
		}
	}

	return -1;
}

// The line of a custom speaker, in place of CDSYS_XAPlay(CDSYS_XA_TYPE_GAME,
// xaID): its own clip of the same set, picked with the same RNG value. A clip
// that plays holds the CD channel as the XA line would, so the queue waits, a
// pause fades it and a load stops it as it would the retail line; the wait
// after it is computed as retail computes it, from the clip's length in the
// same unit. Nothing to play is the XA failure: 0x1e.
static void Voiceline_StartCustom(int seat, u32 voiceSetIndex, u32 rng, u32 xaID)
{
	const int volume = sdata->vol_Voice << CDSYS_XA_VOLUME_SHIFT;
	const int length = NativeChar_SeatSpeakLine(seat, voiceSetIndex, rng, volume);

	if (length == 0)
	{
		sdata->voicelineCooldown = 0x1e;
		return;
	}

	// A copy of the XA state the native branch of CDSYS_XAPlay (game/CDSYS.c)
	// sets for a track that starts: keep the two alike.
	sdata->XA_State = XA_PLAYING;
	sdata->XA_Playing_Index = (int)xaID;
	sdata->XA_Playing_Category = CDSYS_XA_TYPE_GAME;
	sdata->XA_VolumeBitshift = volume;
	sdata->XA_boolFinished = 0;
	sdata->XA_CurrOffset = 0;
	sdata->XA_MaxSampleIndex = 0;
	sdata->XA_MaxSampleNumSaved = 0;
	for (int i = 0; i < CDSYS_XA_MAX_SAMPLE_WINDOW; i++)
	{
		sdata->XA_MaxSampleValArr[i] = 0;
	}
	sdata->XA_MaxSampleVal = 0;
	sdata->XA_MaxSampleValInArr = 0;

	sdata->voicelineCooldown = (s16)(length / 5) + 0x1e;
}

// The short sound of a custom speaker, in place of OtherFX_Play(soundID, 2): at
// the SPU voice volume the retail sound would get - OtherFX_Play_LowLevel with
// the default flags (howl_InitChannelAttr_OtherFX: vol_Voice for a sound with
// the voice flag, vol_FX else, times its volume; Channel_SetVolume). Where
// retail would play nothing (audio off, no such sound, not loaded), nothing.
static void Voiceline_ShortCustom(int seat, u32 voiceType, u32 soundID)
{
	struct ChannelAttr attr;
	struct OtherFX *otherFX;

	if ((sdata->boolAudioEnabled == 0) || (sdata->ptrHowlHeader == NULL) || (soundID >= (u32)sdata->ptrHowlHeader->numOtherFX))
	{
		return;
	}

	otherFX = &sdata->howl_metaOtherFX[soundID];
	if (Howl_SpuAddr(otherFX->spuIndex) == 0)
	{
		return;
	}

	howl_InitChannelAttr_OtherFX(otherFX, &attr, HowlSfx_Volume(HOWL_SFX_DEFAULT_FLAGS), HowlSfx_LR(HOWL_SFX_DEFAULT_FLAGS),
	                             HowlSfx_Distortion(HOWL_SFX_DEFAULT_FLAGS));

	// The clip: read the audio RNG, never move it.
	(void)NativeChar_SeatSpeakShort(seat, voiceType, sdata->audioRNG >> 8, attr.audioL, attr.audioR);
}

void Voiceline_RequestPlay(u32 voiceID, u32 characterID, u32 characterID2)
{
	u8 voiceType;
	u32 elapsedFrames;
	u32 canImmediate;
	u32 canQueue;
	int customSeat;

	if (voiceID >= 0x18)
	{
		return;
	}

	if (characterID >= 0x10)
	{
		return;
	}

	if (characterID2 >= 0x11)
	{
		return;
	}

	// A bound custom seat speaks with no voice of its template. Without voices
	// of its own it is dropped here, before the audio RNG and the timestamps
	// move; with them it takes the retail decision below.
	customSeat = Voiceline_CustomSpeaker(characterID);
	if ((customSeat >= 0) && (NativeChar_SeatVoiced(customSeat) == 0))
	{
		return;
	}

	if ((sdata->gGT->gameMode1 & END_OF_RACE) != 0)
	{
		return;
	}

	voiceType = data.voiceID[voiceID];

	if ((s32)voiceID >= 8)
	{
		u32 alreadyPlayed = sdata->timeSet1[characterID] & (1 << (voiceID & 0x1f));
		u32 rng = Voiceline_RequestPlay_NextAudioRNG();

		if (alreadyPlayed != 0)
		{
			rng &= 7;
		}
		else
		{
			rng &= 3;
		}

		if (rng != 0)
		{
			return;
		}
	}

	elapsedFrames = (u32)CTR_MipsSubLo(sdata->gGT->frameTimer_MainFrame_ResetDB, sdata->timeSet2[characterID]);
	canImmediate = 0;
	if (elapsedFrames >= 0x3d)
	{
		canImmediate = voiceType < 2;
	}

	canQueue = 1;
	if (sdata->boolCanPlayVoicelines == 0)
	{
		canQueue = 0;
	}
	else if ((sdata->voicelineCooldown != 0) && (((u8 *)sdata->backupParams_FUN_8002cf28)[0xa] == characterID))
	{
		canQueue = 0;
	}
	else if (elapsedFrames < 0x3c)
	{
		canQueue = 0;
	}

	if (canQueue != 0)
	{
		if (canImmediate != 0)
		{
			u32 rng = Voiceline_RequestPlay_NextAudioRNG();

			canImmediate = 0;
			if ((rng & 1) != 0)
			{
				canQueue = 0;
				goto playImmediate;
			}
		}
	}
	else
	{
		if (canImmediate == 0)
		{
			return;
		}
	}

	if (canImmediate == 0)
	{
		goto queueVoiceline;
	}

playImmediate:
	if (customSeat >= 0)
	{
		// Its own short sound, beside the XA and the line.
		Voiceline_ShortCustom(customSeat, voiceType, (characterID + ((voiceType == 0) ? 0x1c : 0x2c)) & 0xffff);
	}
	else if (voiceType == 0)
	{
		OtherFX_Play((characterID + 0x1c) & 0xffff, 2);
	}
	else if (voiceType == 1)
	{
		OtherFX_Play((characterID + 0x2c) & 0xffff, 2);
	}

	sdata->timeSet2[characterID] = sdata->gGT->frameTimer_MainFrame_ResetDB;
	return;

queueVoiceline:
	if (canQueue == 0)
	{
		return;
	}

	sdata->timeSet1[characterID] |= 1 << (voiceID & 0x1f);

	for (struct Item *item = sdata->Voiceline2.first; item != NULL; item = item->next)
	{
		struct VoicelineItem *voiceLine = (struct VoicelineItem *)item;

		if ((voiceID == (u32)voiceLine->voiceID) && (characterID == voiceLine->characterID))
		{
			return;
		}
	}

	struct Item *item = sdata->Voiceline1.first;
	if (item != NULL)
	{
		LIST_RemoveMember(&sdata->Voiceline1, item);
	}
	else
	{
		item = sdata->Voiceline2.last;
		if (item != NULL)
		{
			LIST_RemoveMember(&sdata->Voiceline2, item);
		}
	}

	LIST_AddFront(&sdata->Voiceline2, item);

	{
		struct VoicelineItem *voiceLine = (struct VoicelineItem *)item;

		voiceLine->characterID = characterID;
		voiceLine->secondaryCharacterID = characterID2;
		voiceLine->voiceID = voiceID;
		voiceLine->startFrame = sdata->gGT->timer;
	}
}

void Voiceline_StartPlay(struct Item *voiceLine)
{
	struct VoicelineItem *voiceLineItem = (struct VoicelineItem *)voiceLine;
	u32 voiceID = (u16)voiceLineItem->voiceID;
	u32 characterID = voiceLineItem->characterID;
	u32 voiceSetIndex;

	CTR_WriteU32LE(&sdata->backupParams_FUN_8002cf28[0], CTR_ReadU32LE((u8 *)voiceLineItem + 0x0));
	CTR_WriteU32LE(&sdata->backupParams_FUN_8002cf28[1], CTR_ReadU32LE((u8 *)voiceLineItem + 0x4));
	CTR_WriteU32LE(&sdata->backupParams_FUN_8002cf28[2], CTR_ReadU32LE((u8 *)voiceLineItem + 0x8));
	CTR_WriteU32LE(&sdata->backupParams_FUN_8002cf28[3], CTR_ReadU32LE((u8 *)voiceLineItem + 0xc));

	if ((IS_BOSS_RACE(sdata->gGT->gameMode1)) && ((u32)(voiceID - 10) < 6) && (((u32)(characterID - 8) < 4) || (characterID == 0xf)))
	{
		u32 rng = Voiceline_RequestPlay_NextAudioRNG();
		voiceSetIndex = (rng & 3) + 4;
	}
	else
	{
		voiceSetIndex = data.voiceID[(s16)voiceID];
	}

	s16 *voiceIDs = data.voiceData[characterID].voiceSet[voiceSetIndex].ptr;
	u16 numVoiceIDs = data.voiceData[characterID].voiceSet[voiceSetIndex].num;

	if (numVoiceIDs == 0)
	{
		Voiceline_StopAll();
		return;
	}

	u32 rng = Voiceline_RequestPlay_NextAudioRNG();
	u32 voiceIndex = rng % numVoiceIDs;
	u32 xaID = (u16)voiceIDs[voiceIndex];
	int customSeat = Voiceline_CustomSpeaker(characterID);

	if (customSeat >= 0)
	{
		Voiceline_StartCustom(customSeat, voiceSetIndex, rng, xaID);
		return;
	}

	if (CDSYS_XAPlay(CDSYS_XA_TYPE_GAME, xaID) == 0)
	{
		sdata->voicelineCooldown = 0x1e;
		return;
	}

	sdata->voicelineCooldown = (s16)(CDSYS_XAGetTrackLength(CDSYS_XA_TYPE_GAME, xaID) / 5) + 0x1e;
}

void Voiceline_Update(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (sdata->boolCanPlayVoicelines == 0)
	{
		return;
	}

	if (sdata->voicelineCooldown != 0)
	{
		sdata->voicelineCooldown = (s16)CTR_MipsSubLo((u16)sdata->voicelineCooldown, 1);
		if (sdata->voicelineCooldown != 0)
		{
			return;
		}
	}

	if (sdata->XA_State != 0)
	{
		return;
	}

	if (sdata->boolCanPlayWrongWaySFX != 0)
	{
		if ((sdata->WrongWayDirection_bool != 0) && (sdata->framesDrivingSameDirection > 0x1e))
		{
			u32 voiceID;

			sdata->boolCanPlayWrongWaySFX = false;

			if (gGT->numPlyrCurrGame == 1)
			{
				if (!VehPickupItem_MaskBoolGoodGuy(gGT->drivers[0]))
				{
					voiceID = 0x3d;
				}
				else
				{
					voiceID = 0x1e;
				}

				if (CDSYS_XAPlay(CDSYS_XA_TYPE_EXTRA, voiceID) == 0)
				{
					sdata->voicelineCooldown = 0x1e;
					return;
				}

				sdata->voicelineCooldown = (s16)(CDSYS_XAGetTrackLength(CDSYS_XA_TYPE_EXTRA, voiceID) / 5) + 0x1e;
				return;
			}
		}

		if (sdata->boolCanPlayWrongWaySFX != 0)
		{
			goto playQueuedVoice;
		}
	}

	if ((sdata->WrongWayDirection_bool == 0) && (sdata->framesDrivingSameDirection > 0x1e))
	{
		sdata->boolCanPlayWrongWaySFX = true;
	}

playQueuedVoice:
	if (sdata->Voiceline2.first != NULL)
	{
		struct Item *first = sdata->Voiceline2.first;

		LIST_RemoveMember(&sdata->Voiceline2, first);
		LIST_AddBack(&sdata->Voiceline1, first);
		Voiceline_StartPlay(first);
	}
}

void Voiceline_EmptyFunc(void)
{
}

void Voiceline_SetDefaults(void)
{
	sdata->audioState = AUDIO_NONE;
	sdata->desiredXA_FinalLapIndex = 0;
	sdata->desiredXA_RaceIntroIndex = 0;

	sdata->WrongWayDirection_bool = false;

	sdata->framesDrivingSameDirection = 0;
	sdata->nTropyVoiceCount = 0;
	sdata->boolNeedXASeek = 0;

	Music_SetDefaults();
}
