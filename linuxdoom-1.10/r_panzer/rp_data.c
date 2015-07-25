#include <stdlib.h>

#include "../doomdata.h"
#include "../z_zone.h"
#include "../w_wad.h"
#include "../m_swap.h"
#include "../i_system.h"
#include "../p_setup.h"
#include "../r_defs.h"
#include "rp_defs.h"

typedef struct wall_texture_patch_s
{
    int		origin_x;
    int		origin_y;
    int		lump_num;
} wall_texture_patch_t;

typedef struct wall_texture_info_s
{
    wall_texture_patch_t*	patches; // pointer to common array of all patches
    int				patch_count;
} wall_texture_info_t;

static char*		c_textures_list_lump_names[2] = { "TEXTURE1", "TEXTURE2" };

static wall_texture_t*		g_wall_textures;
static wall_texture_patch_t*	g_wall_textures_patches;
static wall_texture_info_t*	g_wall_textures_info;
static int			g_wall_textures_count;

static flat_texture_t*	g_flat_textures;
static int		g_flat_textures_count;
static int		g_first_flat; // lump number

static pixel_t		g_textures_palette[256];

// game uses this directly.
extern int*		flattranslation;
extern int*		texturetranslation;

extern side_t*		sides;
extern int		numsides;

extern sector_t*	sectors;
extern int		numsectors;


static void BuildFlatMip(const pixel_t* in_texture, pixel_t* out_texture, int src_width, int src_height)
{
    int			x, y, i;
    const pixel_t*	src[2];
    pixel_t* 		dst;

    dst = out_texture;
    for( y = 0; y < src_height; y += 2)
    {
	src[0] = in_texture + y * src_width;
	src[1] = src[0] + src_width;
	for( x = 0; x < src_width; x += 2, src[0]+= 2, src[1]+= 2, dst++ )
	{
	    for( i = 0; i < 4; i++ )
		dst->components[i] =
		    (src[0][0].components[i] + src[0][1].components[i] + src[1][0].components[i] + src[1][1].components[i])
		    >> 2;
	}
    }
}

static void BuildWallMip(const pixel_t* in_texture, pixel_t* out_texture, int src_width, int src_height)
{
    int			x, y, i;
    const pixel_t*	src[2];
    pixel_t* 		dst;
    int			w, h;

    w = src_width  & (~1);
    h = src_height & (~1);

    dst = out_texture;
    for( x = 0; x < w; x += 2 )
    {
	src[0] = in_texture + x * src_height;
	src[1] = src[0] + src_height;
	for( y = 0; y < h; y += 2, src[0]+= 2, src[1]+= 2, dst++ )
	{
	    for( i = 0; i < 4; i++ )
		dst->components[i] =
		    (src[0][0].components[i] + src[0][1].components[i] + src[1][0].components[i] + src[1][1].components[i])
		    >> 2;
	}
    }
}

static void R_32b_InitPalette()
{
    int		i;
    int		j;
    byte*	playpal;

    playpal = W_CacheLumpName ("PLAYPAL",PU_CACHE);

    //TODO - get platform pixel order
    for( i = 0, j = 0; i < 256; i++, j+=3 )
    {
	g_textures_palette[i].components[0] = playpal[j+2];
	g_textures_palette[i].components[1] = playpal[j+1];
	g_textures_palette[i].components[2] = playpal[j+0];
	g_textures_palette[i].components[3] = 255;
    }

}

static void RP_InitWallsTextures()
{
    int*		maptex_data[2];
    int			maptex_count[2];
    int*		pnames_data;
    int*		patches_lumps;
    int			pnames_count;
    char		patch_name_nullt[9];
    int			textues_patches_count;
    wall_texture_patch_t*tex_patch;
    int			tex_list_count;
    int			i, j, k;
    wall_texture_t*	tex;
    wall_texture_info_t*tex_info;
    maptexture_t*	maptex;
    mappatch_t*		mappatch;
    int*		offsets;

    // get patches names and translate it to lump names
    pnames_data = W_CacheLumpName("PNAMES", PU_STATIC);
    pnames_count = LONG(*pnames_data);
    patches_lumps = malloc(pnames_count * sizeof(int));
    for( i = 0; i < pnames_count; i++ )
    {
    	strncpy(patch_name_nullt, ((char*)(pnames_data + 1)) + i * 8, 8 );
    	patch_name_nullt[8] = 0;
    	patches_lumps[i] = W_CheckNumForName(patch_name_nullt);
    }

    // calc textures count
    tex_list_count = W_CheckNumForName (c_textures_list_lump_names[1]) == -1 ? 1 : 2;
    g_wall_textures_count = 0;
    for ( i = 0; i < tex_list_count; i++ )
    {
	maptex_data[i] = W_CacheLumpName (c_textures_list_lump_names[i], PU_STATIC);
	maptex_count[i] = LONG(*(maptex_data[i]));
	g_wall_textures_count += maptex_count[i];
    }

    // calc patches in textures
    textues_patches_count = 0;
    for ( i = 0; i < tex_list_count; i++ )
    {
	offsets = maptex_data[i] + 1;
	for( j = 0; j < maptex_count[i]; j++ )
	{
	    maptex = (maptexture_t*) (((byte*)maptex_data[i]) + LONG(offsets[j]));
	    textues_patches_count += SHORT(maptex->patchcount);
	}
    }

    g_wall_textures = Z_Malloc( sizeof(wall_texture_t) * g_wall_textures_count, PU_STATIC, 0 );
    g_wall_textures_info = Z_Malloc( sizeof(wall_texture_info_t) * g_wall_textures_count, PU_STATIC, 0 );
    g_wall_textures_patches = Z_Malloc( textues_patches_count * sizeof(wall_texture_patch_t), PU_STATIC, 0 );
    texturetranslation = Z_Malloc( sizeof(int) * g_wall_textures_count, PU_STATIC, 0 );

    // get textures info
    tex = g_wall_textures;
    tex_info = g_wall_textures_info;
    tex_patch = g_wall_textures_patches;
    for ( i = 0; i < tex_list_count; i++ )
    {
	offsets = maptex_data[i] + 1;
	for( j = 0; j < maptex_count[i]; j++, tex++, tex_info++ )
	{
	    maptex = (maptexture_t*) (((byte*)maptex_data[i]) + LONG(offsets[j]));
	    memcpy( tex->name, maptex->name, 8 );
	    tex->width  = SHORT(maptex->width );
	    tex->height = SHORT(maptex->height);
	    tex->raw_data = NULL;

	    tex_info->patches = tex_patch;
	    tex_info->patch_count = SHORT(maptex->patchcount);
	    mappatch = maptex->patches;
	    for( k = 0; k < tex_info->patch_count; k++, mappatch++ )
	    {
		tex_info->patches[k].origin_x = SHORT(mappatch->originx);
		tex_info->patches[k].origin_y = SHORT(mappatch->originy);
		tex_info->patches[k].lump_num = patches_lumps[ SHORT(mappatch->patch) ];
	    }
	    tex_patch += tex_info->patch_count;
	}
    }

    free(patches_lumps);

    for ( i = 0; i < g_wall_textures_count ; i++ )
	texturetranslation[i] = i;
}

static void RP_InitFlatsTextures()
{
    int		i;
    int		last_flat;

    g_first_flat = W_GetNumForName ("F_START") + 1;
    last_flat    = W_GetNumForName ("F_END"  ) - 1;
    g_flat_textures_count = last_flat - g_first_flat + 1;

    g_flat_textures = Z_Malloc( sizeof(flat_texture_t) * g_flat_textures_count, PU_STATIC, 0 );
    flattranslation = Z_Malloc( sizeof(int) * g_flat_textures_count, PU_STATIC, 0 );

    for ( i = 0; i < g_flat_textures_count; i++ )
	g_flat_textures[i].raw_data = NULL;

    for ( i = 0; i < g_flat_textures_count; i++ )
	flattranslation[i] = i;
}

static void R_32b_LoadWallTexture(int texture_num)
{
    wall_texture_t*		tex;
    wall_texture_info_t*	tex_info;
    int				pixel_count;
    pixel_t*			dst;
    byte*			src;
    int				i;
    int				offset;
    int				x, x0, x1;
    int				y, y0;
    int				column_pixel_count;

    patch_t*			patch;
    column_t*			patch_column;

    tex = &g_wall_textures[texture_num];
    tex_info = &g_wall_textures_info[texture_num];

    // count pixels and mip count
    pixel_count = 0;
    for( i = 0, x = tex->width, y = tex->height; x > 0 && y > 0; x>>= 1, y>>= 1, i++ )
	pixel_count += x * y;
    tex->max_mip = i - 1;
    if (tex->max_mip >= RP_MAX_WALL_MIPS ) tex->max_mip = RP_MAX_WALL_MIPS - 1;

    // allocate data
    tex->raw_data = tex->mip[0] = malloc(sizeof(pixel_t) * pixel_count);
    memset(tex->raw_data, 0, pixel_count * sizeof(pixel_t));

    // build texture from patches
    for( i = 0; i < tex_info->patch_count; i++ )
    {
	patch = W_CacheLumpNum(tex_info->patches[i].lump_num, PU_CACHE);

	x0 = tex_info->patches[i].origin_x;
	if ( x0 < 0 ) x = 0;
	else x = x0;

        x1 = x0 + patch->width;
	if (x1 > tex->width) x1 = tex->width;

	y0 = tex_info->patches[i].origin_y;

	for( ; x < x1; x++)
	{
            dst = tex->raw_data + x * tex->height;
	    patch_column = (column_t *)((byte *)patch + LONG(patch->columnofs[x-x0]));

	    while (patch_column->topdelta != 0xff)
	    {
	    	src = (byte*)patch_column + 3;

		y = patch_column->topdelta + y0;
		column_pixel_count = patch_column->length;
		if (y < 0)
		{
		    column_pixel_count += y; y = 0;
		}
		if( y + column_pixel_count > tex->height) column_pixel_count = tex->height - y;

		while(column_pixel_count > 0 )
		{
		    dst[y] = g_textures_palette[*src];
		    src++; y++;
		    column_pixel_count--;
		}

		patch_column = (column_t *)(  (byte *)patch_column + patch_column->length + 4);
	    }
	}
    }

     // build mips
    offset = tex->width * tex->height;
    for( i = 1, x = tex->width, y = tex->height; i <= tex->max_mip; i++, x>>=1, y>>=1 )
    {
    	tex->mip[i] = tex->raw_data + offset;
    	BuildWallMip(tex->mip[i-1], tex->mip[i], x, y );
    	offset += (x>>1) * (y>>1);
    }
}

static void R_32b_LoadFlatTexture(int flatnum)
{
    byte*		raw_data;
    flat_texture_t*	tex;
    int			offset;
    int			pixel_count;
    int			i;
    int			cur_size;

    raw_data = W_CacheLumpNum( flatnum + g_first_flat, PU_CACHE );

    // allocate data
    tex = &g_flat_textures[flatnum];
    pixel_count = RP_FLAT_TEXTURE_SIZE * RP_FLAT_TEXTURE_SIZE * 4 / 3;
    tex->raw_data = tex->mip[0] = malloc(pixel_count * sizeof(pixel_t));

    // convert 8bit to 32bit
    for( i = 0; i < RP_FLAT_TEXTURE_SIZE * RP_FLAT_TEXTURE_SIZE; i++ )
	tex->mip[0][i] = g_textures_palette[raw_data[i]];

    // generate mips
    offset = RP_FLAT_TEXTURE_SIZE * RP_FLAT_TEXTURE_SIZE;
    for( i = 1, cur_size = RP_FLAT_TEXTURE_SIZE>>1; i <= RP_FLAT_TEXTURE_SIZE_LOG2; i++, cur_size>>=1 )
    {
	tex->mip[i] = tex->raw_data + offset;
	BuildFlatMip(tex->mip[i-1], tex->mip[i], cur_size<<1, cur_size<<1);
	offset += cur_size * cur_size;
    }
}

void R_32b_PrecacheWallsTextures()
{
    int		i;

    for( i = 0; i < g_wall_textures_count; i++ )
    	g_wall_textures[i].used = false;

    for ( i = 0 ; i < numsides; i++ )
    {
	g_wall_textures[sides[i].toptexture   ].used = true;
	g_wall_textures[sides[i].midtexture   ].used = true;
	g_wall_textures[sides[i].bottomtexture].used = true;
    }

    for( i = 0; i < g_wall_textures_count; i++ )
    {
	if (g_wall_textures[i].used && !g_wall_textures[i].raw_data)
	    R_32b_LoadWallTexture(i);
	else
	{
	    if (g_wall_textures[i].raw_data)
	    {
		free(g_wall_textures[i].raw_data);
		g_wall_textures[i].raw_data = NULL;
	    }
	}
    }
}

void R_32b_PrecacheFlatsTextures()
{
    int		i;

    for( i = 0; i < g_flat_textures_count; i++ )
	g_flat_textures[i].used = false;

    for( i = 0; i < numsectors; i++ )
    {
	g_flat_textures[sectors[i].floorpic  ].used = true;
	g_flat_textures[sectors[i].ceilingpic].used = true;
    }

    for( i = 0; i < g_flat_textures_count; i++ )
    {
	if (g_flat_textures[i].used && !g_flat_textures[i].raw_data)
	    R_32b_LoadFlatTexture(i);
	else
	{
	    if (g_flat_textures[i].raw_data)
	    {
		free(g_flat_textures[i].raw_data);
		g_flat_textures[i].raw_data = NULL;
	    }
	}
    }
}

void R_32b_PrecacheLevel()
{
    R_32b_PrecacheWallsTextures();
    R_32b_PrecacheFlatsTextures();
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
	if (!strncasecmp (g_wall_textures[i].name, name, 8) )
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

wall_texture_t* GetWallTexture(int num)
{
    wall_texture_t* tex = &g_wall_textures[num];
    if (!tex->raw_data)
	R_32b_LoadWallTexture(num);

    return tex;
}

flat_texture_t* GetFlatTexture(int num)
{
    flat_texture_t* tex = &g_flat_textures[num];
    if (!tex->raw_data)
	R_32b_LoadFlatTexture(num);

    return tex;
}
