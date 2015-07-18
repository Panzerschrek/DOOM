#include "../doomdata.h"
#include "../z_zone.h"
#include "../w_wad.h"
#include "../m_swap.h"
#include "../i_system.h"
#include "../p_setup.h"
#include "rp_defs.h"


static char*		c_textures_list_lump_names[2] = { "TEXTURE1", "TEXTURE2" };

static wall_textue_t*	g_wall_textues;
static int		g_wall_textures_count;

static flat_texture_t*	g_flat_textures;
static int		g_flat_textures_count;
static int		g_first_flat;

static pixel_t		g_textues_palette[256];

// game uses this directly.
extern int*		flattranslation;
extern int*		texturetranslation;

static void R_32b_InitPalette()
{
    int		i;
    int		j;
    byte*	playpal;

    playpal = W_CacheLumpName ("PLAYPAL",PU_CACHE);

    for( i = 0, j = 0; i < 256; i++, j+=3 )
    {
	g_textues_palette[i].components[0] = playpal[j ];
	g_textues_palette[i].components[1] = playpal[j+1];
	g_textues_palette[i].components[2] = playpal[j+2];
    }

}

static void RP_InitWallsTextures()
{
    int*		maptex_data[2];
    int			maptex_count[2];
    int			tex_list_count;
    int			i;
    int			j;
    wall_textue_t*	tex;
    maptexture_t*	maptex;
    int*		offsets;

    g_wall_textures_count = 0;

    tex_list_count = W_CheckNumForName (c_textures_list_lump_names[1]) == -1 ? 1 : 2;

    for ( i = 0; i < tex_list_count; i++ )
    {
	maptex_data[i] = W_CacheLumpName (c_textures_list_lump_names[i], PU_STATIC);
	maptex_count[i] = LONG(*(maptex_data[i]));
	g_wall_textures_count += maptex_count[i];
    }

    g_wall_textues = Z_Malloc( sizeof(wall_textue_t) * g_wall_textures_count, PU_STATIC, 0 );
    texturetranslation = Z_Malloc( sizeof(int) * g_wall_textures_count, PU_STATIC, 0 );

    tex = g_wall_textues;
    for ( i = 0; i < tex_list_count; i++ )
    {
	offsets = maptex_data[i] + 1;
	for( j = 0; j < maptex_count[i]; j++, tex++ )
	{
	    maptex = (maptexture_t*) (((byte*)maptex_data[i]) + LONG(offsets[j]));
	    memcpy( tex->name, maptex->name, 8 );
	    tex->width  = LONG(maptex->width );
	    tex->height = LONG(maptex->height);
	    tex->raw_data = 0;
	}
    }

    for ( i = 0; i < g_wall_textures_count ; i++ )
	texturetranslation[i] = i;
}

void RP_InitFlatsTextures()
{
    int		i;
    int		last_flat;

    g_first_flat = W_GetNumForName ("F_START") + 1;
    last_flat    = W_GetNumForName ("F_END")   - 1;
    g_flat_textures_count = last_flat - g_first_flat + 1;

    g_flat_textures = Z_Malloc( sizeof(flat_texture_t) * g_flat_textures_count, PU_STATIC, 0 );
    flattranslation = Z_Malloc( sizeof(int) * g_flat_textures_count, PU_STATIC, 0 );

    for ( i = 0; i < g_flat_textures_count; i++ )
	g_flat_textures[i].raw_data = 0;

    for ( i = 0; i < g_flat_textures_count; i++ )
	flattranslation[i] = i;
}

void R_32b_PrecacheLevel()
{

}

void R_32b_InitData ()
{
    R_32b_InitPalette();
    RP_InitWallsTextures();
    RP_InitFlatsTextures();
}

int R_32b_FlatNumForName(char* name)
{
    int		i;
    char	namet[9];

    i = W_CheckNumForName (name);

    if (i == -1)
    {
	namet[8] = 0;
	memcpy (namet, name,8);
	I_Error ("R_FlatNumForName: %s not found",namet);
    }
    return i - g_first_flat;
}

int R_32b_CheckTextureNumForName(char* name)
{
    int		i;

    // "NoTexture" marker.
    if (name[0] == '-')
	return 0;

    for (i=0 ; i<g_wall_textures_count ; i++)
	if (!strncasecmp (g_wall_textues[i].name, name, 8) )
	    return i;

    return -1;
}

int R_32b_TextureNumForName(char* name)
{
    int		i;

    i = R_32b_CheckTextureNumForName (name);

    if (i==-1)
    {
	I_Error ("R_TextureNumForName: %s not found",
		 name);
    }
    return i;
}
