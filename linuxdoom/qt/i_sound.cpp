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

extern "C"
{

#include <math.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"

void I_MixMusic( int len );

} // extern "C"

#include <QtMultimedia/QAudioOutput>

#define MAX_CHANNELS 32
#define ID_SAMPLE_RATE 11025
#define MAX_VOLUME_LOG2 8

#define MUS_MAX_CHANNELS 16
#define MUS_TICKS_PER_SEC 140
#define MUS_MAX_NOTES 128


// note_frequency * 2 * pi / sample_rate
float notes_freq[MUS_MAX_NOTES];

typedef short sample_t;
typedef int fixed8_t;

typedef struct
{
	char	ID[4];          // identifier "MUS" 0x1A
	short	scoreLen;
	short	scoreStart;
	short	channels;	// count of primary channels
	short	sec_channels;	// count of secondary channels
	short	instrCnt;
	short	dummy;
	// variable-length part starts here
	short	instruments[];
} mus_header_t;

typedef struct mus_note_s
{
	int			number;
	boolean		is_active;
	int			position; // position in samples
	int			volume; // in range [0; 127]
	byte		instrument;

	struct mus_note_s*	next;
	struct mus_note_s*	prev;
} mus_note_t;

typedef struct
{
	mus_note_t		notes[MUS_MAX_NOTES];
	int			current_instrument;
	int			current_volume; // in range [0; 127]

} mus_channel_t;


struct
{
	mus_channel_t	channels[MUS_MAX_CHANNELS];
	mus_note_t*		first_playing_note;
	int			master_volume; // in range [0; 127]
	int 		user_volume; // in range [0; 15]

	boolean		is_playing;
	boolean		looping;

	 // in samples
	int			position;
	int			next_event_position;

	int			current_event_tick;

	byte*		music_data;
	byte*		mus_position;

} current_music;

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
static int mus_id = 1; // 0 is reserved

snd_channel_t channels[MAX_CHANNELS];

static std::vector<int> mix_buffer(65536);
static std::vector<int> music_mix_buffer(65536);
static int g_sample_rate = 0;

static void genchannelvolume(snd_channel_t* channel, int vol, int sep)
{
	// input volume is in range [0; 15 * S_VOLUME_SCALER],  separation - [0; 255]
	// output range [0; 256]
	channel->volume[0] = (vol * sep) / (15*S_VOLUME_SCALER);
	channel->volume[1] = (vol * (256-sep)) / (15*S_VOLUME_SCALER);
}

void StartNote( mus_channel_t* channel, int n, int volume )
{
	mus_note_t* note = &channel->notes[n];

	mus_note_t* first_note_in_list = current_music.first_playing_note;

	if (first_note_in_list) first_note_in_list->prev = note;
	current_music.first_playing_note = note;

	note->next = first_note_in_list;
	note->prev = NULL;

	note->volume = volume;
	note->position = 0;
	note->is_active = 1;
	note->number = n;
}

void StopNote( mus_channel_t* channel, int n )
{
	mus_note_t* note = &channel->notes[n];
	mus_note_t* prev = note->prev;
	mus_note_t* next = note->next;

	if (note == current_music.first_playing_note) current_music.first_playing_note = note->next;

	if( prev ) prev->next = next;
	if( next ) next->prev = prev;

	note->is_active = 0;
}

void ResetMusic()
{
	current_music.mus_position = current_music.music_data;
	current_music.position = 0;
	current_music.next_event_position = 0;
	current_music.master_volume = 0;
	current_music.current_event_tick = 0;
	current_music.first_playing_note = NULL;
}

static QAudioOutput* g_audio_output= nullptr;

class SoundIODevice : public QIODevice
{
public:
	virtual qint64 readData(char *stream, qint64 len)
	{
		int			i;
		int			j;
		int			k;
		int 		len_in_samples;
		sample_t*		data;
		snd_channel_t*	channel;

		const int channel_count = 2;

		data = (sample_t*) stream;
		len_in_samples = len / ( sizeof(sample_t) * channel_count );

		I_MixMusic( len_in_samples );

		for ( i = 0; i < len_in_samples * channel_count; i++ )
			mix_buffer[i] = 0;

		for ( i = 0; i < MAX_CHANNELS; i++ )
		{
			channel = &channels[i];
			if (channel->id == 0) continue;

			for ( j = channel->pos, k = 0; j < channel->length && k < len_in_samples; k++, j+= channel->fetch_step )
			{
				int mul_k = j & 0xFF;
				int coord = j >> 8;
				int src_val = (channel->src_data[coord] * (256 - mul_k) + channel->src_data[coord + 1] * mul_k - (127<<8) );
				mix_buffer[k*2] += src_val * channel->volume[1];
				mix_buffer[k*2+1] += src_val * channel->volume[0];
			}

			channel->pos = j;
			if ( channel->pos >= channel->length ) channel->id = 0; // reset channel
		}

		for ( i = 0; i < len_in_samples * channel_count; i++ )
		{
			k = mix_buffer[i] >> MAX_VOLUME_LOG2;
			k+= music_mix_buffer[i];

			if( k > 32767 ) k = 32767;
			else if( k < -32768 ) k = -32768;

			data[i] = k;
		}

		return len;
	}
	virtual qint64 readLineData(char *data, qint64 maxlen)
	{
		return readData(data, maxlen);
	}
	virtual qint64 writeData(const char *data, qint64 len){return 0;}
private:
};

static SoundIODevice* g_io_device= nullptr;

extern "C" void I_MixMusic( int len )
{
	int i;

	for( i = 0; i < len * 2; i++ )
		music_mix_buffer[i] = 0;

	if (!current_music.is_playing) return;

	i = 0;
	do
	{
		float master_volume_f = (float)(current_music.master_volume * current_music.user_volume ) / 15.0f;
		if (master_volume_f > 0.0f)
			for( ; current_music.position < current_music.next_event_position && i < len; i++, current_music.position++ )
			{
				float pos_f = (float) current_music.position;
				float val = 0;

				mus_note_t* note = current_music.first_playing_note;
				while( note )
				{
					val += ((float)note->volume) * sin( pos_f * notes_freq[ note->number ] );
					note = note->next;
				}
				// channel volume [0; 127]
				// note volume [0; 127]
				// result volume - [0; 16129]
				music_mix_buffer[i*2  ] += (int)(val * master_volume_f);
				music_mix_buffer[i*2+1] += (int)(val * master_volume_f);
			}
		else // music volume iz zero - just skip samples
		{
			int samples_read = current_music.next_event_position - current_music.position;
			int samples_write = len - i;
			int samples_left = samples_read < samples_write ? samples_read : samples_write;
			current_music.position += samples_left;
			i += samples_left;
		}

		if( current_music.position == current_music.next_event_position )
		{
			int ticks_to_next_event;
			int event = *current_music.mus_position;
			int event_type = (event & 0x70) >> 4;
			int channel_num = event & 0x0F;
			mus_channel_t* channel = & current_music.channels[channel_num];
			current_music.mus_position++;

			switch (event_type)
			{
			case 0: // release note
				{
				int note_number = *current_music.mus_position & 127;
				current_music.mus_position++;
				if (channel_num != 15 ) StopNote( channel, note_number );
				}
				break;

			case 1: // play note
				{
					int volume;
					int note_number = *current_music.mus_position & 127;
					int is_volume = *current_music.mus_position & 128;
					current_music.mus_position++;
					if(is_volume)
					{
						volume = *current_music.mus_position & 127;
						current_music.mus_position++;
						channel->current_volume = volume;
					}
					else volume = channel->current_volume;
					if (channel_num != 15 ) StartNote( channel, note_number, volume );
				}
				break;

			case 2: // pitch wheel
				current_music.mus_position++; // pitch wheel value
				break;

			case 3: // system event
				current_music.mus_position++; // system event number
				break;

				case 4: // change controller
				{
					int controller_number, controller_value;
					controller_number = *current_music.mus_position;
					current_music.mus_position++;
					controller_value = *current_music.mus_position;
					current_music.mus_position++;
					if (controller_number == 3) // set volume. controller_value is volume
					current_music.master_volume = controller_value & 127;
					else if (controller_number == 4 ) // balance value
					{}
				}
				break;

			case 5: // unknown
				break;

			case 6: // score end
				ResetMusic();
				if (!current_music.looping)
				{
					current_music.is_playing = false;
					return;
				}
				break;

			case 7: // unknown
				break;

				default:
				break;
			};

			ticks_to_next_event = 0;
			if (event & 128)
			{
				byte time_byte;
				do
				{
					time_byte = *current_music.mus_position;
					current_music.mus_position++;
					ticks_to_next_event = ticks_to_next_event * 128 + (time_byte & 127);
				}while( time_byte & 128);
			}

			current_music.current_event_tick += ticks_to_next_event;
			current_music.next_event_position =
			current_music.current_event_tick *
			g_sample_rate /
			MUS_TICKS_PER_SEC;
		} // if new event
	} while( i < len );
}

void I_InitNoteTable()
{
	int i;

	// note A4
	const float base_freq = 440.0f;
	const int base_freq_note = 12 * 4 + 9;

	float mul = pow( 2.0f, 1.0f / 12.0f );

	for( i = 0; i< MUS_MAX_NOTES; i++ )
	{
		notes_freq[i] = base_freq * pow( mul, (float)(i - base_freq_note) );
		notes_freq[i] *= (2.0f * 3.1415926535f) / ((float)g_sample_rate);
	}
}

extern "C" void I_InitSound()
{
	g_sample_rate = 22050;

	for( int i = 0; i < MAX_CHANNELS; i++ ) channels[i].id = 0;

	I_InitNoteTable();
	current_music.is_playing = 0;

	QAudioFormat format;
	format.setSampleRate(g_sample_rate);
	format.setChannelCount(2);
	format.setSampleSize(16);
	format.setCodec("audio/pcm");
	format.setByteOrder(QAudioFormat::LittleEndian);
	format.setSampleType(QAudioFormat::SignedInt);

	g_audio_output = new QAudioOutput(format);
	g_io_device = new SoundIODevice();
	g_io_device->open(QIODevice::ReadOnly);
	g_audio_output->start(g_io_device);
}

extern "C" void I_UpdateSound()
{
}

extern "C" void I_ShutdownSound()
{
	if( g_audio_output != nullptr )
	{
		delete g_audio_output;
		g_audio_output = nullptr;
	}
	if( g_io_device != nullptr )
	{
		delete g_io_device;
		g_io_device = nullptr;
	}
}

extern "C" void I_ShutdownMusic()
{
}

extern "C" void I_SetChannels()
{
}

extern "C" int I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority )
{
	const int	c_lump_bytes_cut = 8; // header or something else, but beginning butes need cut
	int			i;
	int			freeslot;
	snd_channel_t*	channel;
	sfxinfo_t*		info;

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
	if (!info->data) info->data = ((char*)W_CacheLumpNum(info->lumpnum, PU_SOUND)) + c_lump_bytes_cut;
	info->usefulness++;

	channel->src_data = (byte*)info->data;
	// Cut 1 byte, because we can linear interpolation of src sound
	channel->length = (W_LumpLength(info->lumpnum) - c_lump_bytes_cut - 1) << 8;
	channel->fetch_step = (ID_SAMPLE_RATE << 8) / g_sample_rate;

	return channel->id;
}

extern "C" int I_SoundIsPlaying(int handle)
{
	int i;

	for( i = 0; i < MAX_CHANNELS; i++ )
	if(channels[i].id == handle)
	{
		return true;
	}

	return false;
}

extern "C" void I_StopSound(int handle)
{
	int i;

	for( i = 0; i < MAX_CHANNELS; i++ )
		if(channels[i].id == handle)
		{
			channels[i].id = 0;
			break;
		}
}

extern "C" void
I_UpdateSoundParams
( int	handle,
  int	vol,
  int	sep,
  int	pitch)
{	
	int i;
	for( i = 0; i < MAX_CHANNELS; i++ )
	{
		if (channels[i].id == handle)
		{
			genchannelvolume( &channels[i], vol, sep );
			break;
		}
	}
}

//
// Retrieve the raw data lump index
//  for a given SFX name.
//
extern "C" int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

extern "C" int I_RegisterSong(void *data)
{
	mus_header_t* mus;

	mus = (mus_header_t*)data;

#if 0
	int		i;

	byte* d = ((byte*)data) + mus->scoreStart;
	byte* end = d + mus->scoreLen;
	for( i = 0; i < mus->instrCnt; i++ )
	printf( "instrument: %d\n", mus->instruments[i] );

	int notes[256];
	memset( notes, 0, sizeof(notes) );
	while(d < end)
	{
	byte event = *d;
	d++;
	int event_type = (event & 0x70) >> 4;
	int channel = event & 0x0F;

	switch(event_type)
	{
	case 0: // release note
	{
		int note_number = *d & 127;
		printf( "r: %d ch: %d\n", note_number, channel );
		notes[ note_number ] -- ;
		d++;
	}
		break;
	case 1: // play note
	{
		int note_number = *d & 127;
		printf( "p: %d ch: %d\n", note_number, channel );
		int is_volume = *d & 128;
		notes[ note_number ] ++ ;
		d++;
		if(is_volume) d++; // volume
	}
		break;
	case 2: // pitch wheel
		printf( "pitch wheel\n" );
		d++; // pitch wheel value
		break;
	case 3: // system event
		printf( "system event\n" );
		d++; // system event number
		break;
	case 4: // change controller
	{
		int controller_number = *d;
		d++;
		int controller_value = *d;
		d++;
		if (controller_number == 0 ) printf( "select instrument: %d ch: %d\n", controller_value, channel );
		else printf( "change controller. cont: %d value: %d\n", controller_number, controller_value );
	}
		break;
	case 5: // unknown
		printf( "unknwn5 ");
		break;
	case 6: // score end
		printf( "mus end. delta: %d\n", end - d );
		d++;
		break;
	case 7: // unknown
		printf( "unknwn7 ");
		break;
	default:
		break;
	};

	int time = 0;
	if (event & 128)
	{
		byte time_byte;
		do
		{
		time_byte = *d;
		d++;
		time = time * 128 + (time_byte&127);
		}while( time_byte & 128);
		//printf( "t: %d ", time );
		}
	} // while not end of mus

	for( i = 0; i < 256; i++ ) printf( "%d ", notes[i] );
#endif

	current_music.music_data = ((byte*)data) + mus->scoreStart;

	return mus_id++;
}

extern "C" void I_PlaySong
( int		handle,
  int		looping )
{
	ResetMusic();
	current_music.is_playing = true;
	current_music.looping = looping;
}

extern "C" void I_UnRegisterSong(int handle)
{
}

extern "C" void I_PauseSong (int handle)
{
	current_music.is_playing = false;
}

extern "C" void I_ResumeSong (int handle)
{
	current_music.is_playing = true;
}

extern "C" void I_StopSong(int handle)
{
	ResetMusic();
	current_music.is_playing = false;
}

extern "C" void I_SetMusicVolume(int volume)
{
	current_music.user_volume = volume;
}

extern "C" void I_SubmitSound()
{
}
