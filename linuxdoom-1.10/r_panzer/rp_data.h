#ifndef __RP_DATA__
#define __RP_DATA__

#include "rp_defs.h"

wall_texture_t*		RP_GetWallTexture(int num);
flat_texture_t*		RP_GetFlatTexture(int num);
sprite_picture_t*	RP_GetSpritePicture(int lumpnum);
sky_texture_t*		RP_GetSkyTexture();
int* 			RP_GetLightingGammaTable();

#endif//__RP_DATA__
