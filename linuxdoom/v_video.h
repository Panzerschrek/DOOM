// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
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
//
// DESCRIPTION:
//	Gamma correction LUT.
//	Functions to draw patches (by post) directly to screen.
//	Functions to blit a block to the screen.
//
//-----------------------------------------------------------------------------


#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "doomtype.h"

#include "doomdef.h"

// Needed because we are refering to patches.
#include "r_data.h"

//
// VIDEO
//

#define CENTERY			(SCREENHEIGHT/2)


// Screen 0 is the screen updated by I_Update screen.
// Screen 1 is an extra buffer.



extern	byte*		screens[5];

extern	byte	gammatable[5][256];
extern	int	usegamma;



// Allocates buffer screens, call before R_Init.
void V_Init (void);


extern void
(*V_DrawPatch)
( int		x,
  int		y,
  patch_t*	patch);

extern void
(*V_DrawPatchCol)
( int		x,
  int		height,
  patch_t*	patch,
  int		col );


extern void
(*V_DrawPatchScaled)
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch );


extern void
(*V_DrawPatchScaledFlipped)
( int		x,
  int		y,
  int		width,
  int		height,
  patch_t*	patch );


// Draw a linear block of pixels into the view buffer.
extern void
(*V_DrawBlock)
( int		x,
  int		y,
  int		width,
  int		height,
  byte*		src );


extern void
(*V_FillRectByTexture)
( int		x,
  int		y,
  int		width,
  int		height,
  int		tex_width,
  int		tex_height,
  int		tex_scale,
  byte*		tex_data );


extern void
(*V_DrawPixel)
( int x,
  int y,
  int color_index );


extern void
(*V_FillRect)
( int x,
  int y,
  int width,
  int height,
  int color_index );

#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
