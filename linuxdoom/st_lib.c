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
// $Log:$
//
// DESCRIPTION:
//	The status bar widget code.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: st_lib.c,v 1.4 1997/02/03 16:47:56 b1 Exp $";

#include <ctype.h>

#include "doomdef.h"

#include "z_zone.h"
#include "v_video.h"

#include "m_swap.h"

#include "i_system.h"

#include "w_wad.h"

#include "st_stuff.h"
#include "st_lib.h"
#include "r_local.h"


// in m_menu.c
extern int menuscale;
extern int x_offset;

// in AM_map.c
extern boolean		automapactive;




//
// Hack display negative frags.
//  Loads and store the stminus lump.
//
patch_t*		sttminus;

void STlib_init(void)
{
    sttminus = (patch_t *) W_CacheLumpName("STTMINUS", PU_STATIC);
}


// ?
void
STlib_initNum
( st_number_t*		n,
  int			x,
  int			y,
  patch_t**		pl,
  int*			num,
  boolean*		on,
  int			width )
{
    n->x	= x * menuscale + x_offset;
    n->y	= SCREENHEIGHT - ( ID_SCREENHEIGHT - y ) * menuscale;
    n->oldnum	= 0;
    n->width	= width;
    n->num	= num;
    n->on	= on;
    n->p	= pl;
}


//
// A fairly efficient way to draw a number
//  based on differences from the old number.
// Note: worth the trouble?
//
void
STlib_drawNum
( st_number_t*	n )
{

    patch_t*	patch;
    int		numdigits = n->width;
    int		num = *n->num;

    int		w = SHORT(n->p[0]->width) * menuscale;
    int		x = n->x;

    int		neg;

    n->oldnum = *n->num;

    neg = num < 0;

    if (neg)
    {
	if (numdigits == 2 && num < -9)
	    num = -9;
	else if (numdigits == 3 && num < -99)
	    num = -99;

	num = -num;
    }

    // clear the area
    x = n->x - numdigits*w;

    // if non-number, do not draw it
    if (num == 1994)
	return;

    x = n->x;

    // in the special case of 0, you draw 0
    if (!num)
	V_DrawPatchScaled(
	    x - w, n->y,
	    n->p[0]->width * menuscale, n->p[0]->height * menuscale,
	    n->p[0]);

    // draw the new number
    while (num && numdigits--)
    {
	patch = n->p[ num % 10 ];
	x -= w;
	V_DrawPatchScaled( x, n->y, patch->width * menuscale, patch->height * menuscale, patch );
	num /= 10;
    }

    // draw a minus sign if necessary
    if (neg)
	V_DrawPatchScaled( x - 8 * menuscale, n->y, sttminus->width * menuscale, sttminus->height * menuscale, sttminus );
}


//
void
STlib_updateNum
( st_number_t*		n )
{
    if (*n->on) STlib_drawNum(n);
}


//
void
STlib_initPercent
( st_percent_t*		p,
  int			x,
  int			y,
  patch_t**		pl,
  int*			num,
  boolean*		on,
  patch_t*		percent )
{
    STlib_initNum(&p->n, x, y, pl, num, on, 3);
    p->p = percent;
}




void
STlib_updatePercent
( st_percent_t*		per )
{
    if (*per->n.on)
	V_DrawPatchScaled(
	    per->n.x, per->n.y,
	    per->p->width * menuscale, per->p->height * menuscale,
	    per->p);

    STlib_updateNum(&per->n);
}



void
STlib_initMultIcon
( st_multicon_t*	i,
  int			x,
  int			y,
  patch_t**		il,
  int*			inum,
  boolean*		on )
{
    i->x	= x * menuscale + x_offset;
    i->y	= SCREENHEIGHT - ( ID_SCREENHEIGHT - y ) * menuscale;
    i->oldinum 	= -1;
    i->inum	= inum;
    i->on	= on;
    i->p	= il;
}



void
STlib_updateMultIcon
( st_multicon_t*	mi )
{
    patch_t*		patch;

    if (*mi->on
	&& (*mi->inum!=-1))
    {
	patch = mi->p[*mi->inum];
	V_DrawPatchScaled(
	    mi->x, mi->y,
	    patch->width * menuscale, patch->height * menuscale,
	    patch );

	mi->oldinum = *mi->inum;
    }
}



void
STlib_initBinIcon
( st_binicon_t*		b,
  int			x,
  int			y,
  patch_t*		i,
  boolean*		val,
  boolean*		on )
{
    b->x	= x * menuscale + x_offset;
    b->y	= SCREENHEIGHT - ( ID_SCREENHEIGHT - y ) * menuscale;
    b->oldval	= 0;
    b->val	= val;
    b->on	= on;
    b->p	= i;
}



void
STlib_updateBinIcon
( st_binicon_t*		bi )
{
    if (*bi->on)
    {
	V_DrawPatchScaled(
	    bi->x, bi->y,
	    bi->p->width * menuscale, bi->p->height * menuscale,
	    bi->p);

	bi->oldval = *bi->val;
    }
}

