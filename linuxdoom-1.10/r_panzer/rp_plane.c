#include "rp_defs.h"
#include "../m_bbox.h"

#include <stdlib.h>

#define RP_MAX_SUBSECTOR_VERTICES 64
#define RP_MAX_BSP_TREE_DEPTH 128


static full_subsector_t*	g_out_subsectors = NULL;
static vertex_t*		g_out_subsectors_vertices = NULL;
static int			g_out_subsectors_vertex_count = 0;
static int			g_out_subsectors_vertices_capacity = 0;

static vertex_t		g_cur_subsector_vertices[ RP_MAX_SUBSECTOR_VERTICES ];
static int		g_cur_subsector_vertex_count;

node_t*			g_cur_subsector_parent_nodes_stack[ RP_MAX_BSP_TREE_DEPTH ];
clip_plane_t		g_nodes_clip_planes[ RP_MAX_BSP_TREE_DEPTH ];
static int		g_cur_subsector_parent_nodes_count;

// extern bsp strustures
extern subsector_t*	subsectors;
extern int		numsubsectors;

extern node_t*		nodes;
extern int		numnodes;


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

static void BuildSubsector(int subsector_num)
{
    const int		c_bbox_add_eps = FRACUNIT * 4;
    int			i;
    subsector_t*	subsector;
    node_t*		parent_node;
    fixed_t		bb_min[2];
    fixed_t		bb_max[2];

    subsector = &subsectors[ subsector_num ];
    parent_node = g_cur_subsector_parent_nodes_stack[ g_cur_subsector_parent_nodes_count - 1 ];

    bb_min[0] = parent_node->bbox[0][BOXLEFT];
    if (bb_min[0] > parent_node->bbox[1][BOXLEFT]) bb_min[0] = parent_node->bbox[1][BOXLEFT];
    bb_max[0] = parent_node->bbox[0][BOXRIGHT];
    if (bb_max[0] < parent_node->bbox[1][BOXRIGHT]) bb_max[0] = parent_node->bbox[1][BOXRIGHT];

    bb_min[1] = parent_node->bbox[0][BOXBOTTOM];
    if (bb_min[1] > parent_node->bbox[1][BOXBOTTOM]) bb_min[1] = parent_node->bbox[1][BOXBOTTOM];
    bb_max[1] = parent_node->bbox[0][BOXTOP];
    if (bb_max[1] < parent_node->bbox[1][BOXTOP]) bb_max[1] = parent_node->bbox[1][BOXTOP];


    g_cur_subsector_vertex_count = 4;
    g_cur_subsector_vertices[0].x = bb_min[0] - c_bbox_add_eps;
    g_cur_subsector_vertices[0].y = bb_min[1] - c_bbox_add_eps;
    g_cur_subsector_vertices[1].x = bb_min[0] - c_bbox_add_eps;
    g_cur_subsector_vertices[1].y = bb_max[1] + c_bbox_add_eps;
    g_cur_subsector_vertices[2].x = bb_max[0] + c_bbox_add_eps;
    g_cur_subsector_vertices[2].y = bb_max[1] + c_bbox_add_eps;
    g_cur_subsector_vertices[3].x = bb_max[0] + c_bbox_add_eps;
    g_cur_subsector_vertices[3].y = bb_min[1] - c_bbox_add_eps;

    AddSubsector(subsector_num);
}

static void Node_r(int node_num)
{
   clip_plane_t* clip_plane;

    if (node_num & NF_SUBSECTOR)
    {
	BuildSubsector(node_num&(~NF_SUBSECTOR));
    }
    else
    {
    	clip_plane = &g_nodes_clip_planes[ g_cur_subsector_parent_nodes_count ];

    	g_cur_subsector_parent_nodes_stack[ g_cur_subsector_parent_nodes_count ] = &nodes[node_num];
    	g_cur_subsector_parent_nodes_count++;

	Node_r(nodes[node_num].children[0]);
	Node_r(nodes[node_num].children[1]);

	g_cur_subsector_parent_nodes_count--;
    }
}

void R_32b_BuildFullSubsectors()
{
    PrepareOutBuffer();

    g_cur_subsector_parent_nodes_count = 0;

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
