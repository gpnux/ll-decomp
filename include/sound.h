#ifndef SOUND_H
#define SOUND_H

#include "common.h"

#define SOUND_SFX_SLOT_COUNT 4
#define SOUND_SFX_COUNT 220
#define SOUND_MPLAY_COUNT 5
#define SOUND_SONG_COUNT 285
#define SOUND_BGM_TRACK_COUNT 15
#define MPLAY_MEMACC_SIZE 16

/* Wrapper state: m4a PCM/DMA processing suspended; game updates still run. */
#define SOUND_FLAG_DRIVER_SUSPENDED 0x0001
#define SOUND_FLAG_BGM_FADE_IN 0x0002
#define SOUND_FLAG_BGM_FADE_OUT 0x0004

#define SOUND_BGM_VOLUME_MIN 2
#define SOUND_BGM_VOLUME_FULL 0x100

#define MPLAY_STATUS_TRACK_MASK 0x0000FFFF
#define MPLAY_STATUS_PAUSED 0x80000000

struct ToneData;
struct SoundChannel;

/* m4a track layout, 0x50 bytes on the GBA; unconfirmed fields stay opaque. */
struct MusicPlayerTrack
{
    u8 flags;                      /* 0x00 */
    u8 wait;                       /* 0x01 */
    u8 patternDepth;               /* 0x02 */
    u8 unknown03[0x1D];            /* 0x03 */
    struct SoundChannel *channels; /* 0x20 */
    u8 unknown24[0x1C];            /* 0x24 */
    const u8 *command;             /* 0x40 */
    const u8 *patternStack[3];     /* 0x44 */
};

/* Variable-length ROM record: trackCount pointers follow the 8-byte header. */
struct SongHeader
{
    u8 trackCount;                  /* 0x00 */
    u8 blockCount;                  /* 0x01 */
    u8 priority;                    /* 0x02 */
    u8 reverb;                      /* 0x03 */
    const struct ToneData *tone;    /* 0x04 */
    const u8 *tracks[1];            /* 0x08; trailing array, agbcc compatible */
};

/* m4a player layout, 0x40 bytes on the GBA. Unknown bytes stay explicit. */
struct MusicPlayerInfo
{
    const struct SongHeader *songHeader;                         /* 0x00 */
    u32 status;                                                 /* 0x04 */
    u8 trackCount;                                              /* 0x08; capacity */
    u8 priority;                                                /* 0x09 */
    u8 unknown0A;                                               /* 0x0A */
    u8 checkSongPriority;                                       /* 0x0B */
    u32 clock;                                                  /* 0x0C */
    u8 unknown10[8];                                            /* 0x10 */
    u8 *memAccArea;                                             /* 0x18 */
    u16 tempoRaw;                                               /* 0x1C */
    u16 tempoScale;                                             /* 0x1E */
    u16 tempoInterval;                                          /* 0x20 */
    u16 tempoCounter;                                           /* 0x22 */
    u16 fadeInterval;                                           /* 0x24 */
    u16 fadeCounter;                                            /* 0x26 */
    u16 fadeVolume;                                             /* 0x28; low bits are flags */
    u8 unknown2A[2];                                            /* 0x2A */
    struct MusicPlayerTrack *tracks;                            /* 0x2C; 0x50-byte records */
    const struct ToneData *tone;                                /* 0x30 */
    u32 ident;                                                  /* 0x34; 0x68736D53 when idle */
    void (*nextPlayerFunc)(struct MusicPlayerInfo *);             /* 0x38 */
    struct MusicPlayerInfo *nextPlayer;                          /* 0x3C */
};

struct SongTableEntry
{
    const struct SongHeader *songHeader; /* 0x00 */
    u16 musicPlayerIndex;                /* 0x04 */
    u16 unknown06;                       /* 0x06 */
};

struct MusicPlayerTableEntry
{
    struct MusicPlayerInfo *info;    /* 0x00 */
    struct MusicPlayerTrack *tracks; /* 0x04 */
    u8 trackCount;                  /* 0x08 */
    u8 unknown09;                   /* 0x09 */
    u16 checkSongPriority;          /* 0x0A; copied to info as a byte */
};

/* Public m4a COMMON objects defined in sound.c; wrapper state stays private. */
extern struct MusicPlayerInfo gMPlayInfoBgm;  /* 0x03005AB0 */
extern struct MusicPlayerInfo gMPlayInfoSfx0; /* 0x03005AF0 */
extern struct MusicPlayerInfo gMPlayInfoSfx1; /* 0x03005B30 */
extern struct MusicPlayerInfo gMPlayInfoSfx2; /* 0x03005BC0 */
extern struct MusicPlayerInfo gMPlayInfoSfx3; /* 0x03005B70 */
extern u8 gMPlayMemAccArea[MPLAY_MEMACC_SIZE]; /* 0x03005BB0 */

extern const struct SongTableEntry gSongTable[SOUND_SONG_COUNT]; /* 0x086B0E80 */
extern const struct MusicPlayerTableEntry gMPlayTable[SOUND_MPLAY_COUNT]; /* 0x086B0E44 */
extern const struct SongHeader *const gSfxSongTable[SOUND_SFX_COUNT]; /* 0x087ED910 */
extern struct MusicPlayerInfo *const gSfxPlayers[SOUND_SFX_SLOT_COUNT]; /* 0x087EDC80 */

void Sound_Update(void);
void Sound_UpdateSfx(void);
void Sound_Init(void);
u16 Sound_GetFlags(void);
void Sound_DisableVSync(void);
void Sound_EnableVSync(void);

/* BGM ids 63/64/65 map to gSongTable entries 283/284/282 respectively. */
void Sound_PlayBgm(u8 bgmId, u16 volume);
void Sound_StopBgm(void);
void Sound_SetBgmVolume(u16 volume);
/* Duration is measured in Sound_Update calls and must be nonzero. */
void Sound_FadeInBgm(u8 duration);
void Sound_FadeOutBgm(u8 duration);
void Sound_ResumeBgm(void);

/* Slots are 0..3, sfx ids are 0..219, and loop is 0 or 1. */
u8 Sound_IsSfxPlaying(u8 slot);
void Sound_PlaySfx(u16 sfxId, u8 slot, u8 loop);
void Sound_PlaySfxWithBgmFade(u16 sfxId, u8 slot);
void Sound_StopSfx(u8 slot);
u8 Sound_IsSfxLooping(u8 slot);

/* m4a services used by the game wrapper. */
void m4aSoundInit(void);
void m4aSoundMain(void);
void m4aSoundVSync(void);
void m4aSoundVSyncOff(void);
void m4aSoundVSyncOn(void);
void m4aSongNumStart(u16 songId);
void m4aSongNumStop(u16 songId);
void m4aSongNumContinue(u16 songId);
void MPlayStart(struct MusicPlayerInfo *info, const struct SongHeader *songHeader);
void m4aMPlayStop(struct MusicPlayerInfo *info);
void m4aMPlayImmInit(struct MusicPlayerInfo *info);
void m4aMPlayFadeOutTemporarily(struct MusicPlayerInfo *info, u16 interval);
void m4aMPlayFadeIn(struct MusicPlayerInfo *info, u16 interval);
void m4aMPlayVolumeControl(struct MusicPlayerInfo *info, u16 trackMask, u16 volume);

#endif
