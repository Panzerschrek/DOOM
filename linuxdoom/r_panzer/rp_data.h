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


#ifndef __RP_DATA__
#define __RP_DATA__

#include "rp_defs.h"

wall_texture_t*		RP_GetWallTexture(int num);
flat_texture_t*		RP_GetFlatTexture(int num);
sprite_picture_t*	RP_GetSpritePicture(int lumpnum);
sprite_picture_t*	RP_GetSpritePictureTranslated(int lumpnum, int translation_num);
sky_texture_t*		RP_GetSkyTexture();
int* 			RP_GetLightingGammaTable();

#endif//__RP_DATA__
