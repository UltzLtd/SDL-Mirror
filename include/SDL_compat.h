/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2006 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/

/* This file contains functions for backwards compatibility with SDL 1.2 */

#ifndef _SDL_compat_h
#define _SDL_compat_h

#include "SDL_video.h"
#include "SDL_syswm.h"

#include "begin_code.h"
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
/* *INDENT-OFF* */
extern "C" {
/* *INDENT-ON* */
#endif

#define SDL_SWSURFACE       0x00000000
#define SDL_ANYFORMAT       0x00100000
#define SDL_HWPALETTE       0x00200000
#define SDL_DOUBLEBUF       0x00400000
#define SDL_FULLSCREEN      0x00800000
#define SDL_RESIZABLE       0x01000000
#define SDL_NOFRAME         0x02000000
#define SDL_OPENGL          0x04000000
#define SDL_ASYNCBLIT       0x08000000  /* Not used */
#define SDL_HWACCEL         0x08000000  /* Not used */
#define SDL_SCREEN_SURFACE  0x10000000  /* Surface is a window screen surface */
#define SDL_SHADOW_SURFACE  0x20000000  /* Surface is a window shadow surface */

#define SDL_APPMOUSEFOCUS	0x01
#define SDL_APPINPUTFOCUS	0x02
#define SDL_APPACTIVE		0x04

#define SDL_LOGPAL 0x01
#define SDL_PHYSPAL 0x02

#define SDL_ACTIVEEVENT	SDL_EVENT_RESERVED1
#define SDL_VIDEORESIZE	SDL_EVENT_RESERVED2
#define SDL_VIDEOEXPOSE	SDL_EVENT_RESERVED3

typedef struct SDL_VideoInfo
{
    Uint32 hw_available:1;
    Uint32 wm_available:1;
    Uint32 UnusedBits1:6;
    Uint32 UnusedBits2:1;
    Uint32 blit_hw:1;
    Uint32 blit_hw_CC:1;
    Uint32 blit_hw_A:1;
    Uint32 blit_sw:1;
    Uint32 blit_sw_CC:1;
    Uint32 blit_sw_A:1;
    Uint32 blit_fill:1;
    Uint32 UnusedBits3:16;
    Uint32 video_mem;

    SDL_PixelFormat *vfmt;
} SDL_VideoInfo;

/* The most common video overlay formats.
   For an explanation of these pixel formats, see:
   http://www.webartz.com/fourcc/indexyuv.htm

   For information on the relationship between color spaces, see:
   http://www.neuro.sfc.keio.ac.jp/~aly/polygon/info/color-space-faq.html
 */
#define SDL_YV12_OVERLAY  0x32315659    /* Planar mode: Y + V + U  (3 planes) */
#define SDL_IYUV_OVERLAY  0x56555949    /* Planar mode: Y + U + V  (3 planes) */
#define SDL_YUY2_OVERLAY  0x32595559    /* Packed mode: Y0+U0+Y1+V0 (1 plane) */
#define SDL_UYVY_OVERLAY  0x59565955    /* Packed mode: U0+Y0+V0+Y1 (1 plane) */
#define SDL_YVYU_OVERLAY  0x55595659    /* Packed mode: Y0+V0+Y1+U0 (1 plane) */

/* The YUV hardware video overlay */
typedef struct SDL_Overlay
{
    Uint32 format;              /* Read-only */
    int w, h;                   /* Read-only */
    int planes;                 /* Read-only */
    Uint16 *pitches;            /* Read-only */
    Uint8 **pixels;             /* Read-write */

    /* Hardware-specific surface info */
    struct private_yuvhwfuncs *hwfuncs;
    struct private_yuvhwdata *hwdata;

    /* Special flags */
    Uint32 hw_overlay:1;        /* Flag: This overlay hardware accelerated? */
    Uint32 UnusedBits:31;
} SDL_Overlay;

typedef enum
{
    SDL_GRAB_QUERY = -1,
    SDL_GRAB_OFF = 0,
    SDL_GRAB_ON = 1
} SDL_GrabMode;

#define SDL_AllocSurface    SDL_CreateRGBSurface

extern DECLSPEC const SDL_version *SDLCALL SDL_Linked_Version(void);
extern DECLSPEC char *SDLCALL SDL_AudioDriverName(char *namebuf, int maxlen);
extern DECLSPEC char *SDLCALL SDL_VideoDriverName(char *namebuf, int maxlen);
extern DECLSPEC const SDL_VideoInfo *SDLCALL SDL_GetVideoInfo(void);
extern DECLSPEC int SDLCALL SDL_VideoModeOK(int width, int height, int bpp,
                                            Uint32 flags);
extern DECLSPEC SDL_Rect **SDLCALL SDL_ListModes(SDL_PixelFormat * format,
                                                 Uint32 flags);
extern DECLSPEC SDL_Surface *SDLCALL SDL_SetVideoMode(int width, int height,
                                                      int bpp, Uint32 flags);
extern DECLSPEC SDL_Surface *SDLCALL SDL_GetVideoSurface(void);
extern DECLSPEC void SDLCALL SDL_UpdateRects(SDL_Surface * screen,
                                             int numrects, SDL_Rect * rects);
extern DECLSPEC void SDLCALL SDL_UpdateRect(SDL_Surface * screen, Sint32 x,
                                            Sint32 y, Uint32 w, Uint32 h);
extern DECLSPEC int SDLCALL SDL_Flip(SDL_Surface * screen);
extern DECLSPEC SDL_Surface *SDLCALL SDL_DisplayFormat(SDL_Surface * surface);
extern DECLSPEC SDL_Surface *SDLCALL SDL_DisplayFormatAlpha(SDL_Surface *
                                                            surface);
extern DECLSPEC void SDLCALL SDL_WM_SetCaption(const char *title,
                                               const char *icon);
extern DECLSPEC void SDLCALL SDL_WM_GetCaption(char **title, char **icon);
extern DECLSPEC void SDLCALL SDL_WM_SetIcon(SDL_Surface * icon, Uint8 * mask);
extern DECLSPEC int SDLCALL SDL_WM_IconifyWindow(void);
extern DECLSPEC int SDLCALL SDL_WM_ToggleFullScreen(SDL_Surface * surface);
extern DECLSPEC SDL_GrabMode SDLCALL SDL_WM_GrabInput(SDL_GrabMode mode);
extern DECLSPEC int SDLCALL SDL_SetPalette(SDL_Surface * surface, int flags,
                                           const SDL_Color * colors,
                                           int firstcolor, int ncolors);
extern DECLSPEC int SDLCALL SDL_SetScreenColors(SDL_Surface * screen,
                                                const SDL_Color * colors,
                                                int firstcolor, int ncolors);
extern DECLSPEC int SDLCALL SDL_GetWMInfo(SDL_SysWMinfo * info);
extern DECLSPEC Uint8 SDLCALL SDL_GetAppState(void);
extern DECLSPEC void SDLCALL SDL_WarpMouse(Uint16 x, Uint16 y);
extern DECLSPEC SDL_Overlay *SDLCALL SDL_CreateYUVOverlay(int width,
                                                          int height,
                                                          Uint32 format,
                                                          SDL_Surface *
                                                          display);
extern DECLSPEC int SDLCALL SDL_LockYUVOverlay(SDL_Overlay * overlay);
extern DECLSPEC void SDLCALL SDL_UnlockYUVOverlay(SDL_Overlay * overlay);
extern DECLSPEC int SDLCALL SDL_DisplayYUVOverlay(SDL_Overlay * overlay,
                                                  SDL_Rect * dstrect);
extern DECLSPEC void SDLCALL SDL_FreeYUVOverlay(SDL_Overlay * overlay);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
/* *INDENT-OFF* */
}
/* *INDENT-ON* */
#endif
#include "close_code.h"

#endif /* _SDL_compat_h */

/* vi: set ts=4 sw=4 expandtab: */
