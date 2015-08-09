#ifndef __RP_PLANE__
#define __RP_PLANE__

#include "rp_defs.h"

void			RP_BuildFullSubsectors();

vertex_t*		RP_GetFullSubsectorsVertices();
full_subsector_t*	RP_GetFullSubsectors();

int RP_ClipPolygon(vertex_t* vertices, int vertex_count, clip_plane_t* plane);

#endif//PLANE
