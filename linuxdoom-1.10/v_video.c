// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	Gamma correction LUT stuff.
//	Functions to draw patches (by post) directly to screen.
//	Functions to blit a block to the screen.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: v_video.c,v 1.5 1997/02/03 22:45:13 b1 Exp $";


#include "i_system.h"
#include "r_local.h"

#include "doomdef.h"
#include "doomdata.h"

#include "m_bbox.h"
#include "m_swap.h"

#include "v_video.h"


// Each screen is [SCREENWIDTH*SCREENHEIGHT];
byte*				screens[5];

// API pointers

void
(*V_DrawPatch)
( int		x,
  int		y,
  patch_t*	patch);

void
(*V_DrawPatchCol)
( int		x,
  int		height,
  patch_t*	patch,
  int		col );

void
(*V_DrawPatchScaled)
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch );

void
(*V_DrawPatchScaledFlipped)
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch );


void
(*V_DrawBlock)
( int		x,
  int		y,
  int		width,
  int		height,
  byte*		src );

void
(*V_FillRectByTexture)
( int		x,
  int		y,
  int		width,
  int		height,
  int		tex_width,
  int		tex_height,
  int		tex_scale,
  byte*		tex_data );

void
(*V_DrawPixel)
( int x,
  int y,
  int color_index );

void
(*V_FillRect)
( int x,
  int y,
  int width,
  int height,
  int color_index );

// Now where did these came from?
byte gammatable[5][256] =
{
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
     17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
     33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
     49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,
     65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,
     81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,
     97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,
     113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,
     128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
     144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
     160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
     176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
     192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
     208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
     224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
     240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255},

    {2,4,5,7,8,10,11,12,14,15,16,18,19,20,21,23,24,25,26,27,29,30,31,
     32,33,34,36,37,38,39,40,41,42,44,45,46,47,48,49,50,51,52,54,55,
     56,57,58,59,60,61,62,63,64,65,66,67,69,70,71,72,73,74,75,76,77,
     78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,
     99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,
     115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,129,
     130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,
     146,147,148,148,149,150,151,152,153,154,155,156,157,158,159,160,
     161,162,163,163,164,165,166,167,168,169,170,171,172,173,174,175,
     175,176,177,178,179,180,181,182,183,184,185,186,186,187,188,189,
     190,191,192,193,194,195,196,196,197,198,199,200,201,202,203,204,
     205,205,206,207,208,209,210,211,212,213,214,214,215,216,217,218,
     219,220,221,222,222,223,224,225,226,227,228,229,230,230,231,232,
     233,234,235,236,237,237,238,239,240,241,242,243,244,245,245,246,
     247,248,249,250,251,252,252,253,254,255},

    {4,7,9,11,13,15,17,19,21,22,24,26,27,29,30,32,33,35,36,38,39,40,42,
     43,45,46,47,48,50,51,52,54,55,56,57,59,60,61,62,63,65,66,67,68,69,
     70,72,73,74,75,76,77,78,79,80,82,83,84,85,86,87,88,89,90,91,92,93,
     94,95,96,97,98,100,101,102,103,104,105,106,107,108,109,110,111,112,
     113,114,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,
     129,130,131,132,133,133,134,135,136,137,138,139,140,141,142,143,144,
     144,145,146,147,148,149,150,151,152,153,153,154,155,156,157,158,159,
     160,160,161,162,163,164,165,166,166,167,168,169,170,171,172,172,173,
     174,175,176,177,178,178,179,180,181,182,183,183,184,185,186,187,188,
     188,189,190,191,192,193,193,194,195,196,197,197,198,199,200,201,201,
     202,203,204,205,206,206,207,208,209,210,210,211,212,213,213,214,215,
     216,217,217,218,219,220,221,221,222,223,224,224,225,226,227,228,228,
     229,230,231,231,232,233,234,235,235,236,237,238,238,239,240,241,241,
     242,243,244,244,245,246,247,247,248,249,250,251,251,252,253,254,254,
     255},

    {8,12,16,19,22,24,27,29,31,34,36,38,40,41,43,45,47,49,50,52,53,55,
     57,58,60,61,63,64,65,67,68,70,71,72,74,75,76,77,79,80,81,82,84,85,
     86,87,88,90,91,92,93,94,95,96,98,99,100,101,102,103,104,105,106,107,
     108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,
     125,126,127,128,129,130,131,132,133,134,135,135,136,137,138,139,140,
     141,142,143,143,144,145,146,147,148,149,150,150,151,152,153,154,155,
     155,156,157,158,159,160,160,161,162,163,164,165,165,166,167,168,169,
     169,170,171,172,173,173,174,175,176,176,177,178,179,180,180,181,182,
     183,183,184,185,186,186,187,188,189,189,190,191,192,192,193,194,195,
     195,196,197,197,198,199,200,200,201,202,202,203,204,205,205,206,207,
     207,208,209,210,210,211,212,212,213,214,214,215,216,216,217,218,219,
     219,220,221,221,222,223,223,224,225,225,226,227,227,228,229,229,230,
     231,231,232,233,233,234,235,235,236,237,237,238,238,239,240,240,241,
     242,242,243,244,244,245,246,246,247,247,248,249,249,250,251,251,252,
     253,253,254,254,255},

    {16,23,28,32,36,39,42,45,48,50,53,55,57,60,62,64,66,68,69,71,73,75,76,
     78,80,81,83,84,86,87,89,90,92,93,94,96,97,98,100,101,102,103,105,106,
     107,108,109,110,112,113,114,115,116,117,118,119,120,121,122,123,124,
     125,126,128,128,129,130,131,132,133,134,135,136,137,138,139,140,141,
     142,143,143,144,145,146,147,148,149,150,150,151,152,153,154,155,155,
     156,157,158,159,159,160,161,162,163,163,164,165,166,166,167,168,169,
     169,170,171,172,172,173,174,175,175,176,177,177,178,179,180,180,181,
     182,182,183,184,184,185,186,187,187,188,189,189,190,191,191,192,193,
     193,194,195,195,196,196,197,198,198,199,200,200,201,202,202,203,203,
     204,205,205,206,207,207,208,208,209,210,210,211,211,212,213,213,214,
     214,215,216,216,217,217,218,219,219,220,220,221,221,222,223,223,224,
     224,225,225,226,227,227,228,228,229,229,230,230,231,232,232,233,233,
     234,234,235,235,236,236,237,237,238,239,239,240,240,241,241,242,242,
     243,243,244,244,245,245,246,246,247,247,248,248,249,249,250,250,251,
     251,252,252,253,254,254,255,255}
};


int	usegamma;


// implimentation of 8b draw functions

static void
V_8b_FillRectByTexture
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
    byte*	dest;
    byte*	src;

    for ( ; y < y_end; y++ )
    {
	dest = screens[0] + y * SCREENWIDTH + x0;
	src = tex_data + (((y*step)>>FRACBITS) & tex_height1) * tex_width;
	for( x = x0, u = x0 * step; x < x_end; x++, dest++, u+= step )
	    *dest = src[ (u>>FRACBITS) & tex_width1 ];
    }
}

static void
V_8b_DrawPixel
( int x,
  int y,
  int color_index )
{
    screens[0][ x + y * SCREENWIDTH ] = color_index;
}

static void
V_8b_FillRect
( int x,
  int y,
  int width,
  int height,
  int color_index )
{
    byte*	dst;
    int		xx;
    int		yy;
    int		x_end = x + width;
    int		y_end = y + height;

    for( yy = y; yy < y_end; yy++ )
    {
	dst = screens[0] + yy * SCREENWIDTH + x;
	for( xx = x; xx < x_end; xx++, dst++ ) *dst = color_index;
    }
}


//
// V_DrawPatch
// Masks a column based masked pic to the screen.
//
static void
V_8b_DrawPatch
( int		x,
  int		y,
  patch_t*	patch )
{

    int		count;
    int		col;
    column_t*	column;
    byte*	desttop;
    byte*	dest;
    byte*	source;
    int		w;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);
#ifdef RANGECHECK
    if (x<0
	||x+SHORT(patch->width) >SCREENWIDTH
	|| y<0
	|| y+SHORT(patch->height)>SCREENHEIGHT)
    {
      fprintf( stderr, "Patch at %d,%d exceeds LFB\n", x,y );
      // No I_Error abort - what is up with TNT.WAD?
      fprintf( stderr, "V_DrawPatch: bad patch (ignored)\n");
      return;
    }
#endif


    col = 0;
    desttop = screens[0]+y*SCREENWIDTH+x;

    w = SHORT(patch->width);

    for ( ; col<w ; x++, col++, desttop++)
    {
	column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));

	// step through the posts in a column
	while (column->topdelta != 0xff )
	{
	    source = (byte *)column + 3;
	    dest = desttop + column->topdelta*SCREENWIDTH;
	    count = column->length;

	    while (count--)
	    {
		*dest = *source++;
		dest += SCREENWIDTH;
	    }
	    column = (column_t *)(  (byte *)column + column->length
				    + 4 );
	}
    }
}

//
// V_DrawPatchCol
//
static void
V_8b_DrawPatchCol
( int		x,
  int		height,
  patch_t*	patch,
  int		col )
{
    column_t*	column;
    byte*	source;
    byte*	dest;
    byte*	desttop;
    int		v;
    int		v_step;
    int		cur_y;
    int		loc_y;

    column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));
    desttop = screens[0]+x;

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
	    *dest = source[loc_y];
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
    byte*	desttop;
    byte*	dest;
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
	||x+SHORT(width) >SCREENWIDTH
	|| y<0
	|| y+SHORT(height)>SCREENHEIGHT)
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
    desttop = screens[0]+y*SCREENWIDTH+x;

    for ( ; (x - x_end) * x_step < 0; u += u_step, desttop+= x_step, x+= x_step )
    {
	column = (column_t *)((byte *)patch + LONG(patch->columnofs[u >> FRACBITS]));

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
		*dest = source[loc_y];
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
V_8b_DrawPatchScaled
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch )
{
    V_DrawPatchScaledInternal(x, y, width, height, patch, false);
}

static void
V_8b_DrawPatchScaledFlipped
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch )
{
    V_DrawPatchScaledInternal(x, y, width, height, patch, true);
}

//
// V_DrawPatchFlipped
// Masks a column based masked pic to the screen.
// Flips horizontally, e.g. to mirror face.
//
//UNUSED
#if 0
static void
V_8b_DrawPatchFlipped
( int		x,
  int		y,
  patch_t*	patch )
{

    int		count;
    int		col;
    column_t*	column;
    byte*	desttop;
    byte*	dest;
    byte*	source;
    int		w;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);
#ifdef RANGECHECK
    if (x<0
	||x+SHORT(patch->width) >SCREENWIDTH
	|| y<0
	|| y+SHORT(patch->height)>SCREENHEIGHT)
    {
      fprintf( stderr, "Patch origin %d,%d exceeds LFB\n", x,y );
      I_Error ("Bad V_DrawPatch in V_DrawPatchFlipped");
    }
#endif

    col = 0;
    desttop = screens[0]+y*SCREENWIDTH+x;

    w = SHORT(patch->width);

    for ( ; col<w ; x++, col++, desttop++)
    {
	column = (column_t *)((byte *)patch + LONG(patch->columnofs[w-1-col]));

	// step through the posts in a column
	while (column->topdelta != 0xff )
	{
	    source = (byte *)column + 3;
	    dest = desttop + column->topdelta*SCREENWIDTH;
	    count = column->length;

	    while (count--)
	    {
		*dest = *source++;
		dest += SCREENWIDTH;
	    }
	    column = (column_t *)(  (byte *)column + column->length
				    + 4 );
	}
    }
}
#endif


//
// V_DrawBlock
// Draw a linear block of pixels into the view buffer.
//
static void
V_8b_DrawBlock
( int		x,
  int		y,
  int		width,
  int		height,
  byte*		src )
{
    byte*	dest;

#ifdef RANGECHECK
    if (x<0
	||x+width >SCREENWIDTH
	|| y<0
	|| y+height>SCREENHEIGHT)
    {
	I_Error ("Bad V_DrawBlock");
    }
#endif

    dest = screens[0] + y*SCREENWIDTH+x;

    while (height--)
    {
	memcpy (dest, src, width);
	src += width;
	dest += SCREENWIDTH;
    }
}

static void V_8b_InitAPI()
{
    V_DrawPatch			= V_8b_DrawPatch;
    V_DrawPatchCol		= V_8b_DrawPatchCol;
    V_DrawPatchScaled		= V_8b_DrawPatchScaled;
    V_DrawPatchScaledFlipped	= V_8b_DrawPatchScaledFlipped;
    V_DrawBlock			= V_8b_DrawBlock;
    V_FillRectByTexture		= V_8b_FillRectByTexture;
    V_DrawPixel			= V_8b_DrawPixel;
    V_FillRect			= V_8b_FillRect;
}

//
// V_Init
//
void V_Init (void)
{
    int		i;
    byte*	base;

    V_8b_InitAPI();

    base = I_AllocLow (SCREENWIDTH*SCREENHEIGHT*4);

    for (i=0 ; i<4 ; i++)
	screens[i] = base + i*SCREENWIDTH*SCREENHEIGHT;
}
