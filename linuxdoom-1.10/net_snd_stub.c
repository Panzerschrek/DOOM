
#include <SDL.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"


#define MAX_CHANNELS 32

typedef short sample_t;

typedef struct
{
    int		id; // id for sound, 0 - not initialized

    byte*	src_data;
    int		length; // sample count of source data
    int		pos; // position in destination buffer ( in destination values )
    fixed_t	pitch;
    fixed_t	volume;
} snd_channel_t;

static int snd_id = 1; // 0 is reserved

snd_channel_t channels[MAX_CHANNELS];

struct
{
    SDL_AudioSpec format;
    int device_id;
    SDL_mutex* mutex;
} sdl_audio;


static int nearestpotfloor(int x)
{
    int r = 1<<30;
    while(r > x) r>>= 1;
    return r;
}

static int swapshort(short x)
{
    return ((x>>8)&0x0F) | (x<<8);
   // return x;
}

void SDLCALL AudioCallback (void *userdata, Uint8 * stream, int len)
{
    int			i;
    int			j;
    int			k;
    int 		len_in_samples;
    sample_t*		data;
    snd_channel_t*	channel;

    SDL_LockMutex(sdl_audio.mutex);

    data = (sample_t*) stream;
    len_in_samples = len / sizeof(sample_t);

    for ( i = 0; i < len_in_samples; i++ ) data[i] = 0;

    for ( i = 0; i < MAX_CHANNELS; i++ )
    {
	channel = &channels[i];
	if (channel->id == 0) continue;

	for ( j = channel->pos, k = 0; j < channel->length && k < len_in_samples; k++, j++ )
	    data[k] += (channel->src_data[j] - 127 );

	channel->pos += len_in_samples;
	if ( channel->pos >= channel->length ) channel->id = 0; // reset channel
    }

    for ( i = 0; i < len_in_samples; i++ )
    {
	k = data[i] << 7;
	if( k > 32767 ) k = 32767;
	else if( k < -32768 ) k = -32768;

	data[i] = k;
    }

    SDL_UnlockMutex(sdl_audio.mutex);
}

void I_InitSound()
{
    SDL_AudioSpec	audio_format;
    const char*		device_name;
    int			i;
    int			num_devices;

    SDL_InitSubSystem( SDL_INIT_AUDIO );


    audio_format.channels = 1;
    audio_format.freq = 11025;
    audio_format.format = AUDIO_S16;
    audio_format.silence = 0;
    audio_format.callback = AudioCallback;

    audio_format.samples = nearestpotfloor(audio_format.freq / TICRATE);

    sdl_audio.device_id = 0;
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

    sdl_audio.mutex = SDL_CreateMutex();

    for( i = 0; i < MAX_CHANNELS; i++ ) channels[i].id = 0;

    SDL_PauseAudioDevice(sdl_audio.device_id , 0);
}

void I_UpdateSound()
{

}

void I_ShutdownSound()
{
    SDL_AudioQuit();
    SDL_DestroyMutex(sdl_audio.mutex);
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
    int			i;
    int			freeslot;
    snd_channel_t*	channel;
    sfxinfo_t*		info;

    SDL_LockMutex( sdl_audio.mutex );

    freeslot = -1;
    for( i = 0; i < MAX_CHANNELS; i++ )
    	if ( channels[i].id == 0 )
    	{
	    freeslot = i;
	    break;
    	}

    if( freeslot == -1 ) return 0;

    channel = &channels[ freeslot ];

    channel->id = snd_id;
    snd_id++;

    channel->pos = 0;
    channel->pitch = 1 << FRACBITS;
    channel->volume = (vol << FRACBITS) / 128;

    info = &S_sfx[id];
    info->data = W_CacheLumpNum(info->lumpnum, PU_SOUND) + sizeof(lumpinfo_t);
    info->usefulness++;

    channel->src_data = info->data;
    channel->length = W_LumpLength(info->lumpnum);

    SDL_UnlockMutex( sdl_audio.mutex );
    return channel->id;
}

int I_SoundIsPlaying(int handle)
{
    int i;

    SDL_LockMutex( sdl_audio.mutex );

    for( i = 0; i < MAX_CHANNELS; i++ )
	if(channels[i].id == handle)
	{
	    SDL_UnlockMutex( sdl_audio.mutex );
	    return true;
	}

    SDL_UnlockMutex( sdl_audio.mutex );
    return false;
}

void I_StopSound(int handle)
{
    int i;

    SDL_LockMutex( sdl_audio.mutex );

    for( i = 0; i < MAX_CHANNELS; i++ )
	if(channels[i].id == handle)
	{
	    channels[i].id = 0;
	    break;
	}

    SDL_UnlockMutex( sdl_audio.mutex );
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
