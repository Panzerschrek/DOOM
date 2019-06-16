#include "d_player.h"

// PANZER - rendering interface

void (*R_SetViewSize)(int blocks, int detail);
void (*R_RenderPlayerView)(player_t *player);

void (*R_InitData)(void);
void (*R_InitSprites)(char** namelist);
void (*R_ClearSprites)(void);
void (*R_PrecacheLevel)(void);

int (*R_FlatNumForName)(char* name);
int (*R_TextureNumForName)(char* name);
int (*R_CheckTextureNumForName)(char* name);
