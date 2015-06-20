
#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"


void I_InitSound()
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
	return true;
}

void I_StopSound(int handle)
{
}

void I_PlaySong
( int		handle,
  int		looping )
{
}

int I_RegisterSong(void *data)
{
	return 1;
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

void I_InitNetwork()
{
}

void I_NetCmd()
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

int I_GetSfxLumpNum (sfxinfo_t* sfxinfo )
{
}
