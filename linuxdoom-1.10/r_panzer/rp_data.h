#ifndef __RP_DATA__
#define __RP_DATA__

#include "rp_defs.h"

wall_texture_t*		GetWallTexture(int num);
flat_texture_t*		GetFlatTexture(int num);
sprite_picture_t*	GetSpritePicture(int lumpnum);
sky_texture_t*		GetSkyTexture();
int* 			GetLightingGammaTable();

#endif//__RP_DATA__
