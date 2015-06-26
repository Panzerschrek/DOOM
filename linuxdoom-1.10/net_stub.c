#include <stdlib.h>

#include "m_misc.h"
#include "doomdef.h"
#include "doomstat.h"

void I_InitNetwork()
{
    doomcom = malloc (sizeof (*doomcom) );
    memset (doomcom, 0, sizeof(*doomcom) );

    doomcom-> ticdup = 1;
    doomcom-> extratics = 0;

    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
    return;
}

void I_NetCmd()
{
}
