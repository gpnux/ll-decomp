#include "battle.h"
#include "common.h"
#include "sound.h"

/* Shared interpolation helper; mode 0 uses signed 16-bit linear interpolation. */

/* Private wrapper state, allocated in sound.o(.bss) by .lcomm. */
static u16 gSoundFlags;              /* 0x03000F38 */
static u16 gCurrentBgmSong;          /* 0x03000F3A; gSongTable index */
static u16 gBgmVolume;               /* 0x03000F3C */
static s16 gBgmFadeStartVolume;      /* 0x03000F3E */
static u8 gBgmFadeDuration;          /* 0x03000F40 */
static u8 gBgmFadeFrame;             /* 0x03000F41 */
static u8 gSfxPlayingMask;           /* 0x03000F42 */
static u8 gSfxLoopMask;              /* 0x03000F43 */
static u16 gSfxSongIds[SOUND_SFX_SLOT_COUNT]; /* 0x03000F48; gSfxSongTable indices */
static u8 gSfxBgmRestoreMask;        /* 0x03000F50 */

/* Track workspaces referenced by the ROM gMPlayTable. */
static struct MusicPlayerTrack gMPlayTracksBgm[SOUND_BGM_TRACK_COUNT]; /* 0x03000F58 */
static struct MusicPlayerTrack gMPlayTracksSfx0[1]; /* 0x03001408 */
static struct MusicPlayerTrack gMPlayTracksSfx1[1]; /* 0x03001458 */
static struct MusicPlayerTrack gMPlayTracksSfx2[1]; /* 0x030014A8 */
static struct MusicPlayerTrack gMPlayTracksSfx3[1]; /* 0x030014F8 */

/* Shared m4a state: .comm definitions, placed by config/ram.ll.ld. */
struct MusicPlayerInfo gMPlayInfoBgm;  /* 0x03005AB0 */
struct MusicPlayerInfo gMPlayInfoSfx0; /* 0x03005AF0 */
struct MusicPlayerInfo gMPlayInfoSfx1; /* 0x03005B30 */
struct MusicPlayerInfo gMPlayInfoSfx3; /* 0x03005B70 */
u8 gMPlayMemAccArea[MPLAY_MEMACC_SIZE]; /* 0x03005BB0; shared by all five players */
struct MusicPlayerInfo gMPlayInfoSfx2; /* 0x03005BC0 */

/* The linker reserves these COMMON lengths explicitly. */
typedef char SoundPlayerSizeCheck[(sizeof(struct MusicPlayerInfo) == 0x40) ? 1 : -1];
typedef char SoundMemAccSizeCheck[(sizeof(gMPlayMemAccArea) == 0x10) ? 1 : -1];

/* 0x080533F0: advance the BGM fade, then m4a, then SFX completion/looping. */
void Sound_Update(void)
{
    if (gSoundFlags & SOUND_FLAG_BGM_FADE_IN)
    {
        Sound_SetBgmVolume(Battle_InterpolateS16(gBgmFadeStartVolume, SOUND_BGM_VOLUME_FULL,
                                       gBgmFadeDuration, gBgmFadeFrame, 0));
        if (gBgmFadeFrame < gBgmFadeDuration)
            gBgmFadeFrame++;
        else
            gSoundFlags &= ~SOUND_FLAG_BGM_FADE_IN;
    }
    else if (gSoundFlags & SOUND_FLAG_BGM_FADE_OUT)
    {
        Sound_SetBgmVolume(Battle_InterpolateS16(gBgmFadeStartVolume, (s16)-gBgmFadeStartVolume,
                                       gBgmFadeDuration, gBgmFadeFrame, 0));
        if (gBgmFadeFrame < gBgmFadeDuration)
            gBgmFadeFrame++;
        else
            gSoundFlags &= ~SOUND_FLAG_BGM_FADE_OUT;
    }

    m4aSoundMain();
    Sound_UpdateSfx();
}

/* 0x080534B4: finished SFX may restart or restore temporarily faded BGM. */
void Sound_UpdateSfx(void)
{
    u8 slot;

    for (slot = 0; slot < SOUND_SFX_SLOT_COUNT; slot++)
    {
        if ((gSfxPlayingMask >> slot) & 1)
        {
            if ((u16)gSfxPlayers[slot]->status == 0)
            {
                gSfxPlayingMask &= ~(1 << slot);
                if ((gSfxLoopMask >> slot) & 1)
                {
                    u16 sfxId = gSfxSongIds[slot];

                    MPlayStart(gSfxPlayers[slot], gSfxSongTable[sfxId]);
                    gSfxPlayingMask |= 1 << slot;
                    gSfxSongIds[slot] = sfxId;
                    if ((gSfxLoopMask >> slot) & 1)
                        gSfxLoopMask &= ~(1 << slot);
                    gSfxLoopMask |= 1 << slot;
                }
                if ((gSfxBgmRestoreMask >> slot) & 1)
                {
                    m4aMPlayFadeIn(&gMPlayInfoBgm, 2);
                    gSfxBgmRestoreMask &= ~(1 << slot);
                }
            }
        }
    }
}

/* 0x0805359C: initialize m4a and reset the wrapper's flags and masks. */
void Sound_Init(void)
{
    m4aSoundInit();
    gSoundFlags = 0;
    gCurrentBgmSong = 0;
    gBgmVolume = SOUND_BGM_VOLUME_FULL;
    gBgmFadeDuration = 0;
    gSfxPlayingMask = 0;
    gSfxLoopMask = 0;
    gSfxBgmRestoreMask = 0;
}

u16 Sound_GetFlags(void)
{
    return gSoundFlags;
}

void Sound_DisableVSync(void)
{
    m4aSoundVSyncOff();
    gSoundFlags |= SOUND_FLAG_DRIVER_SUSPENDED;
}

void Sound_EnableVSync(void)
{
    m4aSoundVSyncOn();
    gSoundFlags &= ~SOUND_FLAG_DRIVER_SUSPENDED;
}

/* 0x08053628: remap special BGM ids, start the song, and apply its volume. */
void Sound_PlayBgm(u8 bgmId, u16 volume)
{
    gCurrentBgmSong = bgmId;
    switch (gCurrentBgmSong)
    {
    case 63:
        gCurrentBgmSong = 283;
        break;
    case 64:
        gCurrentBgmSong = 284;
        break;
    case 65:
        gCurrentBgmSong = 282;
        break;
    }

    m4aSongNumStart(gCurrentBgmSong);
    m4aMPlayImmInit(&gMPlayInfoBgm);
    Sound_SetBgmVolume(volume);
}

void Sound_StopBgm(void)
{
    m4aSongNumStop(gCurrentBgmSong);
}

/* 0x0805369C: clamp 0/1 to 2; update the first eight BGM tracks (mask 0xFF). */
void Sound_SetBgmVolume(u16 volume)
{
    if (volume < SOUND_BGM_VOLUME_MIN)
        volume = SOUND_BGM_VOLUME_MIN;

    gBgmVolume = volume;
    m4aMPlayVolumeControl(&gMPlayInfoBgm, 0xFF, volume);
}

/* 0x080536C0: schedule a fade from zero with a volume delta of 0x100. */
void Sound_FadeInBgm(u8 duration)
{
    gBgmFadeStartVolume = 0;
    gBgmFadeDuration = duration;
    gBgmFadeFrame = 0;
    gSoundFlags |= SOUND_FLAG_BGM_FADE_IN;
}

/* 0x080536EC: schedule a fade from the stored volume down to zero. */
void Sound_FadeOutBgm(u8 duration)
{
    gBgmFadeStartVolume = gBgmVolume;
    gBgmFadeDuration = duration;
    gBgmFadeFrame = 0;
    gSoundFlags |= SOUND_FLAG_BGM_FADE_OUT;
}

void Sound_ResumeBgm(void)
{
    m4aSongNumContinue(gCurrentBgmSong);
}

u8 Sound_IsSfxPlaying(u8 slot)
{
    return (gSfxPlayingMask >> slot) & 1;
}

/* 0x0805374C: play on a selected player and replace that slot's loop bit. */
void Sound_PlaySfx(u16 sfxId, u8 slot, u8 loop)
{
    MPlayStart(gSfxPlayers[slot], gSfxSongTable[sfxId]);
    gSfxPlayingMask |= 1 << slot;
    gSfxSongIds[slot] = sfxId;
    if ((gSfxLoopMask >> slot) & 1)
        gSfxLoopMask &= ~(1 << slot);
    gSfxLoopMask |= loop << slot;
}

/* 0x080537C0: play SFX and fade out/pause BGM; completion starts its fade-in. */
void Sound_PlaySfxWithBgmFade(u16 sfxId, u8 slot)
{
    MPlayStart(gSfxPlayers[slot], gSfxSongTable[sfxId]);
    gSfxPlayingMask |= 1 << slot;
    gSfxSongIds[slot] = sfxId;
    if ((gSfxBgmRestoreMask >> slot) & 1)
        gSfxBgmRestoreMask &= ~(1 << slot);
    gSfxBgmRestoreMask |= 1 << slot;
    m4aMPlayFadeOutTemporarily(&gMPlayInfoBgm, 2);
}

void Sound_StopSfx(u8 slot)
{
    m4aMPlayStop(gSfxPlayers[slot]);
    gSfxPlayingMask &= ~(1 << slot);
    if ((gSfxLoopMask >> slot) & 1)
        gSfxLoopMask &= ~(1 << slot);
}

u8 Sound_IsSfxLooping(u8 slot)
{
    return (gSfxLoopMask >> slot) & 1;
}

/* Split read-only data, in ROM address order. */

/* ROM 0x086B0E44..0x086B1768; .rodata. */
const struct MusicPlayerTableEntry gMPlayTable[SOUND_MPLAY_COUNT] __attribute__((aligned(1))) =
{
    {
        (struct MusicPlayerInfo *)&gMPlayInfoBgm, (struct MusicPlayerTrack *)&gMPlayTracksBgm, 0x0F, 0x00,
        0x0000,
    },
    {
        (struct MusicPlayerInfo *)&gMPlayInfoSfx0, (struct MusicPlayerTrack *)&gMPlayTracksSfx0, 0x01, 0x00,
        0x0000,
    },
    {
        (struct MusicPlayerInfo *)&gMPlayInfoSfx1, (struct MusicPlayerTrack *)&gMPlayTracksSfx1, 0x01, 0x00,
        0x0000,
    },
    {
        (struct MusicPlayerInfo *)&gMPlayInfoSfx2, (struct MusicPlayerTrack *)&gMPlayTracksSfx2, 0x01, 0x00,
        0x0000,
    },
    {
        (struct MusicPlayerInfo *)&gMPlayInfoSfx3, (struct MusicPlayerTrack *)&gMPlayTracksSfx3, 0x01, 0x00,
        0x0000,
    },
};

const struct SongTableEntry gSongTable[SOUND_SONG_COUNT] __attribute__((aligned(1))) =
{
    { (const struct SongHeader *)0x087D18D8, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D21D0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D25F4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D2AD4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D337C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D3860, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D3ED0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D4660, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D4D7C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D53C4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D5C44, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D6754, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D6D04, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D7710, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D7A0C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D7D80, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D8718, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D8A4C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D8EF0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D93D8, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D96D0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D9CC0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087D9F08, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DA234, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DA6E0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DAC6C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DB040, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DB160, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DB4DC, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DBD40, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DC270, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DC650, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DCC68, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DD214, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DDDA4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DDFF4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DE400, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DEA78, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DEC08, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DF044, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DF154, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DF3CC, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087DFADC, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E043C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E082C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E0DE4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E1740, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E1BC4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E22AC, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E236C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E247C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E2980, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E2BF8, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E3168, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E33B0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E358C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E3618, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E530C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E584C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E5D70, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E5FD4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E608C, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E60F0, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E610C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6128, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6144, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6160, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E617C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6198, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E61B4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E61D0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E61EC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6208, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6224, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6240, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E625C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6278, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6294, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E62B0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E62CC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E62E8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6304, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6320, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E633C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6358, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6374, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6390, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E63AC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E63C8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E63E4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6404, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6420, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E643C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6458, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6474, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6490, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E64AC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E64C8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E64E4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6500, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6520, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E653C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6560, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6580, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E65A0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E65C0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E65E0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E65FC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E661C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6638, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6654, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6670, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E668C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E66A8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E66C4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E66E0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E66FC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6718, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6734, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6750, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E676C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6788, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E67A4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E67C0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E67DC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E67F8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6818, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6838, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6854, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6870, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E688C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E68A8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E68C4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E68E0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E68FC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6918, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6934, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6950, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E696C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6988, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E69A4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E69C0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E69DC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E69F8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6A14, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6A34, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6A50, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6A6C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6A8C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6AA8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6AC4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6AE0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6B00, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6B1C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6B38, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6B54, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6B70, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6B8C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6BA8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6BC4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6BE0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6BFC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6C18, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6C34, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6C50, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6C6C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6C88, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6CA4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6CC0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6CE0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6CFC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6D18, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6D34, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6D50, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6D6C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6D88, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6DA4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6DC0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6DDC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6DFC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6E18, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6E38, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6E54, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6E70, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6E8C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6EA8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6EC4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6EE0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6F04, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6F20, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6F3C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6F58, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6F74, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6F90, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6FAC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6FC8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E6FE4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7000, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E701C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7038, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7054, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7070, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E708C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E70A8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E70C4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E70E0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E70FC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7118, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7140, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E715C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7178, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7194, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E71B4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E71D0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E71F4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7210, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7234, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7250, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E726C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7288, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E72A4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E72C0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E72DC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E72FC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7318, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7334, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7350, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E736C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7388, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E73A4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E73C0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E73DC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E73F8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7414, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7430, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E744C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7468, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7484, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E74A0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E74BC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E74D8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E74F4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7510, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E752C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E754C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7568, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7584, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E75A0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E75BC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E75D8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E75F4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7610, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E762C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7648, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7664, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7680, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E769C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E76B8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E76D4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E76F0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E770C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7728, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7744, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7760, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7780, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E77A0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E77BC, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E77D8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E77F4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7810, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E782C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7848, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7864, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7880, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E789C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E78B8, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E78D4, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E78F0, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E790C, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7928, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E7948, 0x0001, 0x0001 },
    { (const struct SongHeader *)0x087E79E4, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E7A60, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E7BBC, 0x0000, 0x0000 },
    { (const struct SongHeader *)0x087E82B8, 0x0000, 0x0000 },
};

/* ROM 0x087ED910..0x087EDC90; .rodata.007ED910. */
const struct SongHeader *const gSfxSongTable[SOUND_SFX_COUNT] __attribute__((section(".rodata.007ED910"), aligned(1))) =
{
    (const struct SongHeader *)0x087E610C, (const struct SongHeader *)0x087E6128,
    (const struct SongHeader *)0x087E6144, (const struct SongHeader *)0x087E6160,
    (const struct SongHeader *)0x087E617C, (const struct SongHeader *)0x087E6198,
    (const struct SongHeader *)0x087E61B4, (const struct SongHeader *)0x087E61D0,
    (const struct SongHeader *)0x087E61EC, (const struct SongHeader *)0x087E6208,
    (const struct SongHeader *)0x087E6224, (const struct SongHeader *)0x087E6240,
    (const struct SongHeader *)0x087E625C, (const struct SongHeader *)0x087E6278,
    (const struct SongHeader *)0x087E6294, (const struct SongHeader *)0x087E62B0,
    (const struct SongHeader *)0x087E62CC, (const struct SongHeader *)0x087E62E8,
    (const struct SongHeader *)0x087E6304, (const struct SongHeader *)0x087E6320,
    (const struct SongHeader *)0x087E6374, (const struct SongHeader *)0x087E6390,
    (const struct SongHeader *)0x087E6374, (const struct SongHeader *)0x087E6390,
    (const struct SongHeader *)0x087E63AC, (const struct SongHeader *)0x087E63C8,
    (const struct SongHeader *)0x087E63E4, (const struct SongHeader *)0x087E6404,
    (const struct SongHeader *)0x087E6420, (const struct SongHeader *)0x087E643C,
    (const struct SongHeader *)0x087E6458, (const struct SongHeader *)0x087E6474,
    (const struct SongHeader *)0x087E6490, (const struct SongHeader *)0x087E64AC,
    (const struct SongHeader *)0x087E64C8, (const struct SongHeader *)0x087E64E4,
    (const struct SongHeader *)0x087E6500, (const struct SongHeader *)0x087E6520,
    (const struct SongHeader *)0x087E653C, (const struct SongHeader *)0x087E6560,
    (const struct SongHeader *)0x087E6580, (const struct SongHeader *)0x087E65A0,
    (const struct SongHeader *)0x087E65C0, (const struct SongHeader *)0x087E65E0,
    (const struct SongHeader *)0x087E65FC, (const struct SongHeader *)0x087E661C,
    (const struct SongHeader *)0x087E6638, (const struct SongHeader *)0x087E6654,
    (const struct SongHeader *)0x087E6670, (const struct SongHeader *)0x087E668C,
    (const struct SongHeader *)0x087E66A8, (const struct SongHeader *)0x087E66C4,
    (const struct SongHeader *)0x087E66E0, (const struct SongHeader *)0x087E66FC,
    (const struct SongHeader *)0x087E6718, (const struct SongHeader *)0x087E6734,
    (const struct SongHeader *)0x087E6750, (const struct SongHeader *)0x087E676C,
    (const struct SongHeader *)0x087E6788, (const struct SongHeader *)0x087E67A4,
    (const struct SongHeader *)0x087E67C0, (const struct SongHeader *)0x087E67DC,
    (const struct SongHeader *)0x087E67F8, (const struct SongHeader *)0x087E6818,
    (const struct SongHeader *)0x087E6838, (const struct SongHeader *)0x087E6854,
    (const struct SongHeader *)0x087E6870, (const struct SongHeader *)0x087E688C,
    (const struct SongHeader *)0x087E68A8, (const struct SongHeader *)0x087E68C4,
    (const struct SongHeader *)0x087E68E0, (const struct SongHeader *)0x087E68FC,
    (const struct SongHeader *)0x087E6918, (const struct SongHeader *)0x087E6934,
    (const struct SongHeader *)0x087E6950, (const struct SongHeader *)0x087E696C,
    (const struct SongHeader *)0x087E6988, (const struct SongHeader *)0x087E69A4,
    (const struct SongHeader *)0x087E69C0, (const struct SongHeader *)0x087E69DC,
    (const struct SongHeader *)0x087E69F8, (const struct SongHeader *)0x087E6A14,
    (const struct SongHeader *)0x087E6A34, (const struct SongHeader *)0x087E6A50,
    (const struct SongHeader *)0x087E6A6C, (const struct SongHeader *)0x087E6A8C,
    (const struct SongHeader *)0x087E6AA8, (const struct SongHeader *)0x087E6AC4,
    (const struct SongHeader *)0x087E6AE0, (const struct SongHeader *)0x087E6B00,
    (const struct SongHeader *)0x087E6B1C, (const struct SongHeader *)0x087E6B38,
    (const struct SongHeader *)0x087E6B54, (const struct SongHeader *)0x087E6B70,
    (const struct SongHeader *)0x087E6B8C, (const struct SongHeader *)0x087E6BA8,
    (const struct SongHeader *)0x087E6BC4, (const struct SongHeader *)0x087E6BE0,
    (const struct SongHeader *)0x087E6BFC, (const struct SongHeader *)0x087E6C18,
    (const struct SongHeader *)0x087E6C34, (const struct SongHeader *)0x087E6C50,
    (const struct SongHeader *)0x087E6C6C, (const struct SongHeader *)0x087E6C88,
    (const struct SongHeader *)0x087E6CA4, (const struct SongHeader *)0x087E6CC0,
    (const struct SongHeader *)0x087E6CE0, (const struct SongHeader *)0x087E6CFC,
    (const struct SongHeader *)0x087E6D18, (const struct SongHeader *)0x087E6D34,
    (const struct SongHeader *)0x087E6D50, (const struct SongHeader *)0x087E6D6C,
    (const struct SongHeader *)0x087E6D88, (const struct SongHeader *)0x087E6DA4,
    (const struct SongHeader *)0x087E6DC0, (const struct SongHeader *)0x087E6DDC,
    (const struct SongHeader *)0x087E6DFC, (const struct SongHeader *)0x087E6E18,
    (const struct SongHeader *)0x087E6E38, (const struct SongHeader *)0x087E6E54,
    (const struct SongHeader *)0x087E6E70, (const struct SongHeader *)0x087E6E8C,
    (const struct SongHeader *)0x087E6EA8, (const struct SongHeader *)0x087E6EC4,
    (const struct SongHeader *)0x087E6EE0, (const struct SongHeader *)0x087E6F04,
    (const struct SongHeader *)0x087E6F20, (const struct SongHeader *)0x087E6F3C,
    (const struct SongHeader *)0x087E6F58, (const struct SongHeader *)0x087E6F74,
    (const struct SongHeader *)0x087E6F90, (const struct SongHeader *)0x087E6FAC,
    (const struct SongHeader *)0x087E6FC8, (const struct SongHeader *)0x087E6FE4,
    (const struct SongHeader *)0x087E7000, (const struct SongHeader *)0x087E701C,
    (const struct SongHeader *)0x087E7038, (const struct SongHeader *)0x087E7054,
    (const struct SongHeader *)0x087E7070, (const struct SongHeader *)0x087E708C,
    (const struct SongHeader *)0x087E70A8, (const struct SongHeader *)0x087E70C4,
    (const struct SongHeader *)0x087E70E0, (const struct SongHeader *)0x087E70FC,
    (const struct SongHeader *)0x087E7118, (const struct SongHeader *)0x087E7140,
    (const struct SongHeader *)0x087E715C, (const struct SongHeader *)0x087E7178,
    (const struct SongHeader *)0x087E7194, (const struct SongHeader *)0x087E71B4,
    (const struct SongHeader *)0x087E71D0, (const struct SongHeader *)0x087E71F4,
    (const struct SongHeader *)0x087E7210, (const struct SongHeader *)0x087E7234,
    (const struct SongHeader *)0x087E7250, (const struct SongHeader *)0x087E726C,
    (const struct SongHeader *)0x087E7288, (const struct SongHeader *)0x087E72A4,
    (const struct SongHeader *)0x087E72C0, (const struct SongHeader *)0x087E72DC,
    (const struct SongHeader *)0x087E72FC, (const struct SongHeader *)0x087E7318,
    (const struct SongHeader *)0x087E7334, (const struct SongHeader *)0x087E7350,
    (const struct SongHeader *)0x087E736C, (const struct SongHeader *)0x087E7388,
    (const struct SongHeader *)0x087E73A4, (const struct SongHeader *)0x087E73C0,
    (const struct SongHeader *)0x087E73DC, (const struct SongHeader *)0x087E73F8,
    (const struct SongHeader *)0x087E7414, (const struct SongHeader *)0x087E7430,
    (const struct SongHeader *)0x087E744C, (const struct SongHeader *)0x087E7468,
    (const struct SongHeader *)0x087E7484, (const struct SongHeader *)0x087E74A0,
    (const struct SongHeader *)0x087E74BC, (const struct SongHeader *)0x087E74D8,
    (const struct SongHeader *)0x087E74F4, (const struct SongHeader *)0x087E7510,
    (const struct SongHeader *)0x087E752C, (const struct SongHeader *)0x087E754C,
    (const struct SongHeader *)0x087E7568, (const struct SongHeader *)0x087E7584,
    (const struct SongHeader *)0x087E75A0, (const struct SongHeader *)0x087E75BC,
    (const struct SongHeader *)0x087E75D8, (const struct SongHeader *)0x087E75F4,
    (const struct SongHeader *)0x087E7610, (const struct SongHeader *)0x087E762C,
    (const struct SongHeader *)0x087E7648, (const struct SongHeader *)0x087E7664,
    (const struct SongHeader *)0x087E7680, (const struct SongHeader *)0x087E769C,
    (const struct SongHeader *)0x087E76B8, (const struct SongHeader *)0x087E76D4,
    (const struct SongHeader *)0x087E76F0, (const struct SongHeader *)0x087E770C,
    (const struct SongHeader *)0x087E7728, (const struct SongHeader *)0x087E7744,
    (const struct SongHeader *)0x087E7760, (const struct SongHeader *)0x087E7780,
    (const struct SongHeader *)0x087E77A0, (const struct SongHeader *)0x087E77BC,
    (const struct SongHeader *)0x087E77D8, (const struct SongHeader *)0x087E77F4,
    (const struct SongHeader *)0x087E7810, (const struct SongHeader *)0x087E782C,
    (const struct SongHeader *)0x087E7848, (const struct SongHeader *)0x087E7864,
    (const struct SongHeader *)0x087E7880, (const struct SongHeader *)0x087E789C,
    (const struct SongHeader *)0x087E78B8, (const struct SongHeader *)0x087E78D4,
    (const struct SongHeader *)0x087E78F0, (const struct SongHeader *)0x087E790C,
    (const struct SongHeader *)0x087E7928, (const struct SongHeader *)0x087E7928,
    (const struct SongHeader *)0x087E82B8, (const struct SongHeader *)0x087E7948,
};

struct MusicPlayerInfo *const gSfxPlayers[SOUND_SFX_SLOT_COUNT] __attribute__((section(".rodata.007ED910"), aligned(1))) =
{
    (struct MusicPlayerInfo *)&gMPlayInfoSfx0, (struct MusicPlayerInfo *)&gMPlayInfoSfx1,
    (struct MusicPlayerInfo *)&gMPlayInfoSfx2, (struct MusicPlayerInfo *)&gMPlayInfoSfx3,
};

/* End of split read-only data. */
