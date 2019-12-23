// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
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
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for SDL.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

extern "C"
{

#include <stdlib.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"
#include "w_wad.h"
#include "z_zone.h"

#include "doomdef.h"

#include "r_panzer/rp_defs.h"
#include "r_panzer/rp_main.h"
#include "r_panzer/rp_video.h"

} // extern "C"

#include <QtWidgets/QWidget>
#include <QApplication>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>

#define MOUSE_MOTION_SCALE 3

// max scaler - for future ultra-hyper-über displays
#define MAX_SCREEN_SCALER 32
#define MAX_SCREEN_SIZE 10000

extern "C"
{
// m_misc.c
extern int	usemouse;

// settings variables
int		v_fullscreen;
int		v_display;
int		v_32bit;
int		v_scaler;
int		v_system_window_width ;
int		v_system_window_height;

}

enum
{
    COMPONENT_R,
    COMPONENT_G,
    COMPONENT_B,
    COMPONENT_A,
};

static int g_palette_lump_num;

const int TranslateKey(int key)
{
	switch(key)
	{
	case Qt::Key_Left:		return KEY_LEFTARROW;
	case Qt::Key_Right:	return KEY_RIGHTARROW;
	case Qt::Key_Up:		return KEY_UPARROW;
	case Qt::Key_Down:		return KEY_DOWNARROW;

	case Qt::Key_Escape:	return KEY_ESCAPE;
	case Qt::Key_Return:	return KEY_ENTER;
	case Qt::Key_Tab:		return KEY_TAB;

	case Qt::Key_F1:		return KEY_F1;
	case Qt::Key_F2:		return KEY_F2;
	case Qt::Key_F3:		return KEY_F3;
	case Qt::Key_F4:		return KEY_F4;
	case Qt::Key_F5:		return KEY_F5;
	case Qt::Key_F6:		return KEY_F6;
	case Qt::Key_F7:		return KEY_F7;
	case Qt::Key_F8:		return KEY_F8;
	case Qt::Key_F9:		return KEY_F9;
	case Qt::Key_F10:		return KEY_F10;
	case Qt::Key_F11:		return KEY_F11;
	case Qt::Key_F12:		return KEY_F12;

	case Qt::Key_Backspace:
	case Qt::Key_Delete:	return KEY_BACKSPACE;

	case Qt::Key_Pause:	return KEY_PAUSE;
	case Qt::Key_Equal:	return KEY_EQUALS;

	case Qt::Key_Minus:	return KEY_MINUS;

	case Qt::Key_Shift:	return KEY_RSHIFT;

	case Qt::Key_Control:	return KEY_RCTRL;

	case Qt::Key_Alt:	return KEY_RALT;

	default:
		if (key >= Qt::Key_A && key <= Qt::Key_Z )
		return key - Qt::Key_A + 'a';
		if ( key >= Qt::Key_0 && key <= Qt::Key_9 )
		return key - Qt::Key_0 + '0';

	// some keys do not need translate, sdl keys is like internal keys
	return key;
	}
}

int TranslateMouseButton(int button)
{
	switch(button)
	{
	case Qt::LeftButton: return 0;
	case Qt::RightButton: return 1;
	case Qt::MiddleButton: return 2;
	}
	return 3;
}

class DoomWindow : public QWidget
{
public:

	QImage image_;

	virtual void paintEvent(QPaintEvent *event) override
	{
		int		i;
		pixel_t*	p;
		int		must_lock;
		pixel_t*	palette;
		int		x, y;
		fixed_t	inv_scaler;

		palette = VP_GetPaletteStorage();
		p= reinterpret_cast<pixel_t*>(image_.bits());
		for( i = 0; i < v_system_window_width * v_system_window_height; i++ )
			p[i] = palette[ screens[0][i] ];

		QPainter painter( this );
		painter.drawImage( QPoint(0,0), image_ );

		QWidget::update(); // Run in loop.
	}

	virtual void keyPressEvent(QKeyEvent * event) override
	{
		event->accept();
		event_t out_event;
		out_event.type = ev_keydown;
		out_event.data1 = TranslateKey(event->key());
		D_PostEvent(&out_event);
	}

	virtual void keyReleaseEvent(QKeyEvent * event) override
	{
		event->accept();
		event_t out_event;
		out_event.type = ev_keyup;
		out_event.data1 = TranslateKey(event->key());
		D_PostEvent(&out_event);
	}

	virtual void mousePressEvent(QMouseEvent * event) override
	{
		event->accept();

		mouse_keys_state_ |= ( 1 << TranslateMouseButton(event->button()) );

		event_t out_event;
		out_event.type = ev_mouse;
		out_event.data1 = mouse_keys_state_;
		out_event.data2 = out_event.data3 = 0;
		D_PostEvent(&out_event);
	}

	virtual void mouseReleaseEvent(QMouseEvent * event) override
	{
		event->accept();

		mouse_keys_state_ &= ~( 1 << TranslateMouseButton(event->button()) );

		event_t out_event;
		out_event.type = ev_mouse;
		out_event.data1 = mouse_keys_state_;
		out_event.data2 = out_event.data3 = 0;
		D_PostEvent(&out_event);
	}

	virtual void mouseMoveEvent(QMouseEvent * event) override
	{
		event->accept();

		event_t out_event;
		out_event.type = ev_mouse;
		out_event.data1 = mouse_keys_state_;
		out_event.data2 = prev_mouse_x_ - event->x();
		out_event.data3 = prev_mouse_y_ - event->y();
		D_PostEvent(&out_event);

		prev_mouse_x_= event->x();
		prev_mouse_y_= event->y();
	}

	virtual void closeEvent(QCloseEvent*) override
	{
		I_Quit();
	}

private:
	int mouse_keys_state_= 0;
	int prev_mouse_x_= 0;
	int prev_mouse_y_= 0;
};

static DoomWindow* qt_window= nullptr;

extern "C" void I_ShutdownGraphics(void)
{
	if( qt_window != nullptr )
	{
		delete qt_window;
		qt_window= nullptr;
	}
}

//
// I_StartFrame
//
extern "C" void I_StartFrame (void)
{
    // er?
}

extern "C" void I_GrabMouse (void)
{

}

extern "C" void I_UngrabMouse (void)
{

}

extern "C" void I_GetEvent(void)
{
	QApplication::processEvents();
}

//
// I_StartTic
//
extern "C" void I_StartTic (void)
{
	I_GetEvent();
}

//
// I_UpdateNoBlit
//
extern "C" void I_UpdateNoBlit (void)
{
    // what is this?
}

//
// I_FinishUpdate
//
extern "C" void I_FinishUpdate (void)
{
}

//
// I_ReadScreen
//
extern "C" void I_ReadScreen (byte* scr)
{
}

//
// I_SetPalette
//
extern "C" void I_SetPalette (int palette_num)
{
	// Input format - RGB
	byte*	palette;
	int		i;
	pixel_t* pal = VP_GetPaletteStorage();

	if (!v_32bit || palette_num == -1)
	{
		if (palette_num == -1) palette_num = 0;
		palette = ((byte*)W_CacheLumpNum(g_palette_lump_num, PU_STATIC)) + 768 * palette_num;

		for( i = 0; i < 256; i++ )
		{
			pal[i].components[2] = palette[i*3  ];
			pal[i].components[1] = palette[i*3+1];
			pal[i].components[0] = palette[i*3+2];
			pal[i].components[3] = 255;
		}
	}
	else
		RP_SetPlaypalNum(palette_num);
}

extern "C" void I_PrepareGraphics (void)
{
	SCREENWIDTH  = v_system_window_width ;
	SCREENHEIGHT = v_system_window_height;
}

extern "C" void I_InitGraphics(void)
{
	g_palette_lump_num = W_GetNumForName("PLAYPAL");
	I_SetPalette(-1);

	qt_window= new DoomWindow();

	qt_window->image_= QImage( v_system_window_width, v_system_window_height, QImage::Format_RGB32 );
	qt_window->resize( v_system_window_width, v_system_window_height );
	qt_window->show();

}
