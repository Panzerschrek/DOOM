#include <limits.h>
#include <math.h>

#include "rp_main.h"
#include "rp_video.h"
#include "rp_data.h"
#include "rp_plane.h"

#include "../m_fixed.h"
#include "../p_setup.h"
#include "../r_main.h"
#include "../r_sky.h"
#include "../r_things.h"
#include "../tables.h"
#include "../v_video.h"

// special value for inv_z and u/z interpolations
#define PR_SEG_PART_BITS 4

// magic constanst for beautifulizing of texture mapping on slope walls and floors
#define PR_SEG_U_MIP_SCALER 2
#define PR_FLAT_MIP_SCALER 4

#define PR_FLAT_PART_BITS 14

#define RP_Z_NEAR_FIXED (8 * 65536)

#define RP_HALF_FOV_X ANG45


typedef struct screen_vertex_s
{
    fixed_t	x;
    fixed_t	y;
    fixed_t	z;
} screen_vertex_t;

static float		g_view_matrix[16];
static fixed_t		g_view_pos[3];
static int		g_view_angle; // angle number in sin/cos/tan tables
static fixed_t		g_half_fov_tan;
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
    fixed_t	z[2];

    fixed_t	tc_u_offset;
    fixed_t	length;
} g_cur_seg_data;

static struct
{
    // additional 3 vertices - for clipping by 3 planes
    vertex_t	clipped_vertices[ RP_MAX_SUBSECTOR_VERTICES + 3 ];
    int		vertex_count;
} g_cur_subsector_data;

static struct
{
    //TODO - remove static limit
    int		x_min[ MAX_SCREENHEIGHT ];
    int		x_max[ MAX_SCREENHEIGHT ];
    int		y_min;
    int		y_max;

    int		top_vertex_index;
    int		bottom_vertex_index;
} g_cur_screen_polygon;

//TODO - remove static limit
static int	g_y_to_sky_u_table[ MAX_SCREENHEIGHT ];


extern int	skyflatnum;
extern int	skytexture;


// input - in range [0;255]
static void SetLightLevel(int level)
{
    g_cur_column_light = level * 258;
}

// uses g_cur_column_light
static inline pixel_t LightPixel(pixel_t p)
{
   p.components[0] = (p.components[0] * g_cur_column_light) >> 16;
   p.components[1] = (p.components[1] * g_cur_column_light) >> 16;
   p.components[2] = (p.components[2] * g_cur_column_light) >> 16;
   return p;
}

static int IntLog2Floor(int x)
{
    int i = -1;
    while( x > 0)
    {
	x>>= 1;
	i++;
    }
    return i >= 0 ? i : 0;
}

static int PositiveMod( int x, int y )
{
	int div= x/y;
	if( div * y >x ) div--;
	return x - div * y;
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
    g_cur_seg_data.z[0] = FloatToFixed(proj_z[0]);
    g_cur_seg_data.z[1] = FloatToFixed(proj_z[1]);
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

static void ClipCurSubsector(int subsector_num)
{
    int			i;
    full_subsector_t*	full_subsector;
    vertex_t*		full_subsector_vertices;

    full_subsector = R_32b_GetFullSubsectors() + subsector_num;
    full_subsector_vertices = R_32b_GetFullSubsectorsVertices() + full_subsector->first_vertex;

    g_cur_subsector_data.vertex_count = full_subsector->numvertices;
    for( i = 0; i < full_subsector->numvertices; i++ )
	g_cur_subsector_data.clipped_vertices[i] = full_subsector_vertices[i];

    for( i = 0; i < 3; i++ )
    {
	g_cur_subsector_data.vertex_count =
	    R_32b_ClipPolygon(
		g_cur_subsector_data.clipped_vertices,
		g_cur_subsector_data.vertex_count,
		&g_clip_planes[i]);
	if (g_cur_subsector_data.vertex_count == 0 ) return;
    }
}

static void PreparePolygon(screen_vertex_t* vertices, int vertex_count, boolean direction)
{
    int		i, cur_i, next_i;
    int		tmp;
    int*	side;
    int 	y_begin;
    int		y_end;
    int		y;
    fixed_t	x, x_step;
    fixed_t	dy;

    g_cur_screen_polygon.y_min = INT_MAX;
    g_cur_screen_polygon.y_max = INT_MIN;

    for( i = 0; i < vertex_count; i++ )
    {
	cur_i = i;
	next_i = cur_i + 1;
	if (next_i == vertex_count) next_i = 0;

	dy = vertices[next_i].y - vertices[cur_i].y;
	if (dy == 0) continue;

	if( dy < 0 )
	{
	    dy = -dy;
	    tmp = next_i;
	    next_i = cur_i;
	    cur_i = tmp;
	    side = direction ? g_cur_screen_polygon.x_min : g_cur_screen_polygon.x_max;
	}
	else side = direction ? g_cur_screen_polygon.x_max : g_cur_screen_polygon.x_min;

	x_step = FixedDiv(vertices[next_i].x - vertices[cur_i].x, dy);
	x = vertices[cur_i].x;

	y_begin = FixedRoundToInt(vertices[cur_i ].y);
	if( y_begin < 0 ) y_begin = 0;
	y_end   = FixedRoundToInt(vertices[next_i].y);
	if( y_end > SCREENHEIGHT) y_end = SCREENHEIGHT;
	y = y_begin;

	x += FixedMul(x_step, FRACUNIT/2 + (y_begin << FRACBITS) - vertices[cur_i ].y);

	while (y < y_end)
	{
	    side[y] = FixedRoundToInt(x);
	    x+= x_step;
	    y++;
	}

	if (g_cur_screen_polygon.y_min == INT_MAX || vertices[cur_i ].y < vertices[g_cur_screen_polygon.top_vertex_index].y)
	{
	    g_cur_screen_polygon.y_min = y_begin;
	    g_cur_screen_polygon.top_vertex_index = cur_i;
	}
	if (g_cur_screen_polygon.y_max == INT_MIN || vertices[next_i].y > vertices[g_cur_screen_polygon.bottom_vertex_index].y)
	{
	    g_cur_screen_polygon.y_max = y_end;
	    g_cur_screen_polygon.bottom_vertex_index = next_i;
	}
    }
}

void RP_PrepareSky(player_t* player)
{
    int		x;
    fixed_t 	sign, cur_x_tan;
    fixed_t	tan_scaler;
    int		tan_num, angle_num, final_angle_num;
    int		pixel_num;
    int		sky_tex_pixels;

    sky_tex_pixels = ID_SKY_TEXTURE_REPEATS * GetSkyTexture()->width;

    tan_scaler = -FixedDiv(FRACUNIT, finetangent[RP_HALF_FOV_X >> ANGLETOFINESHIFT]);

    for (x = 0; x < SCREENWIDTH; x++)
    {
	cur_x_tan = FixedDiv((x<<FRACBITS) - (SCREENWIDTH<<FRACBITS)/2, (SCREENWIDTH<<FRACBITS)/2);
	cur_x_tan = FixedMul(cur_x_tan, tan_scaler);

	if (cur_x_tan > 0)
	    sign = 1;
	else
	{
		sign = -1;
		cur_x_tan = -cur_x_tan;
	}

	// TODO - tantoangle can not tangent > 1 and angle > 45deg
	// handle this case

	if (cur_x_tan <= FRACUNIT)
	{
	    tan_num = cur_x_tan >> (FRACBITS - SLOPEBITS);
	    angle_num = sign * (tantoangle[tan_num]>>ANGLETOFINESHIFT);
	}
	else
	{
	    tan_num = FixedDiv(FRACUNIT, cur_x_tan) >> (FRACBITS - SLOPEBITS);
	    angle_num = sign * ( ANG45 - (tantoangle[tan_num]>>ANGLETOFINESHIFT));
	}

	final_angle_num = ((player->mo->angle>>ANGLETOFINESHIFT) - angle_num) & FINEMASK;
	pixel_num = (final_angle_num * sky_tex_pixels / FINEANGLES) % GetSkyTexture()->width;

	g_y_to_sky_u_table[x] = pixel_num;
    }
}

void RP_DrawSkyPolygon()
{
    int			y;
    int			x, x_begin, x_end;
    sky_texture_t*	texture;
    pixel_t*		framebuffer;
    pixel_t*		dst;
    pixel_t*		src;

    texture = GetSkyTexture();
    framebuffer = VP_GetFramebuffer();
    src = texture->data;

    // TODO - adopt for fov and aspect ratio

    for (y = g_cur_screen_polygon.y_min; y < g_cur_screen_polygon.y_max; y++)
    {
	x_begin = g_cur_screen_polygon.x_min[y];
	if (x_begin < 0) x_begin = 0;

	x_end = g_cur_screen_polygon.x_max[y];
	if (x_end > SCREENWIDTH) x_end = SCREENWIDTH;

	dst = framebuffer + x_begin + y * SCREENWIDTH;
	src = texture->data + ((y * ID_SCREENHEIGHT / SCREENHEIGHT) % texture->height) * texture->width;

	for (x = x_begin; x < x_end; x++, dst++)
	    *dst = src[ g_y_to_sky_u_table[x] ];
    }
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

    angle_num = ((ANG90 - player->mo->angle) >> ANGLETOFINESHIFT ) & FINEMASK;

    g_view_pos[0] = player->mo->x;
    g_view_pos[1] = player->mo->y;
    g_view_pos[2] = player->viewz;
    g_view_angle = (player->mo->angle >> ANGLETOFINESHIFT ) & FINEMASK;
    g_half_fov_tan = -finetangent[RP_HALF_FOV_X >> ANGLETOFINESHIFT];

    RP_MatIdentity(translate_matrix);
    translate_matrix[12] = - FixedToFloat(player->mo->x);
    translate_matrix[13] = - FixedToFloat(player->mo->y);
    translate_matrix[14] = - FixedToFloat(player->viewz);

    RP_MatIdentity(rotate_matrix);
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

void PR_DrawWallPart(fixed_t top_tex_offset, fixed_t z_min, fixed_t z_max, boolean draw_as_sky)
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

    if (draw_as_sky)
    {
    	screen_vertex_t sky_polygon_vertices[4];

	sky_polygon_vertices[0].x = g_cur_seg_data.screen_x[0];
	sky_polygon_vertices[0].y = screen_y[0];
	sky_polygon_vertices[1].x = g_cur_seg_data.screen_x[0];
	sky_polygon_vertices[1].y = screen_y[1];
	sky_polygon_vertices[2].x = g_cur_seg_data.screen_x[1];
	sky_polygon_vertices[2].y = screen_y[3];
	sky_polygon_vertices[3].x = g_cur_seg_data.screen_x[1];
	sky_polygon_vertices[3].y = screen_y[2];

	PreparePolygon(sky_polygon_vertices, 4, true);
	RP_DrawSkyPolygon();
    	return;
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

    fixed_t tex_width [RP_MAX_WALL_MIPS];
    fixed_t tex_height[RP_MAX_WALL_MIPS];
    fixed_t mip_tc_u_scaler[RP_MAX_WALL_MIPS];
    fixed_t mip_tc_v_scaler[RP_MAX_WALL_MIPS];
    for( i = 0; i <= g_cur_wall_texture->max_mip; i++ )
    {
	tex_width [i] = (g_cur_wall_texture->width  >> i) << FRACBITS;
	tex_height[i] = (g_cur_wall_texture->height >> i) << FRACBITS;
	mip_tc_u_scaler[i] = FixedDiv(tex_width [i], tex_width [0]);
	mip_tc_v_scaler[i] = FixedDiv(tex_height[i], tex_height[0]);
    }

    fixed_t vert_u[2];
    vert_u[0] = PositiveMod(g_cur_side->textureoffset + g_cur_seg_data.tc_u_offset, tex_width[0]);
    vert_u[1] = vert_u[0] + g_cur_seg_data.length;
    fixed_t u_div_z[2];
    u_div_z[0] = FixedDiv(vert_u[0], g_cur_seg_data.z[0]);
    u_div_z[1] = FixedDiv(vert_u[1], g_cur_seg_data.z[1]);

    // for calculation of u mip
    fixed_t u_div_z_step;
    fixed_t inv_z_step;
    u_div_z_step = FixedDiv(u_div_z[1] - u_div_z[0], dx);
    inv_z_step = FixedDiv(g_cur_seg_data.inv_z[1] - g_cur_seg_data.inv_z[0], dx);

    // interpolate value in range [0; 1 ^ PR_SEG_PART_BITS ]
    // becouse direct interpolation of u/z and 1/z can be inaccurate
    fixed_t part_step = FixedDiv(FRACUNIT << PR_SEG_PART_BITS, dx);
    fixed_t part = FixedMul(part_step, ddx);

    pixel_t* dst;
    pixel_t* dst_end;
    pixel_t* src;
    pixel_t pixel;
    SetLightLevel(g_cur_side->sector->lightlevel);
    while (x < x_end)
    {
	fixed_t one_minus_part = (FRACUNIT<<PR_SEG_PART_BITS) - part;
	fixed_t cur_u_div_z = FixedMul(part, u_div_z[1]) + FixedMul(one_minus_part, u_div_z[0]);
	fixed_t inv_z = FixedDiv(part, g_cur_seg_data.z[1]) + FixedDiv(one_minus_part, g_cur_seg_data.z[0]);
	fixed_t u = FixedDiv(cur_u_div_z, inv_z);

	fixed_t du_dx = FixedDiv(u_div_z_step - FixedMul(u, inv_z_step), inv_z >> PR_SEG_PART_BITS);
	int u_mip = IntLog2Floor((du_dx / PR_SEG_U_MIP_SCALER) >> FRACBITS);

	int y_begin = FixedRoundToInt(top_y   );
	if (y_begin < 0 ) y_begin = 0;
	int y_end   = FixedRoundToInt(bottom_y);
	if (y_end > SCREENHEIGHT) y_end = SCREENHEIGHT;

	if (y_end <= y_begin) goto x_loop_end; // can be, in some cases

	int y = y_begin;

	fixed_t ddy = (y_begin<<FRACBITS) + FRACUNIT/2 - top_y;

	dst = framebuffer + x + y * SCREENWIDTH;
	dst_end = framebuffer + x + y_end * SCREENWIDTH;

	fixed_t v_step;
	v_step = FixedDiv(z_max - z_min, bottom_y - top_y);
	fixed_t v = top_tex_offset + g_cur_side->rowoffset + FixedMul(ddy, v_step);

	int v_mip = IntLog2Floor(v_step >> FRACBITS);

	int mip = u_mip > v_mip ? u_mip : v_mip;
	if (mip > g_cur_wall_texture->max_mip) mip = g_cur_wall_texture->max_mip;

	u = FixedMul(mip_tc_u_scaler[mip], u);
	v = FixedMul(mip_tc_v_scaler[mip], v);
	v_step = FixedMul(v_step, mip_tc_v_scaler[mip]);
	if( u >= tex_width [mip]) u %= tex_width [mip];
	fixed_t cur_mip_tex_heigth = tex_height[mip];

	src = g_cur_wall_texture->mip[mip] + (u>>FRACBITS) * (g_cur_wall_texture->height >> mip);

	if( g_cur_wall_texture_transparent)
	    while (dst < dst_end) // draw alpha-tested (TODO - alpha - blend)
	    {
		if (v >= cur_mip_tex_heigth) v %= cur_mip_tex_heigth;
		pixel = src[ (v>>FRACBITS) ];
		if( pixel.components[3] >= 128 ) *dst = LightPixel(pixel);
		dst += SCREENWIDTH;
		v += v_step;
	    }
	else
	    while (dst < dst_end) // draw solid
	    {
		if (v >= cur_mip_tex_heigth) v %= cur_mip_tex_heigth;
		*dst = LightPixel(src[ (v>>FRACBITS) ]);
		//dst->components[0] = dst->components[1] = dst->components[2] = 32 * mip + 32;
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

    if(g_cur_seg->frontsector && g_cur_seg->backsector && (g_cur_seg->linedef->flags & ML_TWOSIDED))
    {
	boolean bottom_is_sky = g_cur_seg->frontsector->floorpic   == skyflatnum && g_cur_seg->backsector->floorpic   == skyflatnum;
	boolean    top_is_sky = g_cur_seg->frontsector->ceilingpic == skyflatnum && g_cur_seg->backsector->ceilingpic == skyflatnum;

	// bottom texture
	if (g_cur_seg->backsector->floorheight > g_cur_seg->frontsector->floorheight)
	{
	    ProjectCurSeg(); seg_projected = true;

	    g_cur_wall_texture = GetWallTexture(texturetranslation[g_cur_side->bottomtexture]);
	    g_cur_wall_texture_transparent = false;

	    if (g_cur_seg->linedef->flags & ML_DONTPEGBOTTOM)
	    {
		fixed_t top = g_cur_seg->frontsector->ceilingheight > g_cur_seg->backsector->ceilingheight ? g_cur_seg->frontsector->ceilingheight : g_cur_seg->backsector->ceilingheight;
		v_offset =
		    PositiveMod(
			-g_cur_seg->backsector->floorheight + top,
			g_cur_wall_texture->height << FRACBITS);
	    }
	    else v_offset = 0;

	    PR_DrawWallPart(
		v_offset,
		g_cur_seg->frontsector->floorheight,
		g_cur_seg->backsector->floorheight,
		bottom_is_sky);
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
		g_cur_seg->frontsector->ceilingheight,
		top_is_sky);
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
		    h + (g_cur_wall_texture->height << FRACBITS),
		    false);
	    }
	    else
	    {
		h = g_cur_seg->frontsector->ceilingheight < g_cur_seg->backsector->ceilingheight
		    ? g_cur_seg->frontsector->ceilingheight
		    : g_cur_seg->backsector->ceilingheight;

		PR_DrawWallPart(
		    0,
		    h - (g_cur_wall_texture->height << FRACBITS),
		    h,
		    false);
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
	    g_cur_seg->frontsector->ceilingheight,
	    false);
    }
}

void PR_DrawSubsectorFlat(int subsector_num, boolean is_floor)
{
    int			i;
    subsector_t*	subsector;

    subsector = &subsectors[subsector_num];

    fixed_t height = is_floor ? subsector->sector->floorheight : subsector->sector->ceilingheight;
    int texture_num = flattranslation[is_floor ? subsector->sector->floorpic : subsector->sector->ceilingpic];

    screen_vertex_t vertices_proj[RP_MAX_SUBSECTOR_VERTICES + 3];

    for( i = 0; i < g_cur_subsector_data.vertex_count; i++)
    {
	float f_vertex[3];

	f_vertex[0] = FixedToFloat(g_cur_subsector_data.clipped_vertices[i].x);
	f_vertex[1] = FixedToFloat(g_cur_subsector_data.clipped_vertices[i].y);
	f_vertex[2] = FixedToFloat(height);

	float proj[3];
	RP_VecMatMul(f_vertex, g_view_matrix, proj);

	proj[0] /= proj[2];
	proj[1] /= proj[2];

	vertices_proj[i].x = FloatToFixed((proj[0] + 1.0f ) * ((float)SCREENWIDTH ) * 0.5f );
	vertices_proj[i].y = FloatToFixed((proj[1] + 1.0f ) * ((float)SCREENHEIGHT) * 0.5f );
	vertices_proj[i].z = FloatToFixed(proj[2]);
    }

    PreparePolygon(vertices_proj, g_cur_subsector_data.vertex_count, is_floor);

    if (texture_num == skyflatnum)
    {
	RP_DrawSkyPolygon();
	return;
    }

    screen_vertex_t*    top_vertex = &vertices_proj[g_cur_screen_polygon.top_vertex_index   ];
    screen_vertex_t* bottom_vertex = &vertices_proj[g_cur_screen_polygon.bottom_vertex_index];

    SetLightLevel(subsector->sector->lightlevel);
    flat_texture_t* texture = GetFlatTexture(texture_num);

    fixed_t dy = bottom_vertex->y - top_vertex->y;
    fixed_t part_step = FixedDiv(FRACUNIT << PR_FLAT_PART_BITS, dy);
    fixed_t ddy = (g_cur_screen_polygon.y_min<<FRACBITS) + FRACUNIT/2 - top_vertex->y;
    fixed_t part = FixedMul(ddy, part_step);

    fixed_t inv_z_scaled_step = FixedDiv( // value is negative
        FixedDiv(FRACUNIT << PR_FLAT_PART_BITS,    top_vertex->z) -
        FixedDiv(FRACUNIT << PR_FLAT_PART_BITS, bottom_vertex->z), dy );
    inv_z_scaled_step = abs(inv_z_scaled_step);

    fixed_t uv_start[2];
    fixed_t uv_dir[2];
    fixed_t uv_per_dir[2];
    uv_start[0] = g_view_pos[0];
    uv_start[1] = -g_view_pos[1];
    uv_dir[0] =  finecosine[g_view_angle];
    uv_dir[1] =  -finesine [g_view_angle];
    uv_per_dir[0] = -uv_dir[1];
    uv_per_dir[1] =  uv_dir[0];

    // TODO - optimize this
    fixed_t uv_line_step_on_z1 = FixedDiv((2 << FRACBITS) / SCREENWIDTH, g_half_fov_tan);
    int y;
    for( y = g_cur_screen_polygon.y_min; y < g_cur_screen_polygon.y_max; y++, part+= part_step )
    {
	fixed_t inv_z_scaled = // (1<<PR_FLAT_PART_BITS) / z
	    FixedDiv(part, bottom_vertex->z) +
	    FixedDiv((FRACUNIT<<PR_FLAT_PART_BITS) - part, top_vertex->z);
	fixed_t z = FixedDiv(FRACUNIT<<PR_FLAT_PART_BITS, inv_z_scaled);

	int y_mip =
	    IntLog2Floor((FixedMul(FixedDiv(inv_z_scaled_step, inv_z_scaled), z) / PR_FLAT_MIP_SCALER) >> FRACBITS);

	int x_begin = g_cur_screen_polygon.x_min[y];
	if (x_begin < 0 ) x_begin = 0;
	int x_end = g_cur_screen_polygon.x_max[y];
	if( x_end > SCREENWIDTH) x_end = SCREENWIDTH;
	int x;
	pixel_t* dst = VP_GetFramebuffer() + x_begin + y * SCREENWIDTH;

	fixed_t line_duv_scaler = FixedMul(uv_line_step_on_z1, z);
	fixed_t u = FixedMul(z, uv_dir[0]) + uv_start[0];
	fixed_t v = FixedMul(z, uv_dir[1]) + uv_start[1];
	fixed_t du_dx = FixedMul(uv_per_dir[0], line_duv_scaler);
	fixed_t dv_dx = FixedMul(uv_per_dir[1], line_duv_scaler);

	fixed_t center_offset = (x_begin<<FRACBITS) - (SCREENWIDTH<<FRACBITS)/2 + FRACUNIT/2;
	u += FixedMul(center_offset, du_dx);
	v += FixedMul(center_offset, dv_dx);

	// mip = log(sqrt(du *du + dv * dv)) = log(du *du + dv * dv) / 2
	// add small shift for prevention of overflow
	int x_mip = IntLog2Floor((
	    FixedMul(du_dx>>2, du_dx>>2) +
	    FixedMul(dv_dx>>2, dv_dx>>2) ) >> (FRACBITS-4) ) >> 1;

	int mip = y_mip > x_mip ? y_mip : x_mip;
	if (mip > RP_FLAT_TEXTURE_SIZE_LOG2) mip = RP_FLAT_TEXTURE_SIZE_LOG2;
	int texel_fetch_shift = RP_FLAT_TEXTURE_SIZE_LOG2 - mip;
	int texel_fetch_mask = (1<<texel_fetch_shift) - 1;
	u>>=mip; v>>=mip;
	du_dx>>=mip; dv_dx>>=mip;

	pixel_t pixel;
	pixel_t* src = texture->mip[mip];
	for( x = x_begin; x < x_end; x++, dst++, u += du_dx, v += dv_dx )
	{
	    pixel = src[ ((u>>FRACBITS)&texel_fetch_mask) + (((v>>FRACBITS)&texel_fetch_mask) << texel_fetch_shift) ];
	    *dst = LightPixel(pixel);
	}
    }
}

void RP_Subsector(int num)
{
    subsector_t*	sub;
    int			line_seg;
    boolean		subsector_clipped;

    sub = &subsectors[num];

    for( line_seg = sub->firstline; line_seg < sub->numlines + sub->firstline; line_seg++ )
    {
    	g_cur_seg = &segs[ line_seg ];
	PR_DrawWall();
    }
    subsector_clipped = false;

    if (g_view_pos[2] > sub->sector->floorheight  )
    {
	ClipCurSubsector(num);
	subsector_clipped = true;
	if (g_cur_subsector_data.vertex_count > 0)
	    PR_DrawSubsectorFlat( num, true  );
    }
    if (g_view_pos[2] < sub->sector->ceilingheight)
    {
	if (!subsector_clipped) ClipCurSubsector(num);
	if (g_cur_subsector_data.vertex_count > 0)
	    PR_DrawSubsectorFlat( num, false );
    }
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
    pixel_t* framebuffer = VP_GetFramebuffer();
    int y, x;
    pixel_t colors[2] = { VP_GetPaletteStorage()[2], VP_GetPaletteStorage()[34] };
    for( y = 0; y < SCREENHEIGHT; y++ )
	for( x = 0; x < SCREENWIDTH; x++, framebuffer++ )
	    *framebuffer = colors[ ((x>>1) ^ (y>>1)) & 1 ];

    RP_BuildViewMatrix(player);
    RP_BuildClipPlanes(player);
    RP_PrepareSky(player);

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
