#include "rp_defs.h"
#include "../m_bbox.h"
#include "../m_fixed.h"

#include <math.h>
#include <stdlib.h>

#define RP_MAX_SUBSECTOR_VERTICES 64
#define RP_MAX_BSP_TREE_DEPTH 128


static full_subsector_t*	g_out_subsectors = NULL;
static vertex_t*		g_out_subsectors_vertices = NULL;
static int			g_out_subsectors_vertex_count = 0;
static int			g_out_subsectors_vertices_capacity = 0;

static vertex_t			g_cur_subsector_vertices[ RP_MAX_SUBSECTOR_VERTICES ];
static int			g_cur_subsector_vertex_count;

static node_t*			g_cur_subsector_parent_nodes_stack[ RP_MAX_BSP_TREE_DEPTH ];
static clip_plane_t		g_nodes_clip_planes[ RP_MAX_BSP_TREE_DEPTH ];
static int			g_cur_subsector_parent_nodes_count;
static fixed_t			g_level_bbox_min[2];
static fixed_t			g_level_bbox_max[2];

// extern bsp strustures
extern subsector_t*	subsectors;
extern int		numsubsectors;

extern node_t*		nodes;
extern int		numnodes;

extern seg_t*		segs;
extern int		numsegs;


static void NormalizeVec(fixed_t x, fixed_t y, fixed_t* out)
{
    float xy[2]= { FixedToFloat(x), FixedToFloat(y) };

    float inv_len = 1.0f / sqrt(xy[0] * xy[0] + xy[1] * xy[1]);
    out[0] = FloatToFixed(xy[0] * inv_len);
    out[1] = FloatToFixed(xy[1] * inv_len);
}

static void PrepareOutBuffer()
{
    if (g_out_subsectors_vertices)
	free( g_out_subsectors_vertices );

    g_out_subsectors_vertices_capacity = numsubsectors * 5;
    g_out_subsectors_vertex_count = 0;

    g_out_subsectors_vertices = malloc(g_out_subsectors_vertices_capacity * sizeof(vertex_t));

    if (g_out_subsectors)
	free(g_out_subsectors);
    g_out_subsectors = malloc(numsubsectors * sizeof(full_subsector_t));
}

static void AddSubsector(int subsector_num)
{
    int 	i;

    int new_vertex_count = g_out_subsectors_vertex_count + g_cur_subsector_vertex_count;
    if (new_vertex_count > g_out_subsectors_vertices_capacity)
    {
	g_out_subsectors_vertices_capacity = g_out_subsectors_vertices_capacity * 3 / 2;
	vertex_t* new_vertices = malloc(g_out_subsectors_vertices_capacity * sizeof(vertex_t));

	memcpy(new_vertices, g_out_subsectors_vertices, g_out_subsectors_vertex_count);
	free(g_out_subsectors_vertices);
	g_out_subsectors_vertices = new_vertices;
    }

    g_out_subsectors[subsector_num].first_vertex = g_out_subsectors_vertex_count;
    g_out_subsectors[subsector_num].numvertices = g_cur_subsector_vertex_count;

    for( i = 0; i < g_cur_subsector_vertex_count; i++ )
	g_out_subsectors_vertices[ i + g_out_subsectors_vertex_count ] = g_cur_subsector_vertices[i];

    g_out_subsectors_vertex_count = new_vertex_count;
}

// returns new vertex count
int R_32b_ClipPolygon(vertex_t* vertices, int vertex_count, clip_plane_t* plane)
{
    int			i, next_i, j;
    static fixed_t	dot[ RP_MAX_SUBSECTOR_VERTICES ];
    vertex_t		new_vertices[2];
    fixed_t		part;
    int			vertices_clipped;

    vertices_clipped = 0;
    for( i = 0; i < vertex_count; i++ )
    {
	dot[i] =
	    FixedMul(vertices[i].x, plane->n[0]) +
	    FixedMul(vertices[i].y, plane->n[1]) +
	    plane->dist;
	if( dot[i] < 0 ) vertices_clipped++;
    }
    if (vertices_clipped == 0) return vertex_count;
    else if(vertices_clipped == vertex_count) return 0;

    // index of edge is index of it first vertex
    // 0 edge - where first vertex clipped and second vertex passed
    // 1 edge - where second vertex clipped and first vertex passed
    // dot < 0 means clipped
    int splitted_edges[2] = {0, 0 }; // handle -W-maybe-uninitialized
    for( i = 0; i < vertex_count; i++ )
    {
	next_i = i + 1;
	if (next_i == vertex_count) next_i = 0;

	if (dot[i] < 0 && dot[next_i] >= 0)
	{
	    splitted_edges[0] = i;

	    part = FixedDiv(-dot[i], dot[next_i] - dot[i]);
	    new_vertices[0].x = vertices[i].x + FixedMul(part, vertices[next_i].x - vertices[i].x);
	    new_vertices[0].y = vertices[i].y + FixedMul(part, vertices[next_i].y - vertices[i].y);
	}
	if (dot[i] >= 0 && dot[next_i] < 0)
	{
	    splitted_edges[1] = i;

	    part = FixedDiv(-dot[next_i], dot[i] - dot[next_i]);
	    new_vertices[1].x = vertices[next_i].x + FixedMul(part, vertices[i].x - vertices[next_i].x);
	    new_vertices[1].y = vertices[next_i].y + FixedMul(part, vertices[i].y - vertices[next_i].y);
	}
    }

    if (vertices_clipped == 2)
    {
	vertices[splitted_edges[0]] = new_vertices[0];

	    next_i = (splitted_edges[1] + 1) % vertex_count;
	    vertices[next_i] = new_vertices[1];
    }
    else if (vertices_clipped > 2)
    {
	// TODO - handle make this case more optimal
	vertex_t tmp_vertices[ RP_MAX_SUBSECTOR_VERTICES ];
	for( i = 0; i < vertex_count; i++ ) tmp_vertices[i] = vertices[i];

	for( i = 0, j = splitted_edges[0] + 1; i < vertex_count - vertices_clipped; i++, j++ )
	    vertices[i] = tmp_vertices[j % vertex_count];
	vertices[i  ] = new_vertices[1];
	vertices[i+1] = new_vertices[0];
    }
    else // clip 1 vertex
    {
	int clipped_vertex = splitted_edges[0];
	for( i = vertex_count; i > clipped_vertex; i-- )
	    vertices[i] = vertices[i-1];
	vertices[clipped_vertex  ] = new_vertices[1];
	vertices[clipped_vertex+1] = new_vertices[0];

    }
    return vertex_count + 2 - vertices_clipped;
}

static void BuildSubsector(int subsector_num)
{
    int			i;
    subsector_t*	subsector;
    seg_t*		seg;
    clip_plane_t	seg_clip_plane;

    subsector = &subsectors[ subsector_num ];

    // TODO - use smaller bounding box, like sector bounding box, for example
    g_cur_subsector_vertex_count = 4;
    g_cur_subsector_vertices[0].x = g_level_bbox_min[0];
    g_cur_subsector_vertices[0].y = g_level_bbox_min[1];
    g_cur_subsector_vertices[1].x = g_level_bbox_min[0];
    g_cur_subsector_vertices[1].y = g_level_bbox_max[1];
    g_cur_subsector_vertices[2].x = g_level_bbox_max[0];
    g_cur_subsector_vertices[2].y = g_level_bbox_max[1];
    g_cur_subsector_vertices[3].x = g_level_bbox_max[0];
    g_cur_subsector_vertices[3].y = g_level_bbox_min[1];

    for( i = 0; i < g_cur_subsector_parent_nodes_count; i++ )
    {
	g_cur_subsector_vertex_count =
	    R_32b_ClipPolygon(
		g_cur_subsector_vertices,
		g_cur_subsector_vertex_count,
		&g_nodes_clip_planes[i]);
    } // for clip planes

    for( i = 0; i < subsector->numlines; i++ )
    {
	seg = &segs[subsector->firstline + i];
	NormalizeVec( seg->v2->y - seg->v1->y, seg->v1->x - seg->v2->x, seg_clip_plane.n );
	seg_clip_plane.dist = - (FixedMul(seg->v1->x, seg_clip_plane.n[0]) + FixedMul(seg->v1->y, seg_clip_plane.n[1]));

	g_cur_subsector_vertex_count =
	    R_32b_ClipPolygon(
		g_cur_subsector_vertices,
		g_cur_subsector_vertex_count,
		&seg_clip_plane);
    }

    AddSubsector(subsector_num);
}

static void Node_r(int node_num)
{
    clip_plane_t* 	clip_plane;
    node_t*		node;

    if (node_num & NF_SUBSECTOR)
    {
	BuildSubsector(node_num&(~NF_SUBSECTOR));
    }
    else
    {
	node = &nodes[node_num];
	clip_plane = &g_nodes_clip_planes[ g_cur_subsector_parent_nodes_count ];

	g_cur_subsector_parent_nodes_stack[ g_cur_subsector_parent_nodes_count ] = node;
	g_cur_subsector_parent_nodes_count++;

	NormalizeVec(node->dy, -node->dx, clip_plane->n);
	clip_plane->dist = - (FixedMul(node->x, clip_plane->n[0]) + FixedMul(node->y, clip_plane->n[1]));
	Node_r(node->children[0]);

	clip_plane->n[0] = -clip_plane->n[0];
	clip_plane->n[1] = -clip_plane->n[1];
	clip_plane->dist = -clip_plane->dist;
	Node_r(node->children[1]);

	g_cur_subsector_parent_nodes_count--;
    }
}

void R_32b_BuildFullSubsectors()
{
    PrepareOutBuffer();

    g_cur_subsector_parent_nodes_count = 0;

    { // build bounding box of all level
	const int c_bbox_add_eps = FRACUNIT;

	node_t* root_node = &nodes[numnodes-1];

	g_level_bbox_min[0] = root_node->bbox[0][BOXLEFT];
	if (g_level_bbox_min[0] > root_node->bbox[1][BOXLEFT]) g_level_bbox_min[0] = root_node->bbox[1][BOXLEFT];
	g_level_bbox_max[0] = root_node->bbox[0][BOXRIGHT];
	if (g_level_bbox_max[0] < root_node->bbox[1][BOXRIGHT]) g_level_bbox_max[0] = root_node->bbox[1][BOXRIGHT];

	g_level_bbox_min[1] = root_node->bbox[0][BOXBOTTOM];
	if (g_level_bbox_min[1] > root_node->bbox[1][BOXBOTTOM]) g_level_bbox_min[1] = root_node->bbox[1][BOXBOTTOM];
	g_level_bbox_max[1] = root_node->bbox[0][BOXTOP];
	if (g_level_bbox_max[1] < root_node->bbox[1][BOXTOP]) g_level_bbox_max[1] = root_node->bbox[1][BOXTOP];

	g_level_bbox_min[0] -= c_bbox_add_eps;
	g_level_bbox_min[1] -= c_bbox_add_eps;
	g_level_bbox_max[0] += c_bbox_add_eps;
	g_level_bbox_max[1] += c_bbox_add_eps;
    }

    Node_r(numnodes-1);
}

vertex_t* R_32b_GetFullSubsectorsVertices()
{
    return g_out_subsectors_vertices;
}

full_subsector_t* R_32b_GetFullSubsectors()
{
    return g_out_subsectors;
}
