/*
 * win_minimal.h - the handful of Win32 types and functions this mod uses.
 *
 * We declare them by hand instead of including <windows.h> so the project
 * builds with nothing but clang + lld (no Windows SDK needed) and so a
 * reader can see at a glance exactly which system functions the mod touches.
 */
#ifndef RE1_WIN_MINIMAL_H
#define RE1_WIN_MINIMAL_H

#include <stdarg.h>

#define WINAPI __stdcall
#define CDECL  __cdecl

typedef int            BOOL;
typedef int            HRESULT;
typedef unsigned int   UINT;
typedef unsigned long  DWORD;     /* 32-bit on Windows, also in this build   */
typedef unsigned long  ULONG;
typedef void          *HANDLE;
typedef void          *HMODULE;
typedef void          *HWND;
typedef const char    *LPCSTR;
typedef char          *LPSTR;

#define TRUE  1
#define FALSE 0
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr)    ((HRESULT)(hr) <  0)

#define DLL_PROCESS_ATTACH      1
#define PAGE_READWRITE          0x04
#define GENERIC_WRITE           0x40000000UL
#define FILE_SHARE_READ         0x00000001UL
#define CREATE_ALWAYS           2
#define FILE_ATTRIBUTE_NORMAL   0x80
#define INVALID_HANDLE_VALUE    ((HANDLE)(long)-1)
#define MAX_PATH                260

/* ---- kernel32 ---- */
HMODULE WINAPI LoadLibraryA(LPCSTR name);
void   *WINAPI GetProcAddress(HMODULE module, LPCSTR name);
DWORD   WINAPI GetModuleFileNameA(HMODULE module, LPSTR buf, DWORD size);
UINT    WINAPI GetSystemDirectoryA(LPSTR buf, UINT size);
BOOL    WINAPI VirtualProtect(void *addr, DWORD size, DWORD newProt, DWORD *oldProt);
HANDLE  WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share, void *sec,
                           DWORD disposition, DWORD flags, HANDLE tmpl);
BOOL    WINAPI WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *written, void *ov);
BOOL    WINAPI CloseHandle(HANDLE h);
BOOL    WINAPI DisableThreadLibraryCalls(HMODULE module);
DWORD   WINAPI GetCurrentProcessId(void);
UINT    WINAPI GetPrivateProfileIntA(LPCSTR section, LPCSTR key, int def, LPCSTR file);
DWORD   WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR def,
                                        LPSTR out, DWORD size, LPCSTR file);

/* ---- user32 ---- */
short   WINAPI GetAsyncKeyState(int vkey);
HWND    WINAPI GetForegroundWindow(void);
DWORD   WINAPI GetWindowThreadProcessId(HWND wnd, DWORD *pid);
int     WINAPI wvsprintfA(LPSTR out, LPCSTR fmt, va_list args);

#endif /* RE1_WIN_MINIMAL_H */
