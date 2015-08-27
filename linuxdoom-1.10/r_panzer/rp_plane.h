// Copyright (C) 2015 by Artöm "Panzerschrek" Kunç.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.


#ifndef __RP_PLANE__
#define __RP_PLANE__

#include "rp_defs.h"

void			RP_BuildFullSubsectors();

vertex_t*		RP_GetFullSubsectorsVertices();
full_subsector_t*	RP_GetFullSubsectors();

int RP_ClipPolygon(vertex_t* vertices, int vertex_count, clip_plane_t* plane);

#endif//PLANE
