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

#include "r_panzer/rp_defs.h"
#include "r_panzer/rp_video.h"

#define MOUSE_MOTION_SCALE 3

// max scaler - for future ultra-hyper-über displays
#define MAX_SCREEN_SCALER 32

// m_misc.c
extern int	usemouse;

// settings variables
int		v_fullscreen;
int		v_display;
int		v_32bit;
int		v_scaler;
int		v_system_window_width ;
int		v_system_window_height;

enum
{
    COMPONENT_R,
    COMPONENT_G,
    COMPONENT_B,
};

struct
{
    SDL_Window*		window;
    SDL_Event		last_event;
    SDL_Surface*	window_surface;

    pixel_t*		scaled_framebuffer_32b;

    struct
    {
	int component_index[3];
    } pixel_format;

    int		current_display;
    int		current_display_mode;
    boolean	fullscreen;

    boolean is_focus;
    int mouse_buttons_state_bits;
    int mouse_delta_x;
    int mouse_delta_y;
} sdl;

void I_ShutdownGraphics(void)
{
    if (sdl.scaled_framebuffer_32b) free(sdl.scaled_framebuffer_32b);
    SDL_DestroyWindow(sdl.window);
    SDL_ShowCursor(true);
    SDL_SetRelativeMouseMode(false);
    SDL_VideoQuit();
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

void I_GrabMouse (void)
{
    if (usemouse) SDL_SetRelativeMouseMode(true);
    else SDL_ShowCursor(false);
}

void I_UngrabMouse (void)
{
    if (usemouse) SDL_SetRelativeMouseMode(false);
    else SDL_ShowCursor(true);
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
	sdl.mouse_delta_x += sdl.last_event.motion.xrel;
	sdl.mouse_delta_y += sdl.last_event.motion.yrel;
	break;

    case SDL_WINDOWEVENT_ENTER:
	if (!sdl.is_focus)
	{
	    sdl.is_focus = true;
	    I_GrabMouse();
	}
	break;

    case SDL_WINDOWEVENT_LEAVE:
	if (sdl.is_focus)
	{
	    sdl.is_focus = false;
	    I_UngrabMouse();
	}
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
    event_t event;

    sdl.mouse_delta_x = sdl.mouse_delta_y = 0;

    while( SDL_PollEvent(&sdl.last_event) )
	I_GetEvent();

    if (usemouse)
    {
	event.type = ev_mouse;
	event.data1 = sdl.mouse_buttons_state_bits;
	event.data2 = sdl.mouse_delta_x * MOUSE_MOTION_SCALE;
	event.data3 = -sdl.mouse_delta_y * MOUSE_MOTION_SCALE;
	D_PostEvent(&event);
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
    int		i;
    pixel_t*	p;
    int		must_lock;
    pixel_t*	palette;
    int		x, y;
    fixed_t	inv_scaler;

    must_lock = SDL_MUSTLOCK(sdl.window_surface);

    if (must_lock) SDL_LockSurface( sdl.window_surface );

    if (!v_32bit)
    {
	palette = VP_GetPaletteStorage();
	if (v_scaler == 1)
	{
	    p = sdl.window_surface->pixels;
	    for( i = 0; i < v_system_window_width * v_system_window_height; i++ )
		p[i] = palette[ screens[0][i] ];
	}
	else
	{
	    byte* src;
	    inv_scaler = FRACUNIT / v_scaler;

	    if( (((v_system_window_width-1) * (inv_scaler+1))>>FRACBITS) <= SCREENWIDTH )
		inv_scaler++;

	    p = sdl.window_surface->pixels;
	    for( y = 0; y < v_system_window_height; y++ )
	    {
		src = screens[0] + (y * SCREENHEIGHT / v_system_window_height) * SCREENWIDTH;
		for( x = 0; x < v_system_window_width; x++, p++ )
		    *p = palette[ src[ (x * inv_scaler)>>FRACBITS ] ];
	    }
	}
    }
    else
    {
	if (v_scaler > 1)
	{
	    pixel_t* src;
	    inv_scaler = FRACUNIT / v_scaler;

	    if( (((v_system_window_width-1) * (inv_scaler+1))>>FRACBITS) < SCREENWIDTH )
		inv_scaler++;

	    p = sdl.window_surface->pixels;
	    for( y = 0; y < v_system_window_height; y++ )
	    {
		src = sdl.scaled_framebuffer_32b + (y * SCREENHEIGHT / v_system_window_height) * SCREENWIDTH;
		for( x = 0; x < v_system_window_width; x++, p++ )
		    *p = src[ (x * inv_scaler)>>FRACBITS ];
	    }
	}
    }

    if (must_lock) SDL_UnlockSurface( sdl.window_surface );
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
    // Input format - RGB
    int i;
    pixel_t* pal = VP_GetPaletteStorage();

    for( i = 0; i < 256; i++ )
    {
	pal[i].components[ sdl.pixel_format.component_index[COMPONENT_R] ] = palette[i*3];
	pal[i].components[ sdl.pixel_format.component_index[COMPONENT_G] ] = palette[i*3+1];
	pal[i].components[ sdl.pixel_format.component_index[COMPONENT_B] ] = palette[i*3+2];
    }
}

void I_PrepareGraphics (void)
{
     if(v_scaler < 1) v_scaler = 1;
     else if (v_scaler > MAX_SCREEN_SCALER) v_scaler = MAX_SCREEN_SCALER;

     SCREENWIDTH  = v_system_window_width  / v_scaler;
     SCREENHEIGHT = v_system_window_height / v_scaler;

     // do not try scaling, if effective screen size less then in vanila
     while (SCREENWIDTH < ID_SCREENWIDTH || SCREENHEIGHT < ID_SCREENHEIGHT)
     {
	v_scaler--;
	SCREENWIDTH  = v_system_window_width  / v_scaler;
	SCREENHEIGHT = v_system_window_height / v_scaler;
     }
}

void I_InitGraphics(void)
{
    int			i;
    SDL_DisplayMode	display_mode;
    SDL_PixelFormat*	pixel_format;

    if ( SDL_InitSubSystem(SDL_INIT_VIDEO) < 0 )
	I_Error("Could not initialize SDL");


    sdl.fullscreen = false;
    if (v_fullscreen)
    {
    	int	display_count;
	int	mode_count;

	display_count = SDL_GetNumVideoDisplays();

	if (v_display >= display_count) v_display = 0; // reset display, if we lost previous

	mode_count = SDL_GetNumDisplayModes(v_display);

	for ( i = 0; i < mode_count; i++ )
	{
	    SDL_DisplayMode mode;
	    SDL_GetDisplayMode( v_display, i, &mode );
	    if (mode.w == v_system_window_width && mode.h == v_system_window_height && SDL_BYTESPERPIXEL(mode.format) == 4)
	    {
		sdl.fullscreen = true; // found necessary mode
		display_mode = mode;
	    }
	}
	if (!sdl.fullscreen ) v_fullscreen = 0; // not found necessary mode, reset fullscreen setting
    }

    sdl.window = SDL_CreateWindow(
	"PanzerDoom",
	SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	v_system_window_width, v_system_window_height,
	    SDL_WINDOW_SHOWN | (sdl.fullscreen ? SDL_WINDOW_FULLSCREEN : 0) );
    if (!sdl.window)
	I_Error("I_InitGraphics: Could not create window");

    if (sdl.fullscreen)
    {
    	int result = SDL_SetWindowDisplayMode( sdl.window, &display_mode );
	    printf( "I_InitGraphics: %s %dx%dx%d %dHz\n",
		result ? "warning, could not set display mode" : "set display mode",
		display_mode.w, display_mode.h,
		SDL_BITSPERPIXEL(display_mode.format), display_mode.refresh_rate);

	// reset fullscreen settings, if we have problems
	sdl.fullscreen = !result;
	v_fullscreen = !result;
    }

    sdl.window_surface = SDL_GetWindowSurface( sdl.window );

    pixel_format = sdl.window_surface->format;

    if (pixel_format->BytesPerPixel != 4)
	I_Error("I_InitGraphics: invalid pixel format. Requred 4 bytes per pixel, actual - %d\n", pixel_format->BytesPerPixel);

	 if (pixel_format->Rmask ==       0xFF) sdl.pixel_format.component_index[ COMPONENT_R ] = 0;
    else if (pixel_format->Rmask ==     0xFF00) sdl.pixel_format.component_index[ COMPONENT_R ] = 1;
    else if (pixel_format->Rmask ==   0xFF0000) sdl.pixel_format.component_index[ COMPONENT_R ] = 2;
    else if (pixel_format->Rmask == 0xFF000000) sdl.pixel_format.component_index[ COMPONENT_R ] = 3;
    else sdl.pixel_format.component_index[ COMPONENT_R ] = -1;
	 if (pixel_format->Gmask ==       0xFF) sdl.pixel_format.component_index[ COMPONENT_G ] = 0;
    else if (pixel_format->Gmask ==     0xFF00) sdl.pixel_format.component_index[ COMPONENT_G ] = 1;
    else if (pixel_format->Gmask ==   0xFF0000) sdl.pixel_format.component_index[ COMPONENT_G ] = 2;
    else if (pixel_format->Gmask == 0xFF000000) sdl.pixel_format.component_index[ COMPONENT_G ] = 3;
    else sdl.pixel_format.component_index[ COMPONENT_G ] = -1;
	 if (pixel_format->Bmask ==       0xFF) sdl.pixel_format.component_index[ COMPONENT_B ] = 0;
    else if (pixel_format->Bmask ==     0xFF00) sdl.pixel_format.component_index[ COMPONENT_B ] = 1;
    else if (pixel_format->Bmask ==   0xFF0000) sdl.pixel_format.component_index[ COMPONENT_B ] = 2;
    else if (pixel_format->Bmask == 0xFF000000) sdl.pixel_format.component_index[ COMPONENT_B ] = 3;
    else sdl.pixel_format.component_index[ COMPONENT_B ] = -1;

   if ( sdl.pixel_format.component_index[ COMPONENT_R ] == -1 ||
	sdl.pixel_format.component_index[ COMPONENT_G ] == -1 ||
	sdl.pixel_format.component_index[ COMPONENT_B ] == -1 )
	I_Error("I_InitGraphics: invalid pixel format. Unknown color component order");


    sdl.scaled_framebuffer_32b = NULL;
    if (v_32bit)
    {
	if (v_scaler == 1)
	    VP_SetupFramebuffer (sdl.window_surface->pixels);
	else
	{
	    // In scaled mode, we can not use inner storage of sdl surface as framebuffer, createown instead.
	    sdl.scaled_framebuffer_32b = malloc( SCREENWIDTH * SCREENHEIGHT * sizeof(pixel_t));
	    VP_SetupFramebuffer (sdl.scaled_framebuffer_32b);
	}
    }

    sdl.is_focus = true;
    I_GrabMouse();
}
