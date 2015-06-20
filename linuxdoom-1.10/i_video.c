// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for SDL.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <stdlib.h>
#include <SDL.h>
#include <SDL_video.h>
#include <SDL_surface.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"

struct
{
    SDL_Window *window;
    SDL_Event last_event;
    SDL_Surface* window_surface;
    byte* screen_data;
    byte palette[1024];

    int mouse_buttons_state_bits;
} sdl;

void I_ShutdownGraphics(void)
{
    SDL_DestroyWindow(sdl.window);
    SDL_ShowCursor(true);

    free( sdl.screen_data );
    screens[0] = NULL;
}

//
// I_StartFrame
//
void I_StartFrame (void)
{
    // er?
}

int sdllatekey(int key)
{
    switch(key)
    {
	case SDLK_LEFT:		return KEY_LEFTARROW;
	case SDLK_RIGHT:	return KEY_RIGHTARROW;
	case SDLK_UP:		return KEY_UPARROW;
	case SDLK_DOWN:		return KEY_DOWNARROW;

	case SDLK_ESCAPE:	return KEY_ESCAPE;
	case SDLK_RETURN:	return KEY_ENTER;
	case SDLK_TAB:		return KEY_TAB;

	case SDLK_F1:		return KEY_F1;
	case SDLK_F2:		return KEY_F2;
	case SDLK_F3:		return KEY_F3;
	case SDLK_F4:		return KEY_F4;
	case SDLK_F5:		return KEY_F5;
	case SDLK_F6:		return KEY_F6;
	case SDLK_F7:		return KEY_F7;
	case SDLK_F8:		return KEY_F8;
	case SDLK_F9:		return KEY_F9;
	case SDLK_F10:		return KEY_F10;
	case SDLK_F11:		return KEY_F11;
	case SDLK_F12:		return KEY_F12;

	case SDLK_BACKSPACE:
	case SDLK_DELETE:	return KEY_BACKSPACE;

	case SDLK_PAUSE:	return KEY_PAUSE;
	case SDLK_EQUALS:	return KEY_EQUALS;

	case SDLK_MINUS:	return KEY_MINUS;

	case SDLK_LSHIFT:
	case SDLK_RSHIFT:	return KEY_RSHIFT;

	case SDLK_LCTRL:
	case SDLK_RCTRL:	return KEY_RCTRL;

	case SDLK_LALT:
	case SDLK_RALT:		return KEY_RALT;

	default:
	    if (key >= SDLK_a && key <= SDLK_z )
		return key - SDLK_a + 'a';
	    if ( key >= SDLK_0 && key <= SDLK_9 )
		return key - SDLK_0 + '0';

	// some keys do not need translate, sdl keys is like internal keys
	return key;
    }
}

int sdllatemousebutton(int button)
{
    switch(button)
    {
	case SDL_BUTTON_LEFT: return 0;
	case SDL_BUTTON_RIGHT: return 1;
	case SDL_BUTTON_MIDDLE: return 2;
	default: return 3;
    };
}

void I_GetEvent(void)
{
    event_t event;
    switch (sdl.last_event.type)
    {
    case SDL_KEYDOWN:
	event.type = ev_keydown;
	event.data1 = sdllatekey(sdl.last_event.key.keysym.sym);
	D_PostEvent(&event);
	break;

    case SDL_KEYUP:
	event.type = ev_keyup;
	event.data1 = sdllatekey(sdl.last_event.key.keysym.sym);
	D_PostEvent(&event);
	break;

    case SDL_MOUSEBUTTONDOWN:
	sdl.mouse_buttons_state_bits |= 1 << sdllatemousebutton(sdl.last_event.button.button);
	event.type = ev_mouse;
	event.data1 = sdl.mouse_buttons_state_bits;
	event.data2 = event.data3 = 0;
	D_PostEvent(&event);
	break;

    case SDL_MOUSEBUTTONUP:
	sdl.mouse_buttons_state_bits &= ~( 1 << sdllatemousebutton(sdl.last_event.button.button) );
	event.type = ev_mouse;
	event.data1 = sdl.mouse_buttons_state_bits;
	event.data2 = event.data3 = 0;
	D_PostEvent(&event);
	break;

    case SDL_MOUSEMOTION:
	event.type = ev_mouse;
	event.data1 = sdl.mouse_buttons_state_bits;
	event.data2 = sdl.last_event.motion.xrel;
	event.data3 = sdl.last_event.motion.yrel;
	D_PostEvent(&event);
	break;

    case SDL_WINDOWEVENT_CLOSE:
    case SDL_QUIT:
	I_Quit();
	break;

    default:
	break;
    }
}

//
// I_StartTic
//
void I_StartTic (void)
{
    while( SDL_PollEvent(&sdl.last_event) )
    {
	I_GetEvent();
    }
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{

    int i;
    int* p;

    p = sdl.window_surface->pixels;
    for( i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++ )
    {
	p[i] = ((int*)sdl.palette)[ screens[0][i] ];
    }
    SDL_UpdateWindowSurface( sdl.window );
}


//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, screens[0], SCREENWIDTH*SCREENHEIGHT);
}

//
// I_SetPalette
//
void I_SetPalette (byte* palette)
{
    int i;
    for( i = 0; i < 256; i++ )
    {
	sdl.palette[i*4  ] = palette[i*3+2];
	sdl.palette[i*4+1] = palette[i*3+1];
	sdl.palette[i*4+2] = palette[i*3  ];
	sdl.palette[i*4+3] = 255;
    }
}

void I_InitGraphics(void)
{
    if ( SDL_Init(SDL_INIT_VIDEO) < 0 )
	I_Error("Could not initialize SDL");

    sdl.window = SDL_CreateWindow(
	"Doom",
	SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	SCREENWIDTH, SCREENHEIGHT, SDL_WINDOW_SHOWN );
    if (!sdl.window)
	I_Error("Could not create window");

    sdl.window_surface = SDL_GetWindowSurface( sdl.window );

    sdl.screen_data = malloc( SCREENWIDTH * SCREENHEIGHT );

    screens[0] = sdl.screen_data;

    SDL_ShowCursor(false);
}
