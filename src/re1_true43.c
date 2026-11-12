/*
 * re1_true43.c - "True 4:3 Fullscreen" for Resident Evil HD Remaster (Steam)
 *
 * WHAT THIS IS
 * ------------
 * A Direct3D 9 proxy. The game loads "d3d9.dll" from its own folder, so when
 * this file is named d3d9.dll it is loaded instead of Microsoft's. We pass
 * every call straight on to the real Direct3D (or to another proxy, see
 * "CHAINING") and only intercept three things:
 *
 *   IDirect3D9::CreateDevice   - to find the device and install our hooks
 *   IDirect3DDevice9::Present  - THE FIX: right before a finished frame is
 *                                shown we crop the 4:3 picture out of the
 *                                game's 16:9 canvas and scale it to fill the
 *                                screen (see layout.h for the geometry)
 *   IDirect3DDevice9::Reset    - to free our private surface on resolution
 *                                changes (required by Direct3D)
 *   IDirect3DDevice9::Release  - to free our surface before the device dies
 *
 * Why not patch bhd.exe? Its code is encrypted by Steam's DRM wrapper, so it
 * cannot be edited safely. Working on the final image also means the menu,
 * inventory, videos and 3D scenes are all handled by one mechanism.
 *
 * CHAINING
 * --------
 * If a file called d3d9_chain.dll sits next to us (put your previous d3d9.dll
 * there, e.g. the g-sync/borderless fix), we load THAT as the "real" d3d9
 * and forward to it. Otherwise we load C:\Windows\System32\d3d9.dll.
 *
 * Build: see build.sh.  Settings: RE1_True43.ini.  Log: RE1_True43.log.
 */
#include "d3d9_minimal.h"

/* ======================================================================== */
/*  Small utilities (no C runtime is linked, so we supply what we need)      */
/* ======================================================================== */

void *memset(void *dst, int value, unsigned size)
{
    unsigned char *p = (unsigned char *)dst;
    while (size--) *p++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned size)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (size--) *d++ = *s++;
    return dst;
}

static unsigned StrLen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }

static int StrEqualNoCase(const char *a, const char *b)
{
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

/* Parses "16:9" into 16 and 9. Returns 1 on success. */
static int ParseAspect(const char *text, int *w, int *h)
{
    int a = 0, b = 0;
    while (*text == ' ') text++;
    if (*text < '0' || *text > '9') return 0;
    while (*text >= '0' && *text <= '9') a = a * 10 + (*text++ - '0');
    if (*text != ':' && *text != '/' && *text != 'x') return 0;
    text++;
    if (*text < '0' || *text > '9') return 0;
    while (*text >= '0' && *text <= '9') b = b * 10 + (*text++ - '0');
    if (a <= 0 || b <= 0 || a > 1000 || b > 1000) return 0;
    *w = a; *h = b;
    return 1;
}

/* ======================================================================== */
/*  Global state - everything the mod remembers lives in this one struct     */
/* ======================================================================== */

typedef struct {
    /* ---- settings (from RE1_True43.ini) ---- */
    int      enabled;                 /* fix on/off, toggled with a hotkey   */
    int      logging;
    int      canvasW, canvasH;        /* aspect of the game's own canvas     */
    int      contentW, contentH;      /* aspect of the picture we keep       */
    FillMode fill;
    int      linearFilter;
    int      disableBackbufferMSAA;
    int      keyToggle, keyReload, keyCycleCanvas;
    char     chainName[MAX_PATH];

    /* ---- paths ---- */
    char     dir[MAX_PATH];           /* folder this DLL lives in (game dir) */
    char     iniPath[MAX_PATH];
    HANDLE   logFile;
    HMODULE  self;

    /* ---- hooked-device bookkeeping ---- */
    void    *device;                  /* the device the game is using        */
    PFN_CreateDevice origCreateDevice;
    PFN_Reset        origReset;
    PFN_Present      origPresent;
    PFN_AddRef       origAddRef;
    PFN_Release      origRelease;

    /* ---- our private copy of the frame ---- */
    void    *copySurface;
    UINT     copyW, copyH, copyFormat;

    /* ---- per-frame cache ---- */
    Layout   layout;
    int      layoutW, layoutH;        /* back buffer size layout was made for */
    int      failStreak;              /* consecutive failed frames            */
    int      warnedMSAA;
    int      frameCount;
    int      keyDown[3];              /* edge detection for hotkeys           */
} State;

static State g;

/* ======================================================================== */
/*  Logging                                                                  */
/* ======================================================================== */

static void Log(const char *fmt, ...)
{
    char line[512];
    DWORD written;
    va_list args;
    int n;

    if (!g.logging || g.logFile == INVALID_HANDLE_VALUE || !g.logFile) return;
    va_start(args, fmt);
    n = wvsprintfA(line, fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n > 509) n = 509;
    line[n++] = '\r';
    line[n++] = '\n';
    WriteFile(g.logFile, line, (DWORD)n, &written, 0);
}

/* ======================================================================== */
/*  Settings                                                                 */
/* ======================================================================== */

static void ReadAspectSetting(const char *key, const char *fallback, int *w, int *h)
{
    char text[32];
    GetPrivateProfileStringA("Layout", key, fallback, text, sizeof text, g.iniPath);
    if (!ParseAspect(text, w, h))
        ParseAspect(fallback, w, h);
}

static void LoadSettings(void)
{
    char text[32];

    g.enabled               = GetPrivateProfileIntA("General", "Enabled", 1, g.iniPath) != 0;
    g.logging               = GetPrivateProfileIntA("General", "Log", 1, g.iniPath) != 0;
    g.disableBackbufferMSAA = GetPrivateProfileIntA("Compatibility", "DisableBackbufferMSAA", 1, g.iniPath) != 0;
    g.keyToggle             = GetPrivateProfileIntA("Hotkeys", "Toggle", 0x78, g.iniPath);        /* F9  */
    g.keyReload             = GetPrivateProfileIntA("Hotkeys", "ReloadSettings", 0x79, g.iniPath);/* F10 */
    g.keyCycleCanvas        = GetPrivateProfileIntA("Hotkeys", "CycleCanvas", 0x7A, g.iniPath);   /* F11 */

    GetPrivateProfileStringA("General", "ChainDll", "d3d9_chain.dll", g.chainName, sizeof g.chainName, g.iniPath);

    ReadAspectSetting("CanvasAspect",  "16:9", &g.canvasW,  &g.canvasH);
    ReadAspectSetting("ContentAspect", "4:3",  &g.contentW, &g.contentH);

    GetPrivateProfileStringA("Layout", "Fill", "cover", text, sizeof text, g.iniPath);
    g.fill = StrEqualNoCase(text, "fit")     ? FILL_FIT
           : StrEqualNoCase(text, "stretch") ? FILL_STRETCH
           :                                   FILL_COVER;

    GetPrivateProfileStringA("Layout", "Filter", "linear", text, sizeof text, g.iniPath);
    g.linearFilter = !StrEqualNoCase(text, "point");

    g.layoutW = 0;   /* force the layout to be recomputed */
}

static void LogSettings(void)
{
    Log("settings: enabled=%d canvas=%d:%d content=%d:%d fill=%d linear=%d noMSAA=%d chain=%s",
        g.enabled, g.canvasW, g.canvasH, g.contentW, g.contentH,
        (int)g.fill, g.linearFilter, g.disableBackbufferMSAA, g.chainName);
}

/* ======================================================================== */
/*  Loading the "real" d3d9 and forwarding the exports we do not handle      */
/* ======================================================================== */

/* Every function the original d3d9.dll exports, in the order of the table. */
static const char *const kForwardNames[] = {
    "D3DPERF_BeginEvent", "D3DPERF_EndEvent", "D3DPERF_GetStatus",
    "D3DPERF_QueryRepeatFrame", "D3DPERF_SetMarker", "D3DPERF_SetOptions",
    "D3DPERF_SetRegion", "Direct3DCreate9", "Direct3DCreate9Ex",
    "Direct3DShaderValidatorCreate9", "PSGPError", "PSGPSampleTexture"
};
#define FORWARD_COUNT 12
#define FWD_Direct3DCreate9 7

/* Addresses of the real functions. The assembly stubs below jump through it. */
void *g_fwd[FORWARD_COUNT];

static void LoadChain(void)
{
    char path[MAX_PATH];
    HMODULE chain = 0;
    unsigned dirLen = StrLen(g.dir), nameLen = StrLen(g.chainName);
    int i;

    if (dirLen + nameLen + 1 < MAX_PATH) {
        memcpy(path, g.dir, dirLen);
        memcpy(path + dirLen, g.chainName, nameLen + 1);
        chain = LoadLibraryA(path);
        if (chain == g.self) chain = 0;           /* misnamed: that is us */
        Log("chain: %s -> %s", path, chain ? "loaded" : "not found");
    }
    if (!chain) {
        UINT n = GetSystemDirectoryA(path, sizeof path);
        const char tail[] = "\\d3d9.dll";
        if (n && n + sizeof tail < MAX_PATH) {
            memcpy(path + n, tail, sizeof tail);
            chain = LoadLibraryA(path);
            Log("chain: system d3d9 %s -> %s", path, chain ? "loaded" : "FAILED");
        }
    }
    if (!chain) return;

    for (i = 0; i < FORWARD_COUNT; i++) {
        g_fwd[i] = GetProcAddress(chain, kForwardNames[i]);
        if (!g_fwd[i]) Log("chain: export %s missing", kForwardNames[i]);
    }
}

/*
 * One tiny assembly stub per export: "jump to whatever address is stored in
 * g_fwd[i]". Because we jump (not call), the arguments and return address
 * are untouched, so it works for any function signature.
 */
#define STR2(x) #x
#define STR(x)  STR2(x)
#define FORWARD_STUB(index, name)                                           \
    __attribute__((naked)) void CDECL Fwd_##name(void)                      \
    { __asm__ volatile("jmp *_g_fwd+" STR(index * 4)); }

FORWARD_STUB(0,  D3DPERF_BeginEvent)
FORWARD_STUB(1,  D3DPERF_EndEvent)
FORWARD_STUB(2,  D3DPERF_GetStatus)
FORWARD_STUB(3,  D3DPERF_QueryRepeatFrame)
FORWARD_STUB(4,  D3DPERF_SetMarker)
FORWARD_STUB(5,  D3DPERF_SetOptions)
FORWARD_STUB(6,  D3DPERF_SetRegion)
/* slot 7 (Direct3DCreate9) is implemented below, it is the entry point we hook */
FORWARD_STUB(8,  Direct3DCreate9Ex)
FORWARD_STUB(9,  Direct3DShaderValidatorCreate9)
FORWARD_STUB(10, PSGPError)
FORWARD_STUB(11, PSGPSampleTexture)

/* ======================================================================== */
/*  The fix: crop + scale the finished frame                                 */
/* ======================================================================== */

static void ReleaseCopySurface(void)
{
    if (g.copySurface) {
        ComRelease(g.copySurface);
        g.copySurface = 0;
    }
}

static int EnsureCopySurface(void *dev, const D3DSURFACE_DESC *bb)
{
    if (g.copySurface && g.copyW == bb->Width && g.copyH == bb->Height && g.copyFormat == bb->Format)
        return 1;

    ReleaseCopySurface();
    if (FAILED(Dev_CreateRenderTarget(dev, bb->Width, bb->Height, bb->Format, &g.copySurface)) || !g.copySurface) {
        g.copySurface = 0;
        Log("could not create %ux%u copy surface (format %u)", bb->Width, bb->Height, bb->Format);
        return 0;
    }
    g.copyW = bb->Width; g.copyH = bb->Height; g.copyFormat = bb->Format;
    Log("created %ux%u copy surface (format %u)", bb->Width, bb->Height, bb->Format);
    return 1;
}

static void UpdateLayout(int w, int h)
{
    if (g.layoutW == w && g.layoutH == h) return;
    ComputeLayout(w, h, g.canvasW, g.canvasH, g.contentW, g.contentH, g.fill, &g.layout);
    g.layoutW = w; g.layoutH = h;
    Log("layout for back buffer %dx%d: canvas(%d,%d,%d,%d) src(%d,%d,%d,%d) -> dst(%d,%d,%d,%d) bars=%d",
        w, h,
        g.layout.canvas.left, g.layout.canvas.top, g.layout.canvas.right, g.layout.canvas.bottom,
        g.layout.src.left, g.layout.src.top, g.layout.src.right, g.layout.src.bottom,
        g.layout.dst.left, g.layout.dst.top, g.layout.dst.right, g.layout.dst.bottom,
        g.layout.dstHasBars);
}

/* Runs just before the game's frame is shown. */
static void ComposeFrame(void *dev)
{
    void *backBuffer = 0;
    D3DSURFACE_DESC desc;
    int ok = 0;

    if (FAILED(Dev_GetBackBuffer(dev, &backBuffer)) || !backBuffer) return;
    if (FAILED(Surface_GetDesc(backBuffer, &desc)))                 goto done;
    if (desc.Width < 64 || desc.Height < 64)                        goto done;   /* minimised */

    if (desc.MultiSampleType != 0) {
        /* StretchRect cannot write into a multisampled surface. */
        if (!g.warnedMSAA) {
            Log("back buffer is multisampled (type %u); fix cannot run. "
                "Keep Compatibility/DisableBackbufferMSAA=1.", desc.MultiSampleType);
            g.warnedMSAA = 1;
        }
        goto done;
    }

    if (!EnsureCopySurface(dev, &desc)) goto done;
    UpdateLayout((int)desc.Width, (int)desc.Height);

    /* 1. Snapshot the finished frame (a surface cannot be stretched onto itself). */
    if (FAILED(Dev_StretchRect(dev, backBuffer, 0, g.copySurface, 0, D3DTEXF_POINT))) goto done;

    /* 2. Paint any bars black (only needed in "fit" mode on non-4:3 screens). */
    if (g.layout.dstHasBars)
        Dev_ColorFill(dev, backBuffer, 0xFF000000);

    /* 3. Paste the cropped region, scaled uniformly, over the back buffer. */
    {
        UINT filter = g.linearFilter ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        HRESULT hr = Dev_StretchRect(dev, g.copySurface, &g.layout.src, backBuffer, &g.layout.dst, filter);
        if (FAILED(hr) && filter == D3DTEXF_LINEAR) {
            Log("linear StretchRect failed (0x%X), using point filtering", (unsigned)hr);
            g.linearFilter = 0;
            hr = Dev_StretchRect(dev, g.copySurface, &g.layout.src, backBuffer, &g.layout.dst, D3DTEXF_POINT);
        }
        ok = SUCCEEDED(hr);
    }

done:
    ComRelease(backBuffer);

    /* Give up (and say so) if it keeps failing, instead of costing time forever. */
    if (ok) {
        g.failStreak = 0;
    } else if (++g.failStreak == 120) {
        Log("120 failed frames in a row - disabling the fix. Press the toggle key to retry.");
        g.enabled = 0;
        g.failStreak = 0;
    }
}

/* ======================================================================== */
/*  Hotkeys                                                                  */
/* ======================================================================== */

static int KeyPressedOnce(int index, int vkey)
{
    int down = vkey && (GetAsyncKeyState(vkey) & 0x8000) != 0;
    int pressed = down && !g.keyDown[index];
    g.keyDown[index] = down;
    return pressed;
}

static int GameIsForeground(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static void HandleHotkeys(void)
{
    /* Canvas guesses to cycle through if the picture looks mis-sized. */
    static const int canvases[][2] = { {16, 9}, {16, 10}, {4, 3}, {21, 9} };
    int toggle, reload, cycle;

    if (!GameIsForeground()) return;
    toggle = KeyPressedOnce(0, g.keyToggle);
    reload = KeyPressedOnce(1, g.keyReload);
    cycle  = KeyPressedOnce(2, g.keyCycleCanvas);

    if (toggle) {
        g.enabled = !g.enabled;
        g.failStreak = 0;
        Log("hotkey: fix %s", g.enabled ? "ON" : "OFF");
    }
    if (reload) {
        LoadSettings();
        Log("hotkey: settings reloaded");
        LogSettings();
    }
    if (cycle) {
        int i, next = 0;
        for (i = 0; i < 4; i++)
            if (canvases[i][0] == g.canvasW && canvases[i][1] == g.canvasH) next = (i + 1) % 4;
        g.canvasW = canvases[next][0];
        g.canvasH = canvases[next][1];
        g.layoutW = 0;                                   /* recompute */
        Log("hotkey: canvas aspect guess is now %d:%d", g.canvasW, g.canvasH);
    }
}

/* ======================================================================== */
/*  Direct3D hooks                                                           */
/* ======================================================================== */

static HRESULT WINAPI Hook_Present(void *dev, const Rect *src, const Rect *dst, HWND wnd, const void *dirty)
{
    if (dev == g.device) {
        HandleHotkeys();
        g.frameCount++;
        /* Only touch full-frame presents; a game passing rects knows best. */
        if (g.enabled && !src && !dst)
            ComposeFrame(dev);
    }
    return g.origPresent(dev, src, dst, wnd, dirty);
}

static HRESULT WINAPI Hook_Reset(void *dev, D3DPRESENT_PARAMETERS *pp)
{
    if (dev == g.device) {
        ReleaseCopySurface();            /* Direct3D refuses Reset while we hold it */
        g.layoutW = 0;
        if (pp && g.disableBackbufferMSAA && pp->MultiSampleType != 0) {
            Log("Reset: MSAA %u removed from back buffer", pp->MultiSampleType);
            pp->MultiSampleType = 0;
            pp->MultiSampleQuality = 0;
        }
        if (pp)
            Log("Reset: back buffer %ux%u windowed=%d", pp->BackBufferWidth, pp->BackBufferHeight, (int)pp->Windowed);
    }
    return g.origReset(dev, pp);
}

static ULONG WINAPI Hook_Release(void *dev)
{
    /* If the game is dropping its last reference, free our surface first so
     * the device can really be destroyed. AddRef+Release reads the count. */
    if (dev == g.device && g.copySurface) {
        g.origAddRef(dev);
        if (g.origRelease(dev) <= 1)
            ReleaseCopySurface();
    }
    return g.origRelease(dev);
}

/* Overwrite one entry of a COM vtable. Returns the original function. */
static void *PatchVtable(void **vtable, int slot, void *replacement)
{
    void *original = vtable[slot];
    DWORD oldProtect;
    if (original == replacement) return original;        /* already patched */
    if (VirtualProtect(&vtable[slot], sizeof(void *), PAGE_READWRITE, &oldProtect)) {
        vtable[slot] = replacement;
        VirtualProtect(&vtable[slot], sizeof(void *), oldProtect, &oldProtect);
    }
    return original;
}

static void HookDevice(void *dev)
{
    void **vt = VTABLE(dev);
    void *orig;

    g.device    = dev;
    g.origAddRef = (PFN_AddRef)vt[SLOT_ADDREF];

    orig = PatchVtable(vt, SLOT_DEV_PRESENT, (void *)Hook_Present);
    if (orig != (void *)Hook_Present) g.origPresent = (PFN_Present)orig;
    orig = PatchVtable(vt, SLOT_DEV_RESET, (void *)Hook_Reset);
    if (orig != (void *)Hook_Reset) g.origReset = (PFN_Reset)orig;
    orig = PatchVtable(vt, SLOT_RELEASE, (void *)Hook_Release);
    if (orig != (void *)Hook_Release) g.origRelease = (PFN_Release)orig;

    Log("device %p hooked", dev);
}

static HRESULT WINAPI Hook_CreateDevice(void *d3d, UINT adapter, UINT devType, HWND focus,
                                        DWORD behavior, D3DPRESENT_PARAMETERS *pp, void **outDev)
{
    HRESULT hr;

    if (pp) {
        Log("CreateDevice: back buffer %ux%u windowed=%d msaa=%u",
            pp->BackBufferWidth, pp->BackBufferHeight, (int)pp->Windowed, pp->MultiSampleType);
        if (g.disableBackbufferMSAA && pp->MultiSampleType != 0) {
            pp->MultiSampleType = 0;
            pp->MultiSampleQuality = 0;
        }
    }
    hr = g.origCreateDevice(d3d, adapter, devType, focus, behavior, pp, outDev);
    Log("CreateDevice: result 0x%X", (unsigned)hr);
    if (SUCCEEDED(hr) && outDev && *outDev)
        HookDevice(*outDev);
    return hr;
}

/* ======================================================================== */
/*  Exported entry point                                                     */
/* ======================================================================== */

typedef void *(WINAPI *PFN_Direct3DCreate9)(UINT sdkVersion);

void *WINAPI Hooked_Direct3DCreate9(UINT sdkVersion)
{
    void *d3d;
    PFN_Direct3DCreate9 real = (PFN_Direct3DCreate9)g_fwd[FWD_Direct3DCreate9];
    if (!real) return 0;

    d3d = real(sdkVersion);
    Log("Direct3DCreate9 -> %p", d3d);
    if (d3d) {
        void *orig = PatchVtable(VTABLE(d3d), SLOT_D3D9_CREATEDEVICE, (void *)Hook_CreateDevice);
        if (orig != (void *)Hook_CreateDevice) g.origCreateDevice = (PFN_CreateDevice)orig;
    }
    return d3d;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DWORD n;
        unsigned i, slash = 0;
        const char iniName[] = "RE1_True43.ini", logName[] = "RE1_True43.log";
        char logPath[MAX_PATH];

        DisableThreadLibraryCalls(module);
        g.self = module;

        /* Work out the folder we live in (the game folder). */
        n = GetModuleFileNameA(module, g.dir, sizeof g.dir);
        for (i = 0; i < n; i++) if (g.dir[i] == '\\' || g.dir[i] == '/') slash = i + 1;
        g.dir[slash] = 0;

        if (slash + sizeof iniName < MAX_PATH) {
            memcpy(g.iniPath, g.dir, slash);
            memcpy(g.iniPath + slash, iniName, sizeof iniName);
            memcpy(logPath, g.dir, slash);
            memcpy(logPath + slash, logName, sizeof logName);
        }

        LoadSettings();
        g.logFile = g.logging
            ? CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0)
            : INVALID_HANDLE_VALUE;

        Log("RE1 True 4:3 Fullscreen proxy loaded from %s", g.dir);
        LogSettings();
        LoadChain();
    }
    return TRUE;
}
