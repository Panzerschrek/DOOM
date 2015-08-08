#include <limits.h>
#include <math.h>

#include "rp_main.h"
#include "rp_video.h"
#include "rp_data.h"
#include "rp_plane.h"

#include "../m_fixed.h"
#include "../p_setup.h"
#include "../p_pspr.h"
#include "../r_main.h"
#include "../r_sky.h"
#include "../r_things.h"
#include "../tables.h"
#include "../v_video.h"
#include "../z_zone.h"

// special value for inv_z and u/z interpolations
#define PR_SEG_PART_BITS 4

// magic constanst for beautifulizing of texture mapping on slope walls and floors
#define PR_SEG_U_MIP_SCALER 2
#define PR_FLAT_MIP_SCALER 4

#define PR_FLAT_PART_BITS 14

#define RP_Z_NEAR_FIXED (8 * 65536)
#define RP_Z_NEAR_FLOAT 8.0f

#define RP_HALF_FOV_X ANG45


typedef struct screen_vertex_s
{
    fixed_t	x;
    fixed_t	y;
    fixed_t	z;
} screen_vertex_t;

typedef struct pixel_range_s
{
    short minmax[2];
} pixel_range_t;

typedef struct draw_sprite_s
{
    sprite_picture_t*		sprite;

    fixed_t			z;
    int				x_begin;
    int				x_end;
    int				y_begin;

    // texture coordinates on x_begin and x_end
    fixed_t			u_begin;
    fixed_t			v_begin;
    fixed_t			u_step;
    fixed_t			v_step;

    int				light_level;
    void			(*draw_func)();
    pixel_range_t*		pixel_range;
} draw_sprite_t;

static float		g_view_matrix[16];
static fixed_t		g_view_pos[3];
static int		g_view_angle; // angle number in sin/cos/tan tables
static fixed_t		g_half_fov_tan;

static fixed_t		g_y_scaler; // aspect ratio correction
static fixed_t		g_inv_y_scaler;

static clip_plane_t	g_clip_planes[3]; // 0 - near, 1 - left, 2 - right

static seg_t*		g_cur_seg;
static side_t*		g_cur_side;
static wall_texture_t*	g_cur_wall_texture;
static boolean		g_cur_wall_texture_transparent;
static int		g_cur_column_light; // in range [0; 65536]
static			boolean g_fullbright;

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
    // array with size SCREENWIDTH
    pixel_range_t	*x;
    int			y_min;
    int			y_max;

    int			top_vertex_index;
    int			bottom_vertex_index;
} g_cur_screen_polygon;

// array with size SCREENWIDTH
static int*		g_x_to_sky_u_table;

// array with size SCREENWIDTH
static pixel_range_t*	g_occlusion_buffer;

static struct
{
    draw_sprite_t*	sprites;
    int			capacity;
    int			count;

    draw_sprite_t**	sort_sprites[2];

    pixel_range_t*	pixel_ranges;
    int			pixel_ranges_capacity;
    int			next_pixel_range_index;
} g_draw_sprites;


extern int		skyflatnum;
extern int		skytexture;
extern spritedef_t*	sprites;
extern int		menuscale;


// input - in range [0;255]
static void SetLightLevel(int level, fixed_t z)
{
    if (g_fullbright)
    {
	g_cur_column_light = RP_GetLightingGammaTable()[255 * 7 / 8]; // fullbright, but not so full
	return;
    }

    // TODO - invent magic for cool fake contrast, like in vanila
    (void)z;
    g_cur_column_light = RP_GetLightingGammaTable()[level];
}

// uses g_cur_column_light
static pixel_t LightPixel(pixel_t p)
{
   p.components[0] = (p.components[0] * g_cur_column_light) >> 16;
   p.components[1] = (p.components[1] * g_cur_column_light) >> 16;
   p.components[2] = (p.components[2] * g_cur_column_light) >> 16;
   return p;
}

static pixel_t BlendPixels(pixel_t p0, pixel_t p1)
{
    int inv_a = 256 - p0.components[3];
    p0.components[0] = (p0.components[0] * p0.components[3] + inv_a * p1.components[0])>>8;
    p0.components[1] = (p0.components[1] * p0.components[3] + inv_a * p1.components[1])>>8;
    p0.components[2] = (p0.components[2] * p0.components[3] + inv_a * p1.components[2])>>8;
    return p0;
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

    full_subsector = RP_GetFullSubsectors() + subsector_num;
    full_subsector_vertices = RP_GetFullSubsectorsVertices() + full_subsector->first_vertex;

    g_cur_subsector_data.vertex_count = full_subsector->numvertices;
    for( i = 0; i < full_subsector->numvertices; i++ )
	g_cur_subsector_data.clipped_vertices[i] = full_subsector_vertices[i];

    for( i = 0; i < 3; i++ )
    {
	g_cur_subsector_data.vertex_count =
	    RP_ClipPolygon(
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
    int		side_ind;
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
	    side_ind = direction ? 0 : 1;
	}
	else side_ind = direction ? 1 : 0;

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
	    g_cur_screen_polygon.x[y].minmax[side_ind] = FixedRoundToInt(x);
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

static void PrepareSky(player_t* player)
{
    int		x;
    fixed_t 	sign, cur_x_tan;
    fixed_t	tan_scaler;
    int		tan_num, angle_num, final_angle_num;
    int		pixel_num;
    int		sky_tex_pixels;

    sky_tex_pixels = ID_SKY_TEXTURE_REPEATS * RP_GetSkyTexture()->width;

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
	pixel_num = (final_angle_num * sky_tex_pixels / FINEANGLES) % RP_GetSkyTexture()->width;

	g_x_to_sky_u_table[x] = pixel_num;
    }
}

static void DrawSkyPolygon()
{
    int			y;
    int			x, x_begin, x_end;
    sky_texture_t*	texture;
    pixel_t*		framebuffer;
    pixel_t*		dst;
    pixel_t*		src;
    int		v;

    texture = RP_GetSkyTexture();
    framebuffer = VP_GetFramebuffer();
    src = texture->data;

    // TODO - adopt for fov and aspect ratio

    for (y = g_cur_screen_polygon.y_min; y < g_cur_screen_polygon.y_max; y++)
    {
	x_begin = g_cur_screen_polygon.x[y].minmax[0];
	if (x_begin < 0) x_begin = 0;

	x_end = g_cur_screen_polygon.x[y].minmax[1];
	if (x_end > SCREENWIDTH) x_end = SCREENWIDTH;

	dst = framebuffer + x_begin + y * SCREENWIDTH;

	v = FixedMulFloorToInt(((y<<FRACBITS) / SCREENHEIGHT) * ID_SCREENHEIGHT, g_inv_y_scaler);
	src = texture->data + (v % texture->height) * texture->width;

	for (x = x_begin; x < x_end; x++, dst++)
	    *dst = src[ g_x_to_sky_u_table[x] ];
    }
}

static void MatMul(const float* mat0, const float* mat1, float* result)
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

static void MatIdentity(float* mat)
{
    int i;

    for( i = 1; i < 15; i++ ) mat[i] = 0.0f;

    mat[ 0] = mat[ 5] =
    mat[10] = mat[15] = 1.0f;
}

static void VecMatMul( float* vec, float* mat, float* result )
{
    int i;
    for( i = 0; i < 3; i++ )
	result[i] = vec[0] * mat[i] + vec[1] * mat[i+4] + vec[2] * mat[i+8] + mat[i+12];
}

static void SetupView(player_t* player)
{
    float		translate_matrix[16];
    float		rotate_matrix[16];
    float		basis_change_matrix[16];
    float		projection_matrix[16];
    float		tmp_mat[2][16];
    int			angle_num;


    // infrared view or invulnerability
    g_fullbright = player->fixedcolormap == 1 || player->fixedcolormap == 32;

    angle_num = ((ANG90 - player->mo->angle) >> ANGLETOFINESHIFT ) & FINEMASK;

    g_view_pos[0] = player->mo->x;
    g_view_pos[1] = player->mo->y;
    g_view_pos[2] = player->viewz;
    g_view_angle = (player->mo->angle >> ANGLETOFINESHIFT ) & FINEMASK;
    g_half_fov_tan = -finetangent[RP_HALF_FOV_X >> ANGLETOFINESHIFT];

    MatIdentity(translate_matrix);
    translate_matrix[12] = - FixedToFloat(player->mo->x);
    translate_matrix[13] = - FixedToFloat(player->mo->y);
    translate_matrix[14] = - FixedToFloat(player->viewz);

    MatIdentity(rotate_matrix);
    rotate_matrix[0] =  FixedToFloat(finecosine[ angle_num ]);
    rotate_matrix[4] = -FixedToFloat(finesine  [ angle_num ]);
    rotate_matrix[1] =  FixedToFloat(finesine  [ angle_num ]);
    rotate_matrix[5] =  FixedToFloat(finecosine[ angle_num ]);

    MatIdentity(basis_change_matrix);
    basis_change_matrix[ 5] = 0.0f;
    basis_change_matrix[ 6] = 1.0f;
    basis_change_matrix[ 9] = -1.0f;
    basis_change_matrix[10] = 0.0f;

    MatIdentity(projection_matrix);
    // TODO: Why minus tangent?
    projection_matrix[0] = FixedToFloat(-finetangent[RP_HALF_FOV_X >> ANGLETOFINESHIFT]);
    projection_matrix[5] = projection_matrix[0] * (((float)SCREENWIDTH) / ((float)SCREENHEIGHT));

    g_y_scaler = (ID_CORRECT_SCREENHEIGHT << FRACBITS) / ID_SCREENHEIGHT;
    g_inv_y_scaler = (ID_SCREENHEIGHT << FRACBITS) / ID_CORRECT_SCREENHEIGHT;
    projection_matrix[5] = projection_matrix[5] * ((float)ID_CORRECT_SCREENHEIGHT)/ ((float)ID_SCREENHEIGHT);

    MatMul( translate_matrix, rotate_matrix, tmp_mat[0] );
    MatMul( tmp_mat[0], basis_change_matrix, tmp_mat[1] );
    MatMul( tmp_mat[1], projection_matrix, g_view_matrix );
}

static void BuildClipPlanes(player_t *player)
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

static void DrawWallPart(fixed_t top_tex_offset, fixed_t z_min, fixed_t z_max)
{
    float	vertex_z[4];
    fixed_t	screen_y[4];
    int		i;
    pixel_t*	framebuffer;
    int		light_level;

    fixed_t	dx;
    int		x, x_begin, x_end;
    fixed_t	ddx;
    fixed_t	top_dy, bottom_dy, top_y, bottom_y;

    fixed_t	tex_width [RP_MAX_WALL_MIPS];
    fixed_t	tex_height[RP_MAX_WALL_MIPS];
    fixed_t	mip_tc_u_scaler[RP_MAX_WALL_MIPS];
    fixed_t	mip_tc_v_scaler[RP_MAX_WALL_MIPS];

    fixed_t	vert_u[2], u_div_z[2];
    fixed_t	u_div_z_step, inv_z_step;
    fixed_t	part_step, part;

    pixel_t*	dst;
    pixel_t*	dst_end;
    pixel_t*	src;

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

    // wall is not wisible on screen
    if (screen_y[0] < 0 && screen_y[2] < 0 ) return;
    if (screen_y[1] >= (SCREENHEIGHT<<FRACBITS) && screen_y[3] >= (SCREENHEIGHT<<FRACBITS) ) return;

    framebuffer = VP_GetFramebuffer();
    // some magic. correct just a bit light level, deend on orientation
    // correction value - [0.8; 1.0]
    light_level = g_cur_side->sector->lightlevel;
    {
       fixed_t seg_cos_abs = abs(finecosine[ (g_cur_seg->angle >> ANGLETOFINESHIFT) & FINEMASK ]);
       light_level = (light_level * (seg_cos_abs + 4 * FRACUNIT)) / (FRACUNIT * 5);
    }

    dx = g_cur_seg_data.screen_x[1] - g_cur_seg_data.screen_x[0];
    if ( dx <= 0 ) return;

    x_begin = FixedRoundToInt(g_cur_seg_data.screen_x[0]);
    if (x_begin < 0 ) x_begin = 0;
    x_end   = FixedRoundToInt(g_cur_seg_data.screen_x[1]);
    if (x_end > SCREENWIDTH) x_end = SCREENWIDTH;
    x = x_begin;

    ddx = (x_begin<<FRACBITS) + FRACUNIT/2 - g_cur_seg_data.screen_x[0];

    top_dy =    FixedDiv(screen_y[3] - screen_y[1], dx);
    bottom_dy = FixedDiv(screen_y[2] - screen_y[0], dx);

    top_y =    screen_y[1] + FixedMul(ddx, top_dy   );
    bottom_y = screen_y[0] + FixedMul(ddx, bottom_dy);

    for( i = 0; i <= g_cur_wall_texture->max_mip; i++ )
    {
	tex_width [i] = (g_cur_wall_texture->width  >> i) << FRACBITS;
	tex_height[i] = (g_cur_wall_texture->height >> i) << FRACBITS;
	mip_tc_u_scaler[i] = FixedDiv(tex_width [i], tex_width [0]);
	mip_tc_v_scaler[i] = FixedDiv(tex_height[i], tex_height[0]);
    }

    vert_u[0] = PositiveMod(g_cur_side->textureoffset + g_cur_seg_data.tc_u_offset, tex_width[0]);
    vert_u[1] = vert_u[0] + g_cur_seg_data.length;
    u_div_z[0] = FixedDiv(vert_u[0], g_cur_seg_data.z[0]);
    u_div_z[1] = FixedDiv(vert_u[1], g_cur_seg_data.z[1]);

    // for calculation of u mip
    u_div_z_step = FixedDiv(u_div_z[1] - u_div_z[0], dx);
    inv_z_step = FixedDiv(g_cur_seg_data.inv_z[1] - g_cur_seg_data.inv_z[0], dx);

    // interpolate value in range [0; 1 ^ PR_SEG_PART_BITS ]
    // becouse direct interpolation of u/z and 1/z can be inaccurate
    part_step = FixedDiv(FRACUNIT << PR_SEG_PART_BITS, dx);
    part = FixedMul(part_step, ddx);

    while (x < x_end)
    {
	int	y,y_begin, y_end;
	fixed_t	ddy, v_step, v;
	fixed_t du_dx;
	int	u_mip, v_mip, mip;
	fixed_t	cur_mip_tex_heigth;
	pixel_t	pixel;

	fixed_t one_minus_part = (FRACUNIT<<PR_SEG_PART_BITS) - part;
	fixed_t cur_u_div_z = FixedMul(part, u_div_z[1]) + FixedMul(one_minus_part, u_div_z[0]);
	fixed_t inv_z = FixedDiv(part, g_cur_seg_data.z[1]) + FixedDiv(one_minus_part, g_cur_seg_data.z[0]);
	fixed_t u = FixedDiv(cur_u_div_z, inv_z);

	SetLightLevel(light_level, FixedDiv((FRACUNIT<<PR_SEG_PART_BITS), inv_z));

	du_dx = FixedDiv(u_div_z_step - FixedMul(u, inv_z_step), inv_z >> PR_SEG_PART_BITS);
	u_mip = IntLog2Floor((du_dx / PR_SEG_U_MIP_SCALER) >> FRACBITS);

	y_begin = FixedRoundToInt(top_y   );
	if (y_begin < 0 ) y_begin = 0;
	y_end   = FixedRoundToInt(bottom_y);
	if (y_end > SCREENHEIGHT) y_end = SCREENHEIGHT;
	if (y_end <= y_begin) goto x_loop_end; // can be, in some cases

	y = y_begin;

	ddy = (y_begin<<FRACBITS) + FRACUNIT/2 - top_y;

	dst = framebuffer + x + y * SCREENWIDTH;
	dst_end = framebuffer + x + y_end * SCREENWIDTH;

	v_step = FixedDiv(z_max - z_min, bottom_y - top_y);
	v = top_tex_offset + g_cur_side->rowoffset + FixedMul(ddy, v_step);

	v_mip = IntLog2Floor(v_step >> FRACBITS);

	mip = u_mip > v_mip ? u_mip : v_mip;
	if (mip > g_cur_wall_texture->max_mip) mip = g_cur_wall_texture->max_mip;

	u = FixedMul(mip_tc_u_scaler[mip], u);
	v = FixedMul(mip_tc_v_scaler[mip], v);
	v_step = FixedMul(v_step, mip_tc_v_scaler[mip]);
	if( u >= tex_width [mip]) u %= tex_width [mip];
	cur_mip_tex_heigth = tex_height[mip];

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

static void DrawSplitWallPart(fixed_t top_tex_offset, fixed_t z_min, fixed_t z_max, boolean draw_as_sky)
{
    /*
    Because we draw walls using column-based algorithm,
    each pixel in column hits new row of framebuffer.
    This is wery bad on modern CPUs, because for each recording of pixel,
    cpu read to cache hundreds and thousands bytes to CPU cache.
    If we split walls, which hits a lot of framebuffer rows, perfomance will increase.
    */
    const int c_framebuffer_pixels_traversed = 128 * 1024;
    const int c_min_allowed_dy = 64; // do not split to low on very wide screens

    float	vertex_z[4];
    fixed_t	screen_y[4];
    int		i;
    int		dy_left, dy_right, dy_max;
    int		allowed_dy;
    int		splits;
    fixed_t	z_step;

    allowed_dy = c_framebuffer_pixels_traversed / SCREENWIDTH;
    if (allowed_dy < c_min_allowed_dy) allowed_dy = c_min_allowed_dy;

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
	DrawSkyPolygon();
	return;
    }

    dy_left  = (screen_y[0] - screen_y[1]) >> FRACBITS;
    dy_right = (screen_y[2] - screen_y[3]) >> FRACBITS;
    dy_max = dy_left > dy_right ? dy_left : dy_right;

    if (dy_max <= allowed_dy)
	DrawWallPart(top_tex_offset, z_min, z_max);
    else
    {
	splits = dy_max / allowed_dy;
	if (splits * allowed_dy < dy_max) splits++;

	z_step = (z_max - z_min) / splits;
	for( i = 0; i < splits; i++ )
	{
	    DrawWallPart(
		top_tex_offset + (splits - i - 1) * z_step,
		z_min + i * z_step,
		(i == splits - 1) ? z_max : (z_min + (i+1) * z_step));
	}
    }
}

static void DrawWall()
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

	    g_cur_wall_texture = RP_GetWallTexture(texturetranslation[g_cur_side->bottomtexture]);
	    g_cur_wall_texture_transparent = false;

	    if (g_cur_seg->linedef->flags & ML_DONTPEGBOTTOM)
		v_offset =
		    PositiveMod(
			g_cur_seg->frontsector->ceilingheight - g_cur_seg->backsector->floorheight,
			g_cur_wall_texture->height << FRACBITS);
	    else v_offset = 0;

	    DrawSplitWallPart(
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

	    g_cur_wall_texture = RP_GetWallTexture(texturetranslation[g_cur_side->toptexture]);
	    g_cur_wall_texture_transparent = false;

	    if (g_cur_seg->linedef->flags & ML_DONTPEGTOP)
		v_offset = 0;
	    else
		v_offset =
		    PositiveMod(
			g_cur_seg->backsector->ceilingheight - g_cur_seg->frontsector->ceilingheight,
			g_cur_wall_texture->height * FRACUNIT );

	    DrawSplitWallPart(
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

	    g_cur_wall_texture = RP_GetWallTexture(texturetranslation[g_cur_side->midtexture]);
	    g_cur_wall_texture_transparent = true;

	    if (g_cur_seg->linedef->flags & ML_DONTPEGBOTTOM)
	    {
		h = g_cur_seg->frontsector->floorheight > g_cur_seg->backsector->floorheight
		    ? g_cur_seg->frontsector->floorheight
		    : g_cur_seg->backsector->floorheight;

		DrawSplitWallPart(
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

		DrawSplitWallPart(
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

	g_cur_wall_texture = RP_GetWallTexture(texturetranslation[g_cur_side->midtexture]);
	g_cur_wall_texture_transparent = false;

	if (g_cur_seg->linedef->flags & ML_DONTPEGBOTTOM)
	    v_offset =
		PositiveMod(
		    g_cur_seg->frontsector->floorheight - g_cur_seg->frontsector->ceilingheight,
		    g_cur_wall_texture->height * FRACUNIT );
	else v_offset = 0;

	DrawSplitWallPart(
	    v_offset,
	    g_cur_seg->frontsector->floorheight,
	    g_cur_seg->frontsector->ceilingheight,
	    false);
    }
}

static void DrawSubsectorFlat(int subsector_num, boolean is_floor)
{
    int			i;
    subsector_t*	subsector;
    fixed_t		height;
    int			texture_num;
    screen_vertex_t	vertices_proj[RP_MAX_SUBSECTOR_VERTICES + 3];
    flat_texture_t*	texture;

    screen_vertex_t*	top_vertex;
    screen_vertex_t*	bottom_vertex;

    fixed_t		dy, ddy, part_step, part;
    fixed_t		inv_z_scaled_step;
    fixed_t		uv_start[2], uv_dir[2], uv_per_dir[2];
    fixed_t		uv_line_step_on_z1;
    int			y;

    subsector = &subsectors[subsector_num];

    height = is_floor ? subsector->sector->floorheight : subsector->sector->ceilingheight;
    texture_num = flattranslation[is_floor ? subsector->sector->floorpic : subsector->sector->ceilingpic];

    for( i = 0; i < g_cur_subsector_data.vertex_count; i++)
    {
	float f_vertex[3], proj[3];

	f_vertex[0] = FixedToFloat(g_cur_subsector_data.clipped_vertices[i].x);
	f_vertex[1] = FixedToFloat(g_cur_subsector_data.clipped_vertices[i].y);
	f_vertex[2] = FixedToFloat(height);

	VecMatMul(f_vertex, g_view_matrix, proj);

	proj[0] /= proj[2];
	proj[1] /= proj[2];

	vertices_proj[i].x = FloatToFixed((proj[0] + 1.0f ) * ((float)SCREENWIDTH ) * 0.5f );
	vertices_proj[i].y = FloatToFixed((proj[1] + 1.0f ) * ((float)SCREENHEIGHT) * 0.5f );
	vertices_proj[i].z = FloatToFixed(proj[2]);
    }

    PreparePolygon(vertices_proj, g_cur_subsector_data.vertex_count, is_floor);

    if (texture_num == skyflatnum)
    {
	DrawSkyPolygon();
	return;
    }

    top_vertex    = &vertices_proj[g_cur_screen_polygon.top_vertex_index   ];
    bottom_vertex = &vertices_proj[g_cur_screen_polygon.bottom_vertex_index];

    texture = RP_GetFlatTexture(texture_num);

    dy = bottom_vertex->y - top_vertex->y;
    part_step = FixedDiv(FRACUNIT << PR_FLAT_PART_BITS, dy);
    ddy = (g_cur_screen_polygon.y_min<<FRACBITS) + FRACUNIT/2 - top_vertex->y;
    part = FixedMul(ddy, part_step);

    inv_z_scaled_step = FixedDiv( // value is negative
        FixedDiv(FRACUNIT << PR_FLAT_PART_BITS,    top_vertex->z) -
        FixedDiv(FRACUNIT << PR_FLAT_PART_BITS, bottom_vertex->z), dy );
    inv_z_scaled_step = abs(inv_z_scaled_step);

    uv_start[0] = g_view_pos[0];
    uv_start[1] = -g_view_pos[1];
    uv_dir[0] =  finecosine[g_view_angle];
    uv_dir[1] =  -finesine [g_view_angle];
    uv_per_dir[0] = -uv_dir[1];
    uv_per_dir[1] =  uv_dir[0];

    // TODO - optimize this
    uv_line_step_on_z1 = FixedDiv((2 << FRACBITS) / SCREENWIDTH, g_half_fov_tan);
    for( y = g_cur_screen_polygon.y_min; y < g_cur_screen_polygon.y_max; y++, part+= part_step )
    {
	int	x, x_begin, x_end;
	int	y_mip, x_mip, mip;
	int	texel_fetch_shift, texel_fetch_mask;
	fixed_t	line_duv_scaler, u, v, du_dx, dv_dx, center_offset;
	pixel_t	*src, *dst, pixel;

	fixed_t inv_z_scaled = // (1<<PR_FLAT_PART_BITS) / z
	    FixedDiv(part, bottom_vertex->z) +
	    FixedDiv((FRACUNIT<<PR_FLAT_PART_BITS) - part, top_vertex->z);
	fixed_t z = FixedDiv(FRACUNIT<<PR_FLAT_PART_BITS, inv_z_scaled);

	SetLightLevel(subsector->sector->lightlevel, z);

	y_mip =
	    IntLog2Floor((FixedMul(FixedDiv(inv_z_scaled_step, inv_z_scaled), z) / PR_FLAT_MIP_SCALER) >> FRACBITS);

	x_begin = g_cur_screen_polygon.x[y].minmax[0];
	if (x_begin < 0 ) x_begin = 0;
	x_end = g_cur_screen_polygon.x[y].minmax[1];
	if( x_end > SCREENWIDTH) x_end = SCREENWIDTH;
	dst = VP_GetFramebuffer() + x_begin + y * SCREENWIDTH;

	line_duv_scaler = FixedMul(uv_line_step_on_z1, z);
	u = FixedMul(z, uv_dir[0]) + uv_start[0];
	v = FixedMul(z, uv_dir[1]) + uv_start[1];
	du_dx = FixedMul(uv_per_dir[0], line_duv_scaler);
	dv_dx = FixedMul(uv_per_dir[1], line_duv_scaler);

	center_offset = (x_begin<<FRACBITS) - (SCREENWIDTH<<FRACBITS)/2 + FRACUNIT/2;
	u += FixedMul(center_offset, du_dx);
	v += FixedMul(center_offset, dv_dx);

	// mip = log(sqrt(du *du + dv * dv)) = log(du *du + dv * dv) / 2
	// add small shift for prevention of overflow
	x_mip = IntLog2Floor((
	    FixedMul(du_dx>>2, du_dx>>2) +
	    FixedMul(dv_dx>>2, dv_dx>>2) ) >> (FRACBITS-4) ) >> 1;

	mip = y_mip > x_mip ? y_mip : x_mip;
	if (mip > RP_FLAT_TEXTURE_SIZE_LOG2) mip = RP_FLAT_TEXTURE_SIZE_LOG2;
	texel_fetch_shift = RP_FLAT_TEXTURE_SIZE_LOG2 - mip;
	texel_fetch_mask = (1<<texel_fetch_shift) - 1;
	u>>=mip; v>>=mip;
	du_dx>>=mip; dv_dx>>=mip;

	src = texture->mip[mip];
	for( x = x_begin; x < x_end; x++, dst++, u += du_dx, v += dv_dx )
	{
	    pixel = src[ ((u>>FRACBITS)&texel_fetch_mask) + (((v>>FRACBITS)&texel_fetch_mask) << texel_fetch_shift) ];
	    *dst = LightPixel(pixel);
	}
    }
}


// TODO - remove this funcs to separate file. Or all code for sprites to separate file.
static fixed_t		g_spr_u;
static fixed_t		g_spr_u_end;
static fixed_t		g_spr_u_step;
static pixel_t*		g_spr_dst;
static pixel_t*		g_spr_src;
static int		g_spr_mip_width_minus_one;

static void SpriteRowFunc()
{
    for(; g_spr_u < g_spr_u_end; g_spr_u++, g_spr_u += g_spr_u_step, g_spr_dst++ )
	*g_spr_dst = BlendPixels( LightPixel( g_spr_src[g_spr_u>>FRACBITS] ), *g_spr_dst );
}

static void SpriteRowFuncFlip()
{
    for(; g_spr_u < g_spr_u_end; g_spr_u++, g_spr_u += g_spr_u_step, g_spr_dst++ )
	*g_spr_dst = BlendPixels( LightPixel( g_spr_src[ g_spr_mip_width_minus_one - (g_spr_u>>FRACBITS) ] ), *g_spr_dst );
}

static void SpriteRowFuncSpectre()
{
    // avg func:   pixel.p = ( ((pixel.p ^ dst->p) & 0xFEFEFEFE) >> 1 ) + (pixel.p & dst->p);
    // half brightness:   dst->p = (dst->p & 0xFEFEFEFE) >> 1
    for(; g_spr_u < g_spr_u_end; g_spr_u++, g_spr_u += g_spr_u_step, g_spr_dst++ )
	if( g_spr_src[ (g_spr_u >> FRACBITS) ].components[3] >= 128 )
	    g_spr_dst->p = (g_spr_dst->p & 0xFEFEFEFE) >> 1;
}

static void SpriteRowFuncSpectreFlip()
{
    for(; g_spr_u < g_spr_u_end; g_spr_u++, g_spr_u += g_spr_u_step, g_spr_dst++ )
	if( g_spr_src[ g_spr_mip_width_minus_one - (g_spr_u >> FRACBITS) ].components[3] >= 128 )
	    g_spr_dst->p = (g_spr_dst->p & 0xFEFEFEFE) >> 1;
}

static void DrawSprite(draw_sprite_t* dsprite)
{
    sprite_picture_t*	sprite;
    fixed_t		v, u_end, v_end;
    fixed_t		sprite_width, sprite_height;
    int			cur_mip_width;
    int			y, mip;
    void		(*spr_func)();
    pixel_t*		fb;

    sprite = dsprite->sprite;

    SetLightLevel(dsprite->light_level, dsprite->z);
    spr_func = dsprite->draw_func;

    sprite_width  = sprite->width  << FRACBITS;
    sprite_height = sprite->height << FRACBITS;

    mip = IntLog2Floor(dsprite->v_step >> FRACBITS);
    if (mip > sprite->max_mip) mip = sprite->max_mip;

    u_end = dsprite->u_begin + (SCREENWIDTH  - dsprite->x_begin) * dsprite->u_step;
    if (u_end > sprite_width ) u_end = sprite_width;
    v_end = dsprite->v_begin + (SCREENHEIGHT - dsprite->y_begin) * dsprite->v_step;
    if (v_end > sprite_height) v_end = sprite_height;

    dsprite->u_begin >>= mip;
    dsprite->v_begin >>= mip;
    dsprite->u_step >>= mip;
    dsprite->v_step >>= mip;
    u_end  >>= mip;
    v_end  >>= mip;

    fb = VP_GetFramebuffer() + dsprite->x_begin;
    cur_mip_width = sprite->width >> mip;

    g_spr_u_step = dsprite->u_step;
    g_spr_u_end = u_end;
    g_spr_mip_width_minus_one = cur_mip_width - 1;

    for( y = dsprite->y_begin, v = dsprite->v_begin; v < v_end; y++, v += dsprite->v_step)
    {
	g_spr_u = dsprite->u_begin;
	g_spr_dst = fb + y * SCREENWIDTH;
	g_spr_src = sprite->mip[mip] + (v>>FRACBITS) * cur_mip_width;
	spr_func();
    }
}

// ascending sorting - near to far
// TODO - optimize this
static void SortSprites_r(draw_sprite_t** in, draw_sprite_t** tmp, int count)
{
    int			half;
    int			p[2], p_end[2], out_p;

    if (count <= 1 )
	return;
    if (count == 2)
    {
	if (in[0]->z > in[1]->z)
	{
	    draw_sprite_t* tmp = in[0];
	    in[0] = in[1];
	    in[1] = tmp;
	}
	return;
    }

    half = count >> 1;

    SortSprites_r(in, tmp, half);
    SortSprites_r(in + half, tmp + half, count - half);

    p[0] = 0;
    p[1]= half;
    p_end[0] = half;
    p_end[1] = count;
    out_p = 0;
    while (out_p < count)
    {
	if (p[0] == p_end[0])
	{
	    tmp[out_p] = in[p[1]]; p[1]++;
	}
	else if (p[1] == p_end[1])
	{
	    tmp[out_p] = in[p[0]]; p[0]++;
	}
	else
	{
	    if (in[p[0]]->z < in[p[1]]->z)
	    {
		tmp[out_p] = in[p[0]]; p[0]++;
	    }
	    else
	    {
		tmp[out_p] = in[p[1]]; p[1]++;
	    }
	}
	out_p++;
    }

    memcpy(in, tmp, count * sizeof(draw_sprite_t*));
}

static void DrawSprites()
{
    int i;

    for( i = 0; i < g_draw_sprites.count; i++ )
	g_draw_sprites.sort_sprites[0][i] = &g_draw_sprites.sprites[i];

    SortSprites_r( g_draw_sprites.sort_sprites[0], g_draw_sprites.sort_sprites[1], g_draw_sprites.count);

    for( i = g_draw_sprites.count - 1; i >= 0; i--)
	DrawSprite(g_draw_sprites.sort_sprites[0][i]);
}

static void AddSubsectorSprites(subsector_t* sub)
{
    mobj_t*		mob;
    spriteframe_t*	frame;
    sprite_picture_t*	sprite;
    int			angle_num;
    draw_sprite_t*	dsprite;
    fixed_t		u_step_on_z1;
    pixel_t*		fb = VP_GetFramebuffer();

    u_step_on_z1 = FixedDiv((2 << FRACBITS) / SCREENWIDTH, g_half_fov_tan);

    mob = sub->sector->thinglist;

    while(mob)
    {
	float	pos[3];
	float	proj[3];
	fixed_t	z, sx, sy;
	fixed_t	x_begin_f, y_begin_f;

	if (g_draw_sprites.count == g_draw_sprites.capacity) // no space for srites anymore
	    return;

	if(mob->subsector != sub) goto next_mob;

	frame = &sprites[mob->sprite].spriteframes[mob->frame & FF_FRAMEMASK];
	{ // TODO - make it easier
	    fixed_t	dir_to_mob[2], mob_dir[2];
	    fixed_t	dot, cross;
	    float	angle;
	    const	float pi = 3.1515926535f;

	    dir_to_mob[0] = mob->x - g_view_pos[0];
	    dir_to_mob[1] = mob->y - g_view_pos[1];

	    angle_num = (mob->angle >> ANGLETOFINESHIFT) & FINEMASK;
	    mob_dir[0] = finecosine[angle_num];
	    mob_dir[1] = finesine  [angle_num];

	    dot   = FixedMul(dir_to_mob[0], mob_dir[0]) + FixedMul(dir_to_mob[1], mob_dir[1]);
	    cross = FixedMul(dir_to_mob[1], mob_dir[0]) - FixedMul(dir_to_mob[0], mob_dir[1]);
	    angle = atan2(FixedToFloat(cross), FixedToFloat(dot)) + pi*3.0f;
	    angle_num = ((int)((8.0f*(angle + pi/8.0f)) / (2.0f * pi))) & 7;
	    sprite = RP_GetSpritePicture(frame->lump[angle_num]);
	}

	pos[0] = FixedToFloat(mob->x);
	pos[1] = FixedToFloat(mob->y);
	pos[2] = FixedToFloat(mob->z + FRACUNIT * sprite->height);
	VecMatMul(pos, g_view_matrix, proj);

	if (proj[2] < RP_Z_NEAR_FLOAT) goto next_mob;
	proj[0] /= proj[2];
	proj[1] /= proj[2];

	z = FloatToFixed(proj[2]);
	sx = FloatToFixed((proj[0] + 1.0f ) * ((float) SCREENWIDTH ) * 0.5f );
	sy = FloatToFixed((proj[1] + 1.0f ) * ((float) SCREENHEIGHT) * 0.5f );

	dsprite = g_draw_sprites.sprites + g_draw_sprites.count;
	g_draw_sprites.count++;

	dsprite->u_step = FixedMul(u_step_on_z1, z);
	dsprite->v_step = FixedMul(dsprite->u_step, g_inv_y_scaler);

	x_begin_f = sx - FixedDiv((sprite->width<<FRACBITS)/2, dsprite->u_step);
	y_begin_f = sy;
	dsprite->x_begin = FixedRoundToInt(x_begin_f);
	dsprite->y_begin = FixedRoundToInt(y_begin_f);

	if (dsprite->x_begin < 0)
	{
	    dsprite->u_begin += - dsprite->x_begin * dsprite->u_step;
	    dsprite->x_begin = 0;
	}
	if (dsprite->y_begin < 0)
	{
	    dsprite->v_begin += - dsprite->y_begin * dsprite->v_step;
	    dsprite->y_begin = 0;
	}

	dsprite->u_begin = FixedMul((dsprite->x_begin<<FRACBITS) - x_begin_f + FRACUNIT/2, dsprite->u_step);
	dsprite->v_begin = FixedMul((dsprite->y_begin<<FRACBITS) - y_begin_f + FRACUNIT/2, dsprite->v_step);

	if (mob->flags & MF_SHADOW)
	    dsprite->draw_func = frame->flip[angle_num] ? SpriteRowFuncSpectreFlip : SpriteRowFuncSpectre;
	else
	    dsprite->draw_func = frame->flip[angle_num] ? SpriteRowFuncFlip : SpriteRowFunc;

	dsprite->sprite = sprite;
	dsprite->z = z;
	dsprite->x_end = 0;
	dsprite->light_level = (mob->frame & FF_FULLBRIGHT) ? 255 : sub->sector->lightlevel;

	next_mob:
	mob = mob->snext;
    }
}

static void Subsector(int num)
{
    subsector_t*	sub;
    int			line_seg;
    boolean		subsector_clipped;

    sub = &subsectors[num];

    for( line_seg = sub->firstline; line_seg < sub->numlines + sub->firstline; line_seg++ )
    {
    	g_cur_seg = &segs[ line_seg ];
	DrawWall();
    }
    subsector_clipped = false;

    if (g_view_pos[2] > sub->sector->floorheight  )
    {
	ClipCurSubsector(num);
	subsector_clipped = true;
	if (g_cur_subsector_data.vertex_count > 0)
	    DrawSubsectorFlat( num, true  );
    }
    if (g_view_pos[2] < sub->sector->ceilingheight)
    {
	if (!subsector_clipped) ClipCurSubsector(num);
	if (g_cur_subsector_data.vertex_count > 0)
	    DrawSubsectorFlat( num, false );
    }

    AddSubsectorSprites(sub);
}
//
// RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
static void RenderBSPNode(int bspnum, boolean for_sprites)
{
    node_t*	bsp;
    int		side;
    int		subsector_num;

    // Found a subsector?
    if (bspnum & NF_SUBSECTOR)
    {
	subsector_num = bspnum == -1 ? 0 : bspnum&(~NF_SUBSECTOR);
	
	if (for_sprites)
	    AddSubsectorSprites(&subsectors[subsector_num]);
	else
	    Subsector(subsector_num);
	return;
    }

    bsp = &nodes[bspnum];

    // Decide which side the view point is on.
    side = 1 ^ R_PointOnSide (g_view_pos[0], g_view_pos[1], bsp);
    if (for_sprites) side ^= 1;

    RenderBSPNode (bsp->children[side], for_sprites);
    RenderBSPNode (bsp->children[side^1], for_sprites);
}

static void DrawPlayerSprites(player_t *player)
{
    int			i;
    pspdef_t*		psprite;
    state_t*		state;
    sprite_picture_t*	sprite;
    fixed_t		scaler, dx;
    fixed_t		x_begin_f, y_begin_f;
    draw_sprite_t	dsprite;

    scaler = FRACUNIT * SCREENHEIGHT / ID_SCREENHEIGHT;
    dx = SCREENWIDTH * FRACUNIT - FixedMul(ID_SCREENWIDTH *scaler, g_inv_y_scaler);
    dsprite.u_step = FixedDiv(g_y_scaler, scaler);
    dsprite.v_step = FixedDiv(FRACUNIT, scaler);

    for (i = 0; i < NUMPSPRITES; i++)
    {
	psprite = &player->psprites[i];
	state = psprite->state;
	if (!state) continue;

	sprite = RP_GetSpritePicture( sprites[state->sprite].spriteframes[state->frame & FF_FRAMEMASK].lump[0] );

	x_begin_f = psprite->sx - (sprite->left_offset<<FRACBITS);
	x_begin_f = FixedMul( FixedMul(x_begin_f, scaler), g_inv_y_scaler ) + dx / 2;

	y_begin_f = psprite->sy - (sprite->top_offset <<FRACBITS);
	y_begin_f = FixedMul(y_begin_f, scaler);
	y_begin_f -= 32 * menuscale * FRACUNIT / 2; // TODO - remove this magic

	dsprite.x_begin = FixedRoundToInt(x_begin_f);
	if (dsprite.x_begin < 0 ) dsprite.x_begin = 0;
	dsprite.y_begin = FixedRoundToInt(y_begin_f);

	dsprite.u_begin = x_begin_f - (dsprite.x_begin<<FRACBITS) + FRACUNIT/2;
	dsprite.v_begin = y_begin_f - (dsprite.y_begin<<FRACBITS) + FRACUNIT/2;

	dsprite.x_end = 0;
	dsprite.z = FRACUNIT;
	dsprite.light_level = 255 * 15/16;
	dsprite.sprite = sprite;
	dsprite.draw_func = (player->mo->flags&MF_SHADOW) ? SpriteRowFuncSpectre : SpriteRowFunc;
	DrawSprite(&dsprite);
    }
}

static void Postprocess(int colormap_num)
{
    pixel_t	pixel;
    pixel_t*	fb = VP_GetFramebuffer();
    pixel_t*	fb_end = fb + SCREENWIDTH * SCREENHEIGHT;
    fixed_t	one_third = FRACUNIT / 3 + 1;

    if (colormap_num == 0 || colormap_num == 1) // no colormap or fullbright
        return;
    else if (colormap_num == 32) // invulnerability
    {
	for( ; fb < fb_end; fb++ )
	{
	    pixel = *fb;
	    pixel.components[0] = pixel.components[1] = pixel.components[2] =
		((pixel.components[0] + pixel.components[1] + pixel.components[2]) * one_third) >> FRACBITS;
	    fb->p = ~pixel.p;
	}
    }
    // UNUSED. TODO - invent how to draw this
    /*
    else
    {
	pixel_t	blend_color;
	int	color_premultiplied[4];
	int	i;

	if (colormap_num <= 8) // red - blood
	{
	    blend_color.components[0] = 0;
	    blend_color.components[1] = 0;
	    blend_color.components[2] = 255;
	    blend_color.components[3] = (colormap_num - 1 + 1) * 255 * 11 / 100;
	}
	else if (colormap_num <= 12) // yellow - pickup
	{
	    blend_color.components[0] = 0;
	    blend_color.components[1] = 255;
	    blend_color.components[2] = 255;
	    blend_color.components[3] = (colormap_num - 8 + 1) * 255 * 125 / 1000;
	}
	else // green - hazard suit
	{
	    blend_color.components[0] = 0;
	    blend_color.components[1] = 255;
	    blend_color.components[2] = 0;
	    blend_color.components[3] = 255 * 125 / 1000;
	}
	blend_color.components[3] = 255 - blend_color.components[3];
	for(i = 0; i < 3; i++)
	    color_premultiplied[i] = blend_color.components[i] * (255-blend_color.components[3]);

	    for( ; fb < fb_end; fb++ )
	    {
		pixel = *fb;
		pixel.components[0] = (color_premultiplied[0] + pixel.components[0] * blend_color.components[3]) >> 8;
		pixel.components[1] = (color_premultiplied[1] + pixel.components[1] * blend_color.components[3]) >> 8;
		pixel.components[2] = (color_premultiplied[2] + pixel.components[2] * blend_color.components[3]) >> 8;
		*fb = pixel;
	    }
    }
    */
}

static void R_32b_RenderPlayerView(player_t* player)
{
   /* int y, x;
    pixel_t conrast_colors[2];
    pixel_t* framebuffer = VP_GetFramebuffer();

    conrast_colors[0] = VP_GetPaletteStorage()[2];
    conrast_colors[1] = VP_GetPaletteStorage()[34];

    for( y = 0; y < SCREENHEIGHT; y++ )
	for( x = 0; x < SCREENWIDTH; x++, framebuffer++ )
	    *framebuffer = conrast_colors[ ((x>>1) ^ (y>>1)) & 1 ];*/

    g_draw_sprites.count = 0;
    {
	int i;
	for( i = 0; i < SCREENWIDTH; i++ )
	{
	    g_occlusion_buffer[i].minmax[0] = 0;
	    g_occlusion_buffer[i].minmax[1] = SCREENHEIGHT;
	}
    }

    SetupView(player);
    BuildClipPlanes(player);
    PrepareSky(player);

    RenderBSPNode(numnodes-1, false);
    RenderBSPNode(numnodes-1, true);

    DrawSprites();
    DrawPlayerSprites(player);

    Postprocess(player->fixedcolormap);
}

// PANZER - STUBS
void R_32b_SetViewSize(int blocks,int detail){}
void R_32b_InitSprites (char** namelist)
{
    extern void R_8b_InitSprites(char** namelist);
    R_8b_InitSprites(namelist);
}

//TODO - implement this
void R_32b_ClearSprites(void){}

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

static void InitStaticData()
{
    g_cur_screen_polygon.x = Z_Malloc(SCREENWIDTH * sizeof(pixel_range_t), PU_STATIC, NULL);
    g_x_to_sky_u_table = Z_Malloc(SCREENWIDTH * sizeof(int), PU_STATIC, NULL);
    g_occlusion_buffer = Z_Malloc(SCREENWIDTH * sizeof(pixel_range_t), PU_STATIC, NULL);

    g_draw_sprites.capacity = 512;
    g_draw_sprites.sprites = Z_Malloc(g_draw_sprites.capacity * sizeof(draw_sprite_t), PU_STATIC, NULL);

    g_draw_sprites.sort_sprites[0] = Z_Malloc(g_draw_sprites.capacity * sizeof(draw_sprite_t*), PU_STATIC, NULL);
    g_draw_sprites.sort_sprites[1] = Z_Malloc(g_draw_sprites.capacity * sizeof(draw_sprite_t*), PU_STATIC, NULL);
}

void RP_Init()
{
    void R_32b_InitData(void);

    R_32b_InitInterface();
    R_32b_InitData();

    InitStaticData();
}
