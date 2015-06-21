
#include <SDL.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"

struct
{
    SDL_AudioSpec format;
    int device_id;
} sdl_audio;

void SDLCALL * AudioCallback (void *userdata, Uint8 * stream, int len)
{
}

void I_InitSound()
{
    SDL_AudioSpec	audio_format;
    char*		device_name;
    int			i;
    int			num_devices;
    int			device_id;

    SDL_InitSubSystem( SDL_INIT_AUDIO );


    audio_format.channels = 1;
    audio_format.freq = 22050;
    audio_format.format = AUDIO_S16;
    audio_format.silence = 0;
    audio_format.samples = 16384;
    audio_format.callback = AudioCallback;

    device_id = 0;
    num_devices = SDL_GetNumAudioDevices(0);
    for( i = 0; i < num_devices; i++ )
    {
	device_name = SDL_GetAudioDeviceName(0, 0);
	sdl_audio.device_id = SDL_OpenAudioDevice(device_name, 0, &audio_format, &sdl_audio.format, 0);
	if (sdl_audio.device_id >= 2)
	{
	    printf( "I_InitSound: Open audio device: %s\n", device_name );
	    break;
	}
    }

    if (sdl_audio.device_id < 2)
    {
	printf( "I_InitSound: Could not open audio device\n" );
	return;
    }
}

void I_ShutdownSound()
{
    SDL_AudioQuit();
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
    doomcom = malloc (sizeof (*doomcom) );
    memset (doomcom, 0, sizeof(*doomcom) );

    doomcom-> ticdup = 1;
    doomcom-> extratics = 0;

    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
    return;
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
