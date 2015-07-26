#ifndef __RP_DEFS__
#define __RP_DEFS__

#include "../doomtype.h"
#include "../r_defs.h"

typedef union pixel_u
{
    byte components[4];
    unsigned int p;
} pixel_t;

// plane equation
// for point on plane n[0] * x + n[1] * y + dist = 0
// for point in front of plane n[0] * x + n[1] * y + dist > 0
typedef struct clip_plane_s
{
    fixed_t n[2];
    fixed_t dist;
} clip_plane_t;

// subsector for rendering
// contains all vertices of subsector
typedef struct full_subsector_s
{
    int		first_vertex;
    int		numvertices;
} full_subsector_t;

/*
512 x 512
256 x 256
128 x 128
 64 x  64
 32 x  32
 16 x  16
  8 x   8
  4 x   4
  2 x   2
  1 x   1
*/
#define RP_MAX_WALL_MIPS		10

#define RP_FLAT_TEXTURE_SIZE_LOG2	6
#define RP_FLAT_TEXTURE_SIZE		64
#define RP_FLAT_TEXTURE_SIZE_MINUS_1	(RP_FLAT_TEXTURE_SIZE-1)

typedef struct wall_texture_s
{
    char	name[8];

    int		width;
    int		height;

    // allocated memory for this texture and all mips
    pixel_t*	raw_data;
    // pointers to pixels of mip in allocated memory. mip[0] = raw_data
    pixel_t*	mip[ RP_MAX_WALL_MIPS ];
    int		max_mip;

    boolean used;
} wall_texture_t;

typedef struct flat_texture_s
{
    // same as in wall_texture_t
    pixel_t*	raw_data;
    pixel_t*	mip[ RP_FLAT_TEXTURE_SIZE_LOG2 + 1 ];

    boolean used;
} flat_texture_t;

#endif//__RP_DEFS__
