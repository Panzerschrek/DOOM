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
//  Refresh module, data I/O, caching, retrieval of graphics
//  by name.
//
//-----------------------------------------------------------------------------


#ifndef __R_DATA__
#define __R_DATA__

#include "r_defs.h"
#include "r_state.h"

#ifdef __GNUG__
#pragma interface
#endif

// Retrieve column data for span blitting.
byte*
R_GetColumn
( int		tex,
  int		col );


// I/O, setting up the stuff.
extern void (*R_InitData) (void);
extern void (*R_PrecacheLevel) (void);


// Retrieval.
// Floor/ceiling opaque texture tiles,
// lookup by name. For animation?
extern int (*R_FlatNumForName) (char* name);


// Called by P_Ticker for switches and animations,
// returns the texture number for the texture name.
extern int (*R_TextureNumForName) (char *name);
extern int (*R_CheckTextureNumForName) (char *name);

#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
