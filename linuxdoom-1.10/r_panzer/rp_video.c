#include "../doomdef.h"
#include "../m_fixed.h"
#include "../r_data.h"
#include "../v_video.h"
#include "rp_defs.h"
#include "rp_video.h"

// Main framebuffer.
// if we can, we use external framebuffer
static pixel_t*		screen;
static pixel_t		palette[256];

static void
V_32b_FillRectByTexture
( int		x,
  int		y,
  int		width,
  int		height,
  int		tex_width,
  int		tex_height,
  int		tex_scale,
  byte*		tex_data )
{
    int		x0 = x;
    int		x_end = x + width;
    int		y_end = y + height;
    int		tex_width1 = tex_width - 1;
    int		tex_height1 = tex_height - 1;
    fixed_t	step = FRACUNIT / tex_scale + 1;
    fixed_t	u;
    pixel_t*	dest;
    byte*	src;

    for ( ; y < y_end; y++ )
    {
	dest = screen + y * SCREENWIDTH + x0;
	src = tex_data + (((y*step)>>FRACBITS) & tex_height1) * tex_width;
	for( x = x0, u = x0 * step; x < x_end; x++, dest++, u+= step )
	    *dest = palette[ src[ (u>>FRACBITS) & tex_width1 ] ];
    }
}

static void
V_32b_DrawPixel
( int x,
  int y,
  int color_index )
{
    screen[ x + y * SCREENWIDTH ] = palette[ color_index ];
}

static void
V_32b_FillRect
( int x,
  int y,
  int width,
  int height,
  int color_index )
{
    pixel_t*	dst;
    int		xx;
    int		yy;
    int		x_end = x + width;
    int		y_end = y + height;

    for( yy = y; yy < y_end; yy++ )
    {
	dst = screen + yy * SCREENWIDTH + x;
	for( xx = x; xx < x_end; xx++, dst++ ) *dst = palette[ color_index ];
    }
}

static void
V_32b_DrawPatch
( int		x,
  int		y,
  patch_t*	patch )
{

    int		count;
    int		col;
    column_t*	column;
    pixel_t*	desttop;
    pixel_t*	dest;
    byte*	source;
    int		w;

    y -= patch->topoffset;
    x -= patch->leftoffset;
#ifdef RANGECHECK
    if (x<0
	||x+patch->width >SCREENWIDTH
	|| y<0
	|| y+patch->height>SCREENHEIGHT)
    {
      fprintf( stderr, "Patch at %d,%d exceeds LFB\n", x,y );
      // No I_Error abort - what is up with TNT.WAD?
      fprintf( stderr, "V_DrawPatch: bad patch (ignored)\n");
      return;
    }
#endif


    col = 0;
    desttop = screen+y*SCREENWIDTH+x;

    w = patch->width;

    for ( ; col<w ; x++, col++, desttop++)
    {
	column = (column_t *)((byte *)patch + ((int)patch->columnofs[col]));

	// step through the posts in a column
	while (column->topdelta != 0xff )
	{
	    source = (byte *)column + 3;
	    dest = desttop + column->topdelta*SCREENWIDTH;
	    count = column->length;

	    while (count--)
	    {
		*dest = palette[ *source++ ];
		dest += SCREENWIDTH;
	    }
	    column = (column_t *)(  (byte *)column + column->length
				    + 4 );
	}
    }
}

static void
V_32b_DrawPatchCol
( int		x,
  int		height,
  patch_t*	patch,
  int		col )
{
    column_t*	column;
    byte*	source;
    pixel_t*	dest;
    pixel_t*	desttop;
    int		v;
    int		v_step;
    int		cur_y;
    int		loc_y;

    column = (column_t *)((byte *)patch + ((int)patch->columnofs[col]));
    desttop = screen+x;

    // step through the posts in a column
    v_step = (patch->height <<FRACBITS) / height;
    v = 0;
    cur_y = 0;
    while(column->topdelta != 0xff)
    {
	while((v>>FRACBITS) < column->topdelta)
	{
	    v+= v_step;
	    cur_y++;
	}
	source = (byte *)column + 3;
	dest = desttop + cur_y * SCREENWIDTH;

	loc_y = (v>>FRACBITS) - column->topdelta;
	while( loc_y < column->length && cur_y < height)
	{
	    *dest = palette[ source[loc_y] ];
	    dest += SCREENWIDTH;
	    v += v_step;
	    cur_y++;
	    loc_y = (v>>FRACBITS) - column->topdelta;
	}
	column = (column_t *)( (byte *)column + column->length + 4 );
    }
}


static void
V_DrawPatchScaledInternal
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch,
  boolean	flipped )
{
    int		cur_y;
    int		loc_y;
    column_t*	column;
    pixel_t*	desttop;
    pixel_t*	dest;
    byte*	source;
    fixed_t	u;
    fixed_t	v;
    fixed_t	u_step;
    fixed_t	v_step;
    int		x_end;
    int		x_step;

    x -= patch->leftoffset * width / patch->width;
    y -= patch->topoffset * height / patch->height;
#ifdef RANGECHECK
    if (x<0
	||x+width >SCREENWIDTH
	|| y<0
	|| y+height>SCREENHEIGHT)
    {
      fprintf( stderr, "Patch at %d,%d exceeds LFB\n", x,y );
      // No I_Error abort - what is up with TNT.WAD?
      fprintf( stderr, "V_DrawPatchScaled: bad patch (ignored)\n");
      return;
    }
#endif

    u_step = (patch->width << FRACBITS) / width;
    if ((((u_step+1) * (width - 1))>>FRACBITS) < patch->width ) u_step++;
    v_step = (patch->height << FRACBITS) / height;v_step++;
    if ((((v_step+1) * (height - 1))>>FRACBITS) < patch->height ) v_step++;
    u = 0;

    if (flipped)
    {
	x_end = x;
	x += width;
	x_step = -1;
    }
    else
    {
    	x_end = x + width;
    	x_step = 1;
    }
    desttop = screen+y*SCREENWIDTH+x;

    for ( ; (x - x_end) * x_step < 0; u += u_step, desttop+= x_step, x+= x_step )
    {
	column = (column_t *)((byte *)patch + patch->columnofs[u >> FRACBITS]);

	// step through the posts in a column
	v = 0;
	cur_y = 0;
	while(column->topdelta != 0xff)
	{
	    while((v>>FRACBITS) < column->topdelta)
	    {
		v+= v_step;
		cur_y++;
	    }
	    source = (byte *)column + 3;
	    dest = desttop + cur_y * SCREENWIDTH;

	    loc_y = (v>>FRACBITS) - column->topdelta;
	    while( loc_y < column->length && cur_y < height)
	    {
		*dest = palette[ source[loc_y] ];
		dest += SCREENWIDTH;
		v += v_step;
		cur_y++;
		loc_y = (v>>FRACBITS) - column->topdelta;
	    }
	    column = (column_t *)( (byte *)column + column->length + 4 );
	}
    }
}

static void
V_32b_DrawPatchScaled
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch )
{
    V_DrawPatchScaledInternal(x, y, width, height, patch, false);
}

static void
V_32b_DrawPatchScaledFlipped
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch )
{
    V_DrawPatchScaledInternal(x, y, width, height, patch, true);
}

// UNFINISHED
static void
V_32b_DrawBlock
( int		x,
  int		y,
  int		width,
  int		height,
  byte*		src )
{
    x = y;
    width = height = src[0];
}

static void V_32b_InitAPI()
{
    V_DrawPatch			= V_32b_DrawPatch;
    V_DrawPatchCol		= V_32b_DrawPatchCol;
    V_DrawPatchScaled		= V_32b_DrawPatchScaled;
    V_DrawPatchScaledFlipped	= V_32b_DrawPatchScaledFlipped;
    V_DrawBlock			= V_32b_DrawBlock;
    V_FillRectByTexture		= V_32b_FillRectByTexture;
    V_DrawPixel			= V_32b_DrawPixel;
    V_FillRect			= V_32b_FillRect;
}

void VP_Init()
{
    V_32b_InitAPI();
}

void VP_SetupFramebuffer(void* framebuffer)
{
    screen = (pixel_t*) framebuffer;
}

pixel_t* VP_GetPaletteStorage()
{
    return palette;
}
