#include <math.h>

#include "rp_main.h"
#include "rp_video.h"
#include "rp_data.h"

#include "../m_fixed.h"
#include "../p_setup.h"
#include "../r_main.h"
#include "../r_things.h"
#include "../tables.h"
#include "../v_video.h"

// special value for inv_z and u/z interpolations
#define PR_SEG_PART_BITS 5

#define RP_Z_NEAR_FIXED (8 * 65536)

#define RP_HALF_FOV_X ANG45

// plane equation
// for point on plane n[0] * x + n[1] * y + dist = 0
// for point in front of plane n[0] * x + n[1] * y + dist > 0
typedef struct clip_plane_s
{
    fixed_t n[2];
    fixed_t dist;
} clip_plane_t;

static float		g_view_matrix[16];
static fixed_t		g_view_pos[3];
static clip_plane_t	g_clip_planes[3]; // 0 - near, 1 - left, 2 - right

static seg_t*		g_cur_seg;
static side_t*		g_cur_side;
static wall_texture_t*	g_cur_wall_texture;
static boolean		g_cur_wall_texture_transparent;
static int		g_cur_column_light; // in range [0; 255 * 258]

static struct
{
    vertex_t	v[2];
    fixed_t	screen_x[2];
    float	screen_z[2];
    fixed_t	inv_z[2];

    fixed_t	tc_u_offset;
    fixed_t	length;
} g_cur_seg_data;

// uses g_cur_column_light
static inline pixel_t LightPixel(pixel_t p)
{
   p.components[0] = (p.components[0] * g_cur_column_light) >> 16;
   p.components[1] = (p.components[1] * g_cur_column_light) >> 16;
   p.components[2] = (p.components[2] * g_cur_column_light) >> 16;
   return p;
}

static int PositiveMod( int x, int y )
{
	int div= x/y;
	if( div * y >x ) div--;
	return x - div * y;
}

static float FixedToFloat(fixed_t f)
{
    return ((float)f) / ((float)FRACUNIT);
}

static fixed_t FloatToFixed(float f)
{
    return (fixed_t)(f * ((float)FRACUNIT));
}

static int FixedRoundToInt(fixed_t f)
{
    return (f + FRACUNIT/2)>>FRACBITS;
}

static fixed_t GetSegLength(seg_t* seg)
{
    // TODO - remove sqrt and type convertions
    float d[2] = { FixedToFloat(seg->v1->x - seg->v2->x), FixedToFloat(seg->v1->y - seg->v2->y) };
    return FloatToFixed( sqrt( d[0] * d[0] + d[1] * d[1] ) );
}

static void ProjectCurSeg()
{
    float pos_x[2];
    float pos_y[2];
    float proj_x[2];
    float proj_z[2];

    pos_x[0] = FixedToFloat(g_cur_seg_data.v[0].x);
    pos_y[0] = FixedToFloat(g_cur_seg_data.v[0].y);

    pos_x[1] = FixedToFloat(g_cur_seg_data.v[1].x);
    pos_y[1] = FixedToFloat(g_cur_seg_data.v[1].y);

    proj_x[0] = pos_x[0] * g_view_matrix[0] + pos_y[0] * g_view_matrix[4] + g_view_matrix[12];
    proj_z[0] = pos_x[0] * g_view_matrix[2] + pos_y[0] * g_view_matrix[6] + g_view_matrix[14];

    proj_x[1] = pos_x[1] * g_view_matrix[0] + pos_y[1] * g_view_matrix[4] + g_view_matrix[12];
    proj_z[1] = pos_x[1] * g_view_matrix[2] + pos_y[1] * g_view_matrix[6] + g_view_matrix[14];

    proj_x[0] /= proj_z[0];
    proj_x[1] /= proj_z[1];

    g_cur_seg_data.screen_x[0] = FloatToFixed((proj_x[0] + 1.0f ) * ((float) SCREENWIDTH) * 0.5f );
    g_cur_seg_data.screen_x[1] = FloatToFixed((proj_x[1] + 1.0f ) * ((float) SCREENWIDTH) * 0.5f );
    g_cur_seg_data.screen_z[0] = proj_z[0];
    g_cur_seg_data.screen_z[1] = proj_z[1];

    g_cur_seg_data.inv_z[0] = FloatToFixed(1.0f / proj_z[0]);
    g_cur_seg_data.inv_z[1] = FloatToFixed(1.0f / proj_z[1]);
}

// returns true if segment fully clipped
static boolean ClipCurSeg()
{
    fixed_t	dot[2];
    fixed_t	xy[2];
    fixed_t	part;
    fixed_t	lost_length;
    int		i;

    g_cur_seg_data.v[0].x = g_cur_seg->v1->x;
    g_cur_seg_data.v[0].y = g_cur_seg->v1->y;
    g_cur_seg_data.v[1].x = g_cur_seg->v2->x;
    g_cur_seg_data.v[1].y = g_cur_seg->v2->y;
    g_cur_seg_data.tc_u_offset = g_cur_seg->offset;
    g_cur_seg_data.length = GetSegLength(g_cur_seg);

    for( i = 0; i < 3; i++ )
    {
    	dot[0] =
	    FixedMul(g_cur_seg_data.v[0].x, g_clip_planes[i].n[0]) +
	    FixedMul(g_cur_seg_data.v[0].y, g_clip_planes[i].n[1]) +
	    g_clip_planes[i].dist;
	dot[1] =
	    FixedMul(g_cur_seg_data.v[1].x, g_clip_planes[i].n[0]) +
	    FixedMul(g_cur_seg_data.v[1].y, g_clip_planes[i].n[1]) +
	    g_clip_planes[i].dist;
	if( dot[0] <= 0 && dot[1] <= 0) return true;
	if( dot[0] >= 0 && dot[1] >= 0) continue;

	if (dot[1] > 0 )
	{
		part = FixedDiv(-dot[0], -dot[0] + dot[1]);
		xy[0] = g_cur_seg_data.v[0].x + FixedMul(part, g_cur_seg_data.v[1].x - g_cur_seg_data.v[0].x);
		xy[1] = g_cur_seg_data.v[0].y + FixedMul(part, g_cur_seg_data.v[1].y - g_cur_seg_data.v[0].y);
		g_cur_seg_data.v[0].x = xy[0];
		g_cur_seg_data.v[0].y = xy[1];

		lost_length = FixedMul(part, g_cur_seg_data.length );
		g_cur_seg_data.tc_u_offset += lost_length;
		g_cur_seg_data.length -= lost_length;
	}
	else
	{
		part = FixedDiv(-dot[1], dot[0] - dot[1]);
		xy[0] = g_cur_seg_data.v[1].x + FixedMul(part, g_cur_seg_data.v[0].x - g_cur_seg_data.v[1].x);
		xy[1] = g_cur_seg_data.v[1].y + FixedMul(part, g_cur_seg_data.v[0].y - g_cur_seg_data.v[1].y);
		g_cur_seg_data.v[1].x = xy[0];
		g_cur_seg_data.v[1].y = xy[1];

		lost_length = FixedMul(part, g_cur_seg_data.length );
		g_cur_seg_data.length -= lost_length;
	}
    }
    return false;
}

void RP_MatMul(const float* mat0, const float* mat1, float* result)
{
    int i, j, k;

    for( i = 0; i< 4; i++ )
	for( j = 0; j< 16; j+=4 )
	{
	    result[ i + j ] = 0.0f;
	    for( k = 0; k < 4; k++ )
		result[i + j] += mat0[k + j] * mat1[i + k * 4];
	}
}

void RP_MatIdentity(float* mat)
{
    int i;

    for( i = 1; i < 15; i++ ) mat[i] = 0.0f;

    mat[ 0] = mat[ 5] =
    mat[10] = mat[15] = 1.0f;
}

void RP_VecMatMul( float* vec, float* mat, float* result )
{
    int i;
    for( i = 0; i < 3; i++ )
	result[i] = vec[0] * mat[i] + vec[1] * mat[i+4] + vec[2] * mat[i+8] + mat[i+12];
}

void RP_BuildViewMatrix(player_t *player)
{
    float		translate_matrix[16];
    float		rotate_matrix[16];
    float		basis_change_matrix[16];
    float		projection_matrix[16];
    float		tmp_mat[2][16];
    int			angle_num;

    g_view_pos[0] = player->mo->x;
    g_view_pos[1] = player->mo->y;
    g_view_pos[2] = player->viewz;

    RP_MatIdentity(translate_matrix);
    translate_matrix[12] = - FixedToFloat(player->mo->x);
    translate_matrix[13] = - FixedToFloat(player->mo->y);
    translate_matrix[14] = - FixedToFloat(player->viewz);

    RP_MatIdentity(rotate_matrix);
    angle_num = ((ANG90 - player->mo->angle) >> ANGLETOFINESHIFT ) & FINEMASK;
    rotate_matrix[0] =  FixedToFloat(finecosine[ angle_num ]);
    rotate_matrix[4] = -FixedToFloat(finesine  [ angle_num ]);
    rotate_matrix[1] =  FixedToFloat(finesine  [ angle_num ]);
    rotate_matrix[5] =  FixedToFloat(finecosine[ angle_num ]);

    RP_MatIdentity(basis_change_matrix);
    basis_change_matrix[ 5] = 0.0f;
    basis_change_matrix[ 6] = 1.0f;
    basis_change_matrix[ 9] = -1.0f;
    basis_change_matrix[10] = 0.0f;

    RP_MatIdentity(projection_matrix);
    // TODO: Why minus tangent?
    projection_matrix[0] = FixedToFloat(-finetangent[RP_HALF_FOV_X >> ANGLETOFINESHIFT]);
    projection_matrix[5] = projection_matrix[0] * (((float)SCREENWIDTH) / ((float)SCREENHEIGHT));

    RP_MatMul( translate_matrix, rotate_matrix, tmp_mat[0] );
    RP_MatMul( tmp_mat[0], basis_change_matrix, tmp_mat[1] );
    RP_MatMul( tmp_mat[1], projection_matrix, g_view_matrix );
}

void RP_BuildClipPlanes(player_t *player)
{
    int ang;
    int angles[3];
    int i;

    // extend a little fov, to prevent problems on screen edges
    int half_fov = RP_HALF_FOV_X + ANG90/16;

    ang = player->mo->angle;
    angles[0] = (ang >> ANGLETOFINESHIFT) & FINEMASK;
    angles[1] = ((ang - ANG90 + half_fov) >> ANGLETOFINESHIFT) & FINEMASK;
    angles[2] = ((player->mo->angle + ANG90 - half_fov) >> ANGLETOFINESHIFT) & FINEMASK;

    for( i = 0; i < 3; i++ )
    {
	g_clip_planes[i].n[0] = finecosine[angles[i]];
	g_clip_planes[i].n[1] = finesine  [angles[i]];
	g_clip_planes[i].dist = - (
	    FixedMul(g_clip_planes[i].n[0], player->mo->x) +
	    FixedMul(g_clip_planes[i].n[1], player->mo->y));
    }

    // move forward near clip plane
    g_clip_planes[0].dist -= RP_Z_NEAR_FIXED;
}

void PR_DrawWallPart(fixed_t top_tex_offset, fixed_t z_min, fixed_t z_max)
{
    float	vertex_z[4];
    fixed_t	screen_y[4];
    int		i;

    vertex_z[0] = FixedToFloat(z_min);
    vertex_z[1] = FixedToFloat(z_max);
    vertex_z[2] = FixedToFloat(z_min);
    vertex_z[3] = FixedToFloat(z_max);

    for( i = 0; i < 4; i++ )
    {
	float screen_space_y = g_view_matrix[9] * vertex_z[i] + g_view_matrix[13];
	screen_space_y /= g_cur_seg_data.screen_z[i>>1];
	screen_y[i] = FloatToFixed((screen_space_y + 1.0f ) * ((float)SCREENHEIGHT) * 0.5f );
    }

    pixel_t* framebuffer = VP_GetFramebuffer();

    fixed_t dx = g_cur_seg_data.screen_x[1] - g_cur_seg_data.screen_x[0];
    if ( dx <= 0 ) return;

    int x_begin = FixedRoundToInt(g_cur_seg_data.screen_x[0]);
    if (x_begin < 0 ) x_begin = 0;
    int x_end   = FixedRoundToInt(g_cur_seg_data.screen_x[1]);
    if (x_end > SCREENWIDTH) x_end = SCREENWIDTH;
    int x = x_begin;

    fixed_t ddx = (x_begin<<FRACBITS) + FRACUNIT/2 - g_cur_seg_data.screen_x[0];

    fixed_t top_dy =    FixedDiv(screen_y[3] - screen_y[1], dx);
    fixed_t bottom_dy = FixedDiv(screen_y[2] - screen_y[0], dx);

    fixed_t top_y =    screen_y[1] + FixedMul(ddx, top_dy   );
    fixed_t bottom_y = screen_y[0] + FixedMul(ddx, bottom_dy);

    fixed_t tex_width  = g_cur_wall_texture ->width  << FRACBITS;
    fixed_t tex_height = g_cur_wall_texture ->height << FRACBITS;

    fixed_t vert_u[2];
    vert_u[0] = PositiveMod(g_cur_side->textureoffset + g_cur_seg_data.tc_u_offset, tex_width);
    vert_u[1] = vert_u[0] + g_cur_seg_data.length;
    fixed_t u_div_z[2];
    u_div_z[0] = FixedMul(vert_u[0], g_cur_seg_data.inv_z[0]);
    u_div_z[1] = FixedMul(vert_u[1], g_cur_seg_data.inv_z[1]);

    // interpolate value in range [0; 1 ^ PR_SEG_PART_BITS ]
    // becouse direct interpolation of u/z and 1/z can be inaccurate
    fixed_t part_step = FixedDiv(FRACUNIT << PR_SEG_PART_BITS, dx);
    fixed_t part = FixedMul(part_step, ddx);

    pixel_t* dst;
    pixel_t* dst_end;
    pixel_t* src;
    pixel_t pixel;
    g_cur_column_light = g_cur_side->sector->lightlevel * 258;
    while (x < x_end)
    {
	fixed_t part_f16 = part >> PR_SEG_PART_BITS;
	fixed_t one_minus_part_f16 = FRACUNIT - (part>>PR_SEG_PART_BITS);
	fixed_t cur_u_div_z = FixedMul(part_f16, u_div_z[1]) + FixedMul(one_minus_part_f16, u_div_z[0]);
	fixed_t inv_z = FixedMul(part_f16, g_cur_seg_data.inv_z[1]) + FixedMul(one_minus_part_f16, g_cur_seg_data.inv_z[0]);
	fixed_t u = FixedDiv(cur_u_div_z, inv_z);
	if( u >= tex_width) u %= tex_width;

	int y_begin = FixedRoundToInt(top_y   );
	if (y_begin < 0 ) y_begin = 0;
	int y_end   = FixedRoundToInt(bottom_y);
	if (y_end > SCREENHEIGHT) y_end = SCREENHEIGHT;

	if (y_end <= y_begin) goto x_loop_end; // can be, in some cases

	int y = y_begin;

	fixed_t ddy = (y_begin<<FRACBITS) + FRACUNIT/2 - top_y;

	dst = framebuffer + x + y * SCREENWIDTH;
	dst_end = framebuffer + x + y_end * SCREENWIDTH;
	src = g_cur_wall_texture->mip[0] + (u>>FRACBITS) * g_cur_wall_texture->height;

	fixed_t v_step;
	v_step = FixedDiv(z_max - z_min, bottom_y - top_y);

	fixed_t v = top_tex_offset + g_cur_side->rowoffset + FixedMul(ddy, v_step);

	if( g_cur_wall_texture_transparent)
	    while (dst < dst_end) // draw alpha-tested (TODO - alpha - blend)
	    {
		if (v >= tex_height) v %= tex_height;
		pixel = src[ (v>>FRACBITS) ];
		if( pixel.components[3] >= 128 ) *dst = LightPixel(pixel);
		dst += SCREENWIDTH;
		v += v_step;
	    }
	else
	    while (dst < dst_end) // draw solid
	    {
		if (v >= tex_height) v %= tex_height;
		*dst = LightPixel(src[ (v>>FRACBITS) ]);
		dst += SCREENWIDTH;
		v += v_step;
	    }

	x_loop_end:
	top_y    += top_dy;
	bottom_y += bottom_dy;
	x++;
	part += part_step;
    } // for x
}

void PR_DrawWall()
{
    int		v_offset;
    fixed_t	seg_normal[2];
    fixed_t	vec_to_seg[2];
    fixed_t	normal_angle;
    fixed_t	dot_product;
    fixed_t	h;
    boolean	seg_projected = false;

    if(g_cur_seg->frontsector == g_cur_seg->backsector) return;
    if( ClipCurSeg() ) return;

    g_cur_side = g_cur_seg->sidedef;

    normal_angle = ((ANG90 + g_cur_seg->angle) >> ANGLETOFINESHIFT) & FINEMASK;
    seg_normal[0] = finecosine[ normal_angle ];
    seg_normal[1] = finesine  [ normal_angle ];

    vec_to_seg[0] = g_cur_seg->v1->x - g_view_pos[0];
    vec_to_seg[1] = g_cur_seg->v1->y - g_view_pos[1];

    dot_product = FixedMul(seg_normal[0], vec_to_seg[0] ) + FixedMul(seg_normal[1], vec_to_seg[1] );
    if (dot_product <= 0 ) return;

    if(g_cur_seg->frontsector && g_cur_seg->backsector)
    {
	// bottom texture
	if (g_cur_seg->backsector->floorheight > g_cur_seg->frontsector->floorheight)
	{
	    ProjectCurSeg(); seg_projected = true;

	    g_cur_wall_texture = GetWallTexture(texturetranslation[g_cur_side->bottomtexture]);
	    g_cur_wall_texture_transparent = false;

	    if (g_cur_seg->linedef->flags & ML_DONTPEGBOTTOM)
		v_offset =
		    PositiveMod(
			g_cur_seg->frontsector->floorheight - g_cur_seg->backsector->floorheight,
			g_cur_wall_texture->height << FRACBITS);
	    else v_offset = 0;

	    PR_DrawWallPart(
		v_offset,
		g_cur_seg->frontsector->floorheight,
		g_cur_seg->backsector->floorheight);
	}

	// top texture
	if (g_cur_seg->backsector->ceilingheight < g_cur_seg->frontsector->ceilingheight)
	{
	    if(!seg_projected) ProjectCurSeg();
	    seg_projected = true;

	    g_cur_wall_texture = GetWallTexture(texturetranslation[g_cur_side->toptexture]);
	    g_cur_wall_texture_transparent = false;

	    if (g_cur_seg->linedef->flags & ML_DONTPEGTOP)
		v_offset = 0;
	    else
		v_offset =
		    PositiveMod(
			g_cur_seg->backsector->ceilingheight - g_cur_seg->frontsector->ceilingheight,
			g_cur_wall_texture->height * FRACUNIT );

	    PR_DrawWallPart(
		v_offset,
		g_cur_seg->backsector->ceilingheight,
		g_cur_seg->frontsector->ceilingheight);
	}

	// middle texture
	if( g_cur_seg->sidedef->midtexture)
	{
	    if(!seg_projected) ProjectCurSeg();
	    seg_projected = true;

	    g_cur_wall_texture = GetWallTexture(texturetranslation[g_cur_side->midtexture]);
	    g_cur_wall_texture_transparent = true;

	    if (g_cur_seg->linedef->flags & ML_DONTPEGBOTTOM)
	    {
		h = g_cur_seg->frontsector->floorheight > g_cur_seg->backsector->floorheight
		    ? g_cur_seg->frontsector->floorheight
		    : g_cur_seg->backsector->floorheight;

		PR_DrawWallPart(
		    0,
		    h,
		    h + (g_cur_wall_texture->height << FRACBITS));
	    }
	    else
	    {
		h = g_cur_seg->frontsector->ceilingheight < g_cur_seg->backsector->ceilingheight
		    ? g_cur_seg->frontsector->ceilingheight
		    : g_cur_seg->backsector->ceilingheight;

		PR_DrawWallPart(
		    0,
		    h - (g_cur_wall_texture->height << FRACBITS),
		    h );
	    }
	}
    }
    else if (g_cur_seg->frontsector)
    {
	if(!seg_projected) ProjectCurSeg();
	seg_projected = true;

	g_cur_wall_texture = GetWallTexture(texturetranslation[g_cur_side->midtexture]);
	g_cur_wall_texture_transparent = false;

	if (g_cur_seg->linedef->flags & ML_DONTPEGBOTTOM)
	    v_offset =
		PositiveMod(
		    g_cur_seg->frontsector->floorheight - g_cur_seg->frontsector->ceilingheight,
		    g_cur_wall_texture->height * FRACUNIT );
	else v_offset = 0;

	PR_DrawWallPart(
	    v_offset,
	    g_cur_seg->frontsector->floorheight,
	    g_cur_seg->frontsector->ceilingheight );
    }
}

#if 0
void PR_DrawSubsectorFlat(subsector_t* sub, fixed_t height)
{
    int			line_seg;
    int			i, j;
    seg_t*		seg;

    short left_border [MAX_SCREENWIDTH];
    short right_border[MAX_SCREENWIDTH];
    int y_max = 0, y_min = SCREENHEIGHT;
    for(i = 0; i < SCREENHEIGHT; i++ )
    {
    	left_border[i] = SCREENWIDTH;
    	right_border[i] = 0;
    }

int color_index = ((int)sub) & 255;

    for( seg = segs + sub->firstline, i = 0; i< sub->numlines; seg++, i++ )
    {
    	int v_ind;
    	/*int fineangle = (seg->angle >> ANGLETOFINESHIFT) & FINEMASK;
    	if( FixedMul(finesine  [fineangle], seg->v1->x - seg->v2->x) +
	    FixedMul(finecosine[fineangle], seg->v1->y - seg->v2->y) >= 0)
	    v_ind = 0;
    	else v_ind = 1;*/
    	v_ind = 1;

	float vertices[2][3];
	int   screen_vertices[2][2];

    	vertices[0][0] = FixedToFloat( seg->v1->x );
    	vertices[0][1] = FixedToFloat( seg->v1->y );
    	vertices[0][2] = FixedToFloat(height);

    	vertices[1][0] = FixedToFloat( seg->v2->x );
    	vertices[1][1] = FixedToFloat( seg->v2->y );
    	vertices[1][2] = FixedToFloat(height);

    	for( j = 0; j < 2; j++ )
    	{
    		float vertex_proj[3];
    		RP_VecMatMul( vertices[j], g_view_matrix, vertex_proj );
		if (vertex_proj[2] < 0.0f ) return;
		vertex_proj[0] /= vertex_proj[2];
		vertex_proj[1] /= vertex_proj[2];

		screen_vertices[j][0] = (int)((vertex_proj[0] + 1.0f ) * ((float)  SCREENWIDTH) * 0.5f );
		screen_vertices[j][1] = (int)((vertex_proj[1] + 1.0f ) * ((float) SCREENHEIGHT) * 0.5f );
		if (screen_vertices[j][0] < 0 || screen_vertices[j][0] >=  SCREENWIDTH) return;
		if (screen_vertices[j][1] < 0 || screen_vertices[j][1] >= SCREENHEIGHT) return;

		if(screen_vertices[j][1] < y_min ) y_min = screen_vertices[j][1];
		if(screen_vertices[j][1] > y_max) y_max = screen_vertices[j][1];
    	}

    	int dy = screen_vertices[1][1] - screen_vertices[0][1];
    	if (dy == 0) continue;
    	//int y_step = dy > 0 ? 1 : -1;
    	v_ind = dy > 0 ? 0 : 1;
    	if (dy < 0 ) dy = -dy;
    	int y;
	fixed_t x = screen_vertices[v_ind][0] << FRACBITS;
	fixed_t x_step = ((screen_vertices[v_ind^1][0] - screen_vertices[v_ind][0]) << FRACBITS) / dy;
	int dir = v_ind ^ (dy > 0 ? 1 : 0);
	for ( y = screen_vertices[v_ind][1]; y <= screen_vertices[v_ind^1][1]; y++, x += x_step )
	{
		V_DrawPixel( x >> FRACBITS, y, dir * 64 + 128 );
	}

	x = screen_vertices[v_ind][0] << FRACBITS;
	if (!dir)
	{
	    for ( y = screen_vertices[v_ind][1]; y <= screen_vertices[v_ind^1][1]; y ++, x += x_step )
		left_border[y] = x >>FRACBITS;
	}
	else
	{
	    for ( y = screen_vertices[v_ind][1]; y <= screen_vertices[v_ind^1][1]; y ++, x += x_step )
		right_border[y] = x >>FRACBITS;
	}
    }


    int y;
    for( y = y_min; y < y_max; y++ )
    {
    	int x;
    	for( x = left_border[y]; x < right_border[y]; x++ )
    	    V_DrawPixel( x, y, color_index );
    }
}
#endif

void RP_Subsector(int num)
{
    subsector_t*	sub;
    int			line_seg;

    sub = &subsectors[num];

    for( line_seg = sub->firstline; line_seg < sub->numlines + sub->firstline; line_seg++ )
    {
    	g_cur_seg = &segs[ line_seg ];
	PR_DrawWall();
    }
    //PR_DrawSubsectorFlat( sub, sub->sector->floorheight );
    //PR_DrawSubsectorFlat( sub, sub->sector->ceilingheight );
}
//
// RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
void RP_RenderBSPNode (int bspnum)
{
    node_t*	bsp;
    int		side;

    // Found a subsector?
    if (bspnum & NF_SUBSECTOR)
    {
	if (bspnum == -1)
	    RP_Subsector (0);
	else
	    RP_Subsector (bspnum&(~NF_SUBSECTOR));
	return;
    }

    bsp = &nodes[bspnum];

    // Decide which side the view point is on.
    side = 1 ^ R_PointOnSide (g_view_pos[0], g_view_pos[1], bsp);

    // Recursively divide front space.
    RP_RenderBSPNode (bsp->children[side]);

    // Possibly divide back space.
    //if (R_CheckBBox (bsp->bbox[side^1]))
    RP_RenderBSPNode (bsp->children[side^1]);
}

void R_32b_RenderPlayerView (player_t *player)
{
    //V_FillRect( SCREENWIDTH / 2 - 2, SCREENHEIGHT / 2 - 2, 4, 4, 32 );

    V_FillRect( 0, 0, SCREENWIDTH, SCREENHEIGHT, 0 );

    RP_BuildViewMatrix(player);
    RP_BuildClipPlanes(player);

    RP_RenderBSPNode(numnodes-1);
}

// PANZER - STUBS
void R_32b_SetViewSize(int blocks,int detail){}
void R_32b_InitSprites (char** namelist){}
void R_32b_ClearSprites(){}

void R_32b_InitInterface()
{
    void R_32b_InitData (void);
    void R_32b_PrecacheLevel(void);
    int R_32b_FlatNumForName(char* name);
    int R_32b_TextureNumForName(char* name);
    int R_32b_CheckTextureNumForName(char* name);

    R_SetViewSize = R_32b_SetViewSize;
    R_RenderPlayerView = R_32b_RenderPlayerView;

    R_InitData = R_32b_InitData;
    R_InitSprites = R_32b_InitSprites;
    R_ClearSprites = R_32b_ClearSprites;
    R_PrecacheLevel = R_32b_PrecacheLevel;

    R_FlatNumForName = R_32b_FlatNumForName;
    R_TextureNumForName = R_32b_TextureNumForName;
    R_CheckTextureNumForName = R_32b_CheckTextureNumForName;
}

void RP_Init()
{
    void R_32b_InitData(void);

    R_32b_InitInterface();
    R_32b_InitData();
}
