#ifndef __RP_PLANE__
#define __RP_PLANE__

#include "rp_defs.h"

void			R_32b_BuildFullSubsectors();

vertex_t*		R_32b_GetFullSubsectorsVertices();
full_subsector_t*	R_32b_GetFullSubsectors();

int R_32b_ClipPolygon(vertex_t* vertices, int vertex_count, clip_plane_t* plane);

#endif//PLANE
