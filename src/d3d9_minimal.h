/*
 * d3d9_minimal.h - just enough of Direct3D 9 to hook the game.
 *
 * A COM object is a pointer to a pointer to a table of functions (the
 * "vtable"). Instead of redeclaring the whole D3D9 API we only record the
 * vtable SLOT NUMBER of each method we need. The slot numbers come from the
 * order of methods in d3d9.h and never change.
 */
#ifndef RE1_D3D9_MINIMAL_H
#define RE1_D3D9_MINIMAL_H

#include "win_minimal.h"
#include "layout.h"     /* for Rect (identical memory layout to Win32 RECT) */

/* The vtable of a COM object `p` is  *(void ***)p  */
#define VTABLE(p) (*(void ***)(p))

/* ---- IUnknown (shared by every COM object) ---- */
#define SLOT_ADDREF   1
#define SLOT_RELEASE  2

/* ---- IDirect3D9 ---- */
#define SLOT_D3D9_CREATEDEVICE 16

/* ---- IDirect3DDevice9 ---- */
#define SLOT_DEV_RESET               16
#define SLOT_DEV_PRESENT             17
#define SLOT_DEV_GETBACKBUFFER       18
#define SLOT_DEV_CREATERENDERTARGET  28
#define SLOT_DEV_STRETCHRECT         34
#define SLOT_DEV_COLORFILL           35

/* ---- IDirect3DSurface9 ---- */
#define SLOT_SURF_GETDESC 12

/* D3DTEXTUREFILTERTYPE values accepted by StretchRect */
#define D3DTEXF_POINT  1
#define D3DTEXF_LINEAR 2

typedef struct {
    UINT  Format, Type;
    DWORD Usage;
    UINT  Pool, MultiSampleType;
    DWORD MultiSampleQuality;
    UINT  Width, Height;
} D3DSURFACE_DESC;

typedef struct {
    UINT  BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    UINT  MultiSampleType;
    DWORD MultiSampleQuality;
    UINT  SwapEffect;
    HWND  hDeviceWindow;
    BOOL  Windowed;
    BOOL  EnableAutoDepthStencil;
    UINT  AutoDepthStencilFormat;
    DWORD Flags;
    UINT  FullScreen_RefreshRateInHz;
    UINT  PresentationInterval;
} D3DPRESENT_PARAMETERS;

/* ---- function pointer types for the calls we hook or make ---- */
typedef ULONG   (WINAPI *PFN_AddRef)(void *self);
typedef ULONG   (WINAPI *PFN_Release)(void *self);
typedef HRESULT (WINAPI *PFN_CreateDevice)(void *d3d, UINT adapter, UINT devType, HWND focus,
                                           DWORD behavior, D3DPRESENT_PARAMETERS *pp, void **outDev);
typedef HRESULT (WINAPI *PFN_Reset)(void *dev, D3DPRESENT_PARAMETERS *pp);
typedef HRESULT (WINAPI *PFN_Present)(void *dev, const Rect *src, const Rect *dst,
                                      HWND wnd, const void *dirty);

/* ---- thin helpers so the main code reads like normal COM calls ---- */
static ULONG ComRelease(void *obj)
{
    return ((PFN_Release)VTABLE(obj)[SLOT_RELEASE])(obj);
}

static HRESULT Dev_GetBackBuffer(void *dev, void **outSurface)
{
    typedef HRESULT (WINAPI *Fn)(void *, UINT swapChain, UINT index, UINT type, void **out);
    return ((Fn)VTABLE(dev)[SLOT_DEV_GETBACKBUFFER])(dev, 0, 0, 0 /*D3DBACKBUFFER_TYPE_MONO*/, outSurface);
}

static HRESULT Dev_CreateRenderTarget(void *dev, UINT w, UINT h, UINT format, void **outSurface)
{
    typedef HRESULT (WINAPI *Fn)(void *, UINT, UINT, UINT, UINT msType, DWORD msQuality,
                                 BOOL lockable, void **out, HANDLE *shared);
    return ((Fn)VTABLE(dev)[SLOT_DEV_CREATERENDERTARGET])(dev, w, h, format, 0, 0, FALSE, outSurface, 0);
}

static HRESULT Dev_StretchRect(void *dev, void *src, const Rect *srcRect,
                               void *dst, const Rect *dstRect, UINT filter)
{
    typedef HRESULT (WINAPI *Fn)(void *, void *, const Rect *, void *, const Rect *, UINT);
    return ((Fn)VTABLE(dev)[SLOT_DEV_STRETCHRECT])(dev, src, srcRect, dst, dstRect, filter);
}

static HRESULT Dev_ColorFill(void *dev, void *surface, DWORD argb)
{
    typedef HRESULT (WINAPI *Fn)(void *, void *, const Rect *, DWORD);
    return ((Fn)VTABLE(dev)[SLOT_DEV_COLORFILL])(dev, surface, 0, argb);
}

static HRESULT Surface_GetDesc(void *surface, D3DSURFACE_DESC *desc)
{
    typedef HRESULT (WINAPI *Fn)(void *, D3DSURFACE_DESC *);
    return ((Fn)VTABLE(surface)[SLOT_SURF_GETDESC])(surface, desc);
}

#endif /* RE1_D3D9_MINIMAL_H */
