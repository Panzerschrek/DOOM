#include "rp_main.h"
#include "rp_video.h"

#include "../m_fixed.h"
#include "../p_setup.h"
#include "../r_main.h"
#include "../tables.h"
#include "../v_video.h"

static float view_matrix[16];

static float FixedToFloat(fixed_t f)
{
    return ((float)f) / ((float)FRACUNIT);
}

static fixed_t FloatToFixed(float f)
{
    return (fixed_t)(f * ((float)FRACUNIT));
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
    const int half_fov = ANG45;
    fixed_t aspect = (SCREENWIDTH << FRACBITS) / SCREENHEIGHT;
    //projection_matrix[5] = FixedToFloat(FixedDiv( FRACUNIT, finetangent[half_fov >> ANGLETOFINESHIFT] ));
    //projection_matrix[0] = FixedToFloat(FixedDiv( projection_matrix[5], aspect ));
    projection_matrix[5] = 1.0f / tan(3.1415926535f * 0.25f );
    projection_matrix[0] = projection_matrix[5] / (((float)SCREENWIDTH) / ((float)SCREENHEIGHT));

    RP_MatMul( translate_matrix, rotate_matrix, tmp_mat[0] );
    RP_MatMul( tmp_mat[0], basis_change_matrix, tmp_mat[1] );
    RP_MatMul( tmp_mat[1], projection_matrix, view_matrix );
}

void PR_DrawWallPart(seg_t* seg, fixed_t z_min, fixed_t z_max)
{
    float	vertices[4][3];
    float	vertices_proj[4][3];
    int		screen_pos[4][2];
    int		i;

    vertices[0][0] = FixedToFloat(seg->v1->x);
    vertices[0][1] = FixedToFloat(seg->v1->y);
    vertices[0][2] = FixedToFloat(z_min);
    vertices[1][0] = FixedToFloat(seg->v1->x);
    vertices[1][1] = FixedToFloat(seg->v1->y);
    vertices[1][2] = FixedToFloat(z_max);

    vertices[2][0] = FixedToFloat(seg->v2->x);
    vertices[2][1] = FixedToFloat(seg->v2->y);
    vertices[2][2] = FixedToFloat(z_min);
    vertices[3][0] = FixedToFloat(seg->v2->x);
    vertices[3][1] = FixedToFloat(seg->v2->y);
    vertices[3][2] = FixedToFloat(z_max);

    for( i = 0; i < 4; i++ )
    	RP_VecMatMul( vertices[i], view_matrix, vertices_proj[i] );

   // back side
   //if (vertices_proj[0][2] <= 0.0f ||  vertices_proj[2][2] <= 0.0f ) return;

    int color_index = ((int)seg) & 255;
    for( i = 0; i < 4; i++ )
    {
   	if (vertices_proj[i][2] <= 0.0f ) return;

    	vertices_proj[i][0] /= vertices_proj[i][2];
    	vertices_proj[i][1] /= vertices_proj[i][2];

    	screen_pos[i][0] = (int)((vertices_proj[i][0] + 1.0f ) * ((float) SCREENWIDTH) * 0.5f );
    	screen_pos[i][1] = (int)((vertices_proj[i][1] + 1.0f ) * ((float)SCREENHEIGHT) * 0.5f );
    	if (screen_pos[i][0] < 0 || screen_pos[i][0] >=  SCREENWIDTH) return;
    	if (screen_pos[i][1] < 0 || screen_pos[i][1] >= SCREENHEIGHT) return;

    	//V_DrawPixel( screen_pos[i][0], screen_pos[i][1], /*112*/color_index );
    }

    pixel_t* framebuffer = VP_GetFramebuffer();
    pixel_t* palette = VP_GetPaletteStorage();

    int dx = screen_pos[2][0] - screen_pos[0][0];

    int right_side, left_side;
    if ( dx < 0 )
    {
    	dx = -dx;

    	left_side = 2;
	right_side = 0;
    }
    else if ( dx > 0 )
    {
	left_side = 0;
	right_side = 2;
    }
    else return;

    fixed_t top_dy =    ((screen_pos[right_side+1][1] - screen_pos[left_side+1][1]) << FRACBITS) / dx;
    fixed_t bottom_dy = ((screen_pos[right_side  ][1] - screen_pos[left_side  ][1]) << FRACBITS) / dx;

    fixed_t top_y =    screen_pos[left_side+1][1] << FRACBITS;
    fixed_t bottom_y = screen_pos[left_side  ][1] << FRACBITS;

    int x = screen_pos[left_side][0];

    while( x < screen_pos[right_side][0])
    {
    	int y = top_y >> FRACBITS;
    	int y_end = bottom_y >> FRACBITS;
    	while( y < y_end)
    	{
    		framebuffer[ x + y * SCREENWIDTH ] = palette[ color_index ];
    		y++;
    	}

    	top_y   +=  top_dy;
    	bottom_y += bottom_dy;
    	x++;
    }
}

void PR_DrawWall(seg_t* seg)
{
    sector_t*	sector;

    if (seg->frontsector && seg->backsector)
    {
    	if (seg->backsector->floorheight > seg->frontsector->floorheight )
		PR_DrawWallPart( seg, seg->frontsector->floorheight, seg->backsector->floorheight );
	else if (seg->backsector->floorheight < seg->frontsector->floorheight )
		PR_DrawWallPart( seg, seg->backsector->floorheight, seg->frontsector->floorheight );

	if (seg->backsector->ceilingheight > seg->frontsector->ceilingheight )
		PR_DrawWallPart( seg, seg->frontsector->ceilingheight, seg->backsector->ceilingheight );
	else if (seg->backsector->ceilingheight < seg->frontsector->ceilingheight )
		PR_DrawWallPart( seg, seg->backsector->ceilingheight, seg->frontsector->ceilingheight );
    }
    else
    {
    	sector = seg->frontsector ? seg->frontsector : seg->backsector;
    	PR_DrawWallPart( seg, sector->floorheight, sector->ceilingheight );
    }
}

void RP_Subsector(int num)
{
    subsector_t*	sub;
    seg_t*		seg;
    sector_t*		sector;
    int			line_seg;

    sub = &subsectors[num];

    for( line_seg = sub->firstline; line_seg < sub->numlines + sub->firstline; line_seg++ )
    {
    	seg = &segs[ line_seg ];
	PR_DrawWall(seg);
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
    side = 1 ^ R_PointOnSide (viewx, viewy, bsp);

    // Recursively divide front space.
    RP_RenderBSPNode (bsp->children[side]);

    // Possibly divide back space.
    //if (R_CheckBBox (bsp->bbox[side^1]))
    RP_RenderBSPNode (bsp->children[side^1]);
}

void RP_RenderPlayerView (player_t *player)
{
    //V_FillRect( SCREENWIDTH / 2 - 2, SCREENHEIGHT / 2 - 2, 4, 4, 32 );

    V_FillRect( 0, 0, SCREENWIDTH, SCREENHEIGHT, 0 );

    RP_BuildViewMatrix(player);
    RP_RenderBSPNode(numnodes-1);
}
