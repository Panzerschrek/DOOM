#ifndef __RP_DEFS__
#define __RP_DEFS__

#include "../doomtype.h"

typedef union pixel_u
{
    byte components[4];
    unsigned int p;
} pixel_t;

#endif//__RP_DEFS__
