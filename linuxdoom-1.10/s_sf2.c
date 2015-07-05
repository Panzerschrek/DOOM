#include "doomdef.h"
#include "doomtype.h"

#include <stdlib.h>
#include <stdio.h>

static const char* riff_id = "RIFF";
static const char* list_id = "LIST";

// SF2 structures required 2-byte alignment
#pragma pack (push,2)

typedef struct
{
    char	id[4];
    int		size;
    byte	data[];
} riff_chunk_t;

typedef struct
{
    char	name[20];
    short	preset;
    short	bank;
    short	preset_bank_index;
    int		library;
    int		genre;
    int		morphology;
} preset_header_t;

typedef struct
{
    short	gen_index;
    short	mod_index;
} preset_bag_t;

#pragma pack(pop)


void LoadInfoList(riff_chunk_t* info_list)
{
    riff_chunk_t*	ifil_ck; // version
    riff_chunk_t*	isng_ck; // target sound engine
    riff_chunk_t*	inam_ck; // bank name
    riff_chunk_t*	irom_ck; // sound ROM name
    riff_chunk_t*	iver_ck; // sound ROM version
    riff_chunk_t*	icrd_ck; // date of creation
    riff_chunk_t*	ieng_ck; // sound designers and engineers
    riff_chunk_t*	iprd_ck; // product
    riff_chunk_t*	icop_ck; // copyright
    riff_chunk_t*	icmt_ck; // comments
    riff_chunk_t*	isft_ck; // tools

    byte*		file_pos = info_list->data + 4;

    ifil_ck = (riff_chunk_t*) file_pos; file_pos += ifil_ck->size + sizeof(riff_chunk_t);
    isng_ck = (riff_chunk_t*) file_pos; file_pos += isng_ck->size + sizeof(riff_chunk_t);
    inam_ck = (riff_chunk_t*) file_pos; file_pos += inam_ck->size + sizeof(riff_chunk_t);
    irom_ck = (riff_chunk_t*) file_pos; file_pos += irom_ck->size + sizeof(riff_chunk_t);
    iver_ck = (riff_chunk_t*) file_pos; file_pos += iver_ck->size + sizeof(riff_chunk_t);
    icrd_ck = (riff_chunk_t*) file_pos; file_pos += icrd_ck->size + sizeof(riff_chunk_t);
    ieng_ck = (riff_chunk_t*) file_pos; file_pos += ieng_ck->size + sizeof(riff_chunk_t);
    iprd_ck = (riff_chunk_t*) file_pos; file_pos += iprd_ck->size + sizeof(riff_chunk_t);
    icop_ck = (riff_chunk_t*) file_pos; file_pos += icop_ck->size + sizeof(riff_chunk_t);
    icmt_ck = (riff_chunk_t*) file_pos; file_pos += icmt_ck->size + sizeof(riff_chunk_t);
    isft_ck = (riff_chunk_t*) file_pos; file_pos += isft_ck->size + sizeof(riff_chunk_t);

    return;
}

void LoadSdata(riff_chunk_t* sdata_list)
{
    char* sdta;

    sdta = (char*) sdata_list->data;
}

void LoadPdata(riff_chunk_t* sdata_list)
{
    riff_chunk_t*	phdr_ck; // present headers
    riff_chunk_t*	pbag_ck; // present index list
    riff_chunk_t*	pmod_ck; // present modulator list
    riff_chunk_t*	pgen_ck; // present generator list
    riff_chunk_t*	inst_ck; // instrument names and indeces
    riff_chunk_t*	ibag_ck; // instrument index list
    riff_chunk_t*	imod_ck; // instrument modulator list
    riff_chunk_t*	igen_ck; // instrument generator list
    riff_chunk_t*	shdr_ck; // sample headers

    byte* file_pos = sdata_list->data + 4;

    phdr_ck = (riff_chunk_t*) file_pos; file_pos += phdr_ck->size + sizeof(riff_chunk_t);
    pbag_ck = (riff_chunk_t*) file_pos; file_pos += pbag_ck->size + sizeof(riff_chunk_t);
    pmod_ck = (riff_chunk_t*) file_pos; file_pos += pmod_ck->size + sizeof(riff_chunk_t);
    pgen_ck = (riff_chunk_t*) file_pos; file_pos += pgen_ck->size + sizeof(riff_chunk_t);
    inst_ck = (riff_chunk_t*) file_pos; file_pos += inst_ck->size + sizeof(riff_chunk_t);
    ibag_ck = (riff_chunk_t*) file_pos; file_pos += ibag_ck->size + sizeof(riff_chunk_t);
    imod_ck = (riff_chunk_t*) file_pos; file_pos += imod_ck->size + sizeof(riff_chunk_t);
    igen_ck = (riff_chunk_t*) file_pos; file_pos += igen_ck->size + sizeof(riff_chunk_t);
    shdr_ck = (riff_chunk_t*) file_pos; file_pos += shdr_ck->size + sizeof(riff_chunk_t);

    preset_header_t*	presets;
    int			preset_count;

    preset_count = phdr_ck->size / sizeof(preset_header_t);
    presets = (preset_header_t*) phdr_ck->data;

    preset_bag_t*	presets_bags;
    int			preset_bags_count;

    preset_bags_count = pbag_ck->size / sizeof(preset_bag_t);
    presets_bags = (preset_bag_t*) pbag_ck->data;

    return;
}

void LoadSoundFont()
{
    FILE*		handle;
    byte*		file_data;
    byte*		file_pos;
    int			file_size;

    riff_chunk_t*	file_header;
    char*		sfbk;
    riff_chunk_t*	info_list;
    riff_chunk_t*	sdata_list;
    riff_chunk_t*	pdata_list;

    handle = fopen( "TimGM6mb.sf2", "rb" );

    fseek( handle, 0, SEEK_END );
    file_size = ftell( handle );
    fseek( handle, 0, SEEK_SET );
    file_data = malloc( file_size );
    fread( file_data, 1, file_size, handle );
    fclose(handle);

    file_header = (riff_chunk_t*) file_data;
    sfbk = (char*) (file_data + 8);

    file_pos = file_data + 12;

    info_list = (riff_chunk_t*) file_pos;
    file_pos += info_list->size + sizeof(riff_chunk_t);

    sdata_list = (riff_chunk_t*) file_pos;
    file_pos += sdata_list->size + sizeof(riff_chunk_t);

    pdata_list = (riff_chunk_t*) file_pos;

    LoadInfoList(info_list);
    LoadSdata(sdata_list);
    LoadPdata(pdata_list);

   return;
}
