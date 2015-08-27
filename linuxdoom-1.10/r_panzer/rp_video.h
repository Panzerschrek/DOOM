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


#ifndef __RP_VIDEO__
#define __RP_VIDEO__

#include "rp_defs.h"


void VP_Init();
void VP_SetupFramebuffer(void* framebuffer);

pixel_t* VP_GetPaletteStorage();
pixel_t* VP_GetFramebuffer();

#endif//__RP_VIDEO__
