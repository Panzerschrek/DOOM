
#include <SDL.h>

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

typedef short sample_t;
typedef int fixed8_t;

typedef struct
{
    int		id; // id for sound, 0 - not initialized

    byte*	src_data;
    fixed8_t	length; // sample count of source data
    fixed8_t	pos; // position in source buffer
    fixed8_t	fetch_step;
    int		volume[2]; // in range 0 - (1<<MAX_VOLUME_LOG2)
} snd_channel_t;

static int snd_id = 1; // 0 is reserved

snd_channel_t channels[MAX_CHANNELS];

struct
{
    SDL_AudioSpec	format;
    int			device_id;

    int*		mixbuffer;
    SDL_mutex*		mutex;
} sdl_audio;


static int nearestpotfloor(int x)
{
    int r = 1<<30;
    while(r > x) r>>= 1;
    return r;
}

static void genchannelvolume(snd_channel_t* channel, int vol, int sep)
{
    // input volume is in range [0; 15 * S_VOLUME_SCALER],  separation - [0; 255]
    // output range [0; 256]
    channel->volume[0] = (vol * sep) / (15*S_VOLUME_SCALER);
    channel->volume[1] = (vol * (256-sep)) / (15*S_VOLUME_SCALER);
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
    len_in_samples = len / ( sizeof(sample_t) * sdl_audio.format.channels );

    for ( i = 0; i < len_in_samples * sdl_audio.format.channels; i++ ) sdl_audio.mixbuffer[i] = 0;

    for ( i = 0; i < MAX_CHANNELS; i++ )
    {
	channel = &channels[i];
	if (channel->id == 0) continue;

	for ( j = channel->pos, k = 0; j < channel->length && k < len_in_samples; k++, j+= channel->fetch_step )
	{
	    int mul_k = j & 0xFF;
	    int coord = j >> 8;
	    int src_val = (channel->src_data[coord] * (256 - mul_k) + channel->src_data[coord + 1] * mul_k - (127<<8) );
	    sdl_audio.mixbuffer[k*2] += src_val * channel->volume[1];
	    sdl_audio.mixbuffer[k*2+1] += src_val * channel->volume[0];
	}

	channel->pos = j;
	if ( channel->pos >= channel->length ) channel->id = 0; // reset channel
    }

    for ( i = 0; i < len_in_samples * sdl_audio.format.channels; i++ )
    {
	k = sdl_audio.mixbuffer[i] >> MAX_VOLUME_LOG2;
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


    audio_format.channels = 2;
    audio_format.freq = 22050;
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
    sdl_audio.mixbuffer = malloc( 4 * sizeof(sample_t) * sdl_audio.format.samples * sdl_audio.format.channels );

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
    free(sdl_audio.mixbuffer);
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
    genchannelvolume( channel, vol, sep );

    info = &S_sfx[id];
    if (!info->data) info->data = W_CacheLumpNum(info->lumpnum, PU_SOUND) + sizeof(lumpinfo_t);
    info->usefulness++;

    channel->src_data = info->data;
    // Cut 1 byte, because we can linear interpolation of src sound
    channel->length = (W_LumpLength(info->lumpnum) - sizeof(lumpinfo_t) - 1) << 8;
    channel->fetch_step = (ID_SAMPLE_RATE << 8) / sdl_audio.format.freq;

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

void
I_UpdateSoundParams
( int	handle,
  int	vol,
  int	sep,
  int	pitch)
{
    int		i;

    SDL_LockMutex( sdl_audio.mutex );

    for( i = 0; i < MAX_CHANNELS; i++ )
    {
    	if (channels[i].id == handle)
	{
	    genchannelvolume( &channels[i], vol, sep );
    	    break;
	}
    }

    SDL_UnlockMutex( sdl_audio.mutex );
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
