// Copyright (C) 2015 by Artöm "Panzerschrek" Kunç.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.


#include <math.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"


#define MAX_CHANNELS 32
#define ID_SAMPLE_RATE 11025
#define MAX_VOLUME_LOG2 8

#define MUS_MAX_CHANNELS 16
#define MUS_TICKS_PER_SEC 140
#define MUS_MAX_NOTES 128

void I_MixMusic( int len )
{
}

void I_InitSound()
{
}

void I_UpdateSound()
{
}

void I_ShutdownSound()
{
}

void I_ShutdownMusic()
{
}

void I_SetChannels()
{
}

int I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority )
{
}

int I_SoundIsPlaying(int handle)
{
	return 0;
}

void I_StopSound(int handle)
{
}

void
I_UpdateSoundParams
( int	handle,
  int	vol,
  int	sep,
  int	pitch)
{
}

//
// Retrieve the raw data lump index
//  for a given SFX name.
//
int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_RegisterSong(void *data)
{
	static int mus_id= 0;
    return mus_id++;
}

void I_PlaySong
( int		handle,
  int		looping )
{
}

void I_UnRegisterSong(int handle)
{
}

void I_PauseSong (int handle)
{
}

void I_ResumeSong (int handle)
{
}

void I_StopSong(int handle)
{
}

void I_SetMusicVolume(int volume)
{
}

void I_SubmitSound()
{
}
