#!/usr/bin/env bash
# Builds d3d9.dll (32-bit, because bhd.exe is a 32-bit program) with only
# clang + lld. No Windows SDK or C runtime is needed.
#
#   ./build.sh            -> build/d3d9.dll
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

# 1. Import libraries for the few system DLL functions we call.
#    (-k strips the @N suffix from stdcall names so they match the real exports)
cat > build/kernel32.def <<'EOF'
LIBRARY kernel32.dll
EXPORTS
LoadLibraryA@4
GetProcAddress@8
GetModuleFileNameA@12
GetSystemDirectoryA@8
VirtualProtect@16
CreateFileA@28
WriteFile@20
CloseHandle@4
DisableThreadLibraryCalls@4
GetCurrentProcessId@0
GetPrivateProfileIntA@16
GetPrivateProfileStringA@24
EOF
cat > build/user32.def <<'EOF'
LIBRARY user32.dll
EXPORTS
GetAsyncKeyState@4
GetForegroundWindow@0
GetWindowThreadProcessId@8
wvsprintfA@12
EOF
llvm-dlltool -m i386 -k -d build/kernel32.def -l build/kernel32.lib
llvm-dlltool -m i386 -k -d build/user32.def   -l build/user32.lib

# 2. Compile.
clang --target=i686-pc-windows-msvc -O2 -Wall -Wextra -std=c11 \
      -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables \
      -mno-stack-arg-probe -c src/re1_true43.c -o build/re1_true43.obj

# 3. Link.
lld-link /dll /nodefaultlib /entry:DllMain /subsystem:windows \
         /def:src/d3d9.def /out:build/d3d9.dll /implib:build/d3d9_proxy.lib \
         /safeseh:no /opt:ref /release \
         build/re1_true43.obj build/kernel32.lib build/user32.lib

echo "built: build/d3d9.dll"
