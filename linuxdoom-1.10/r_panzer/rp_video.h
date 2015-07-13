#ifndef __RP_VIDEO__
#define __RP_VIDEO__

#include "rp_defs.h"


void VP_Init();
void VP_SetupFramebuffer(void* framebuffer);

pixel_t* VP_GetPaletteStorage();
pixel_t* VP_GetFramebuffer();

#endif//__RP_VIDEO__
