@echo off
rem Builds p2patch.dll (32-bit) and the installer.
rem Requires Visual Studio with the C++ x86 tools and the .NET Framework 4 C# compiler.
setlocal
set "ROOT=%~dp0"
set "OUT=%ROOT%build"
if not exist "%OUT%" mkdir "%OUT%"

set "VSDIR="
for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*") do for /d %%e in ("%%v\*") do if exist "%%e\VC\Auxiliary\Build\vcvars32.bat" set "VSDIR=%%e"
if not defined VSDIR goto novs
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1

pushd "%OUT%"
fxc /nologo /T vs_4_0 /E VSMain /Vn g_VSMain /Fh upscale_vs.h "%ROOT%src\p2patch\upscale.hlsl" >nul || (popd & exit /b 1)
fxc /nologo /T ps_4_0 /E PSMain /Vn g_PSMain /Fh upscale_ps.h "%ROOT%src\p2patch\upscale.hlsl" >nul || (popd & exit /b 1)
cl /nologo /O2 /W3 /MT /GS- /LD /EHsc /D_CRT_SECURE_NO_WARNINGS /I. ^
   "%ROOT%src\p2patch\presenter.cpp" ^
   "%ROOT%src\p2patch\main.c" "%ROOT%src\p2patch\patch.c" "%ROOT%src\p2patch\display.c" ^
   "%ROOT%src\p2patch\sprites.c" "%ROOT%src\p2patch\paths.c" "%ROOT%src\p2patch\vehicles.c" ^
   "%ROOT%src\p2patch\groups.c" "%ROOT%src\p2patch\errors.c" "%ROOT%src\p2patch\gameplay.c" "%ROOT%src\p2patch\perf.c" "%ROOT%src\p2patch\robustness.c" ^
   "%ROOT%src\p2patch\userdata.c" ^
   /Fe:msimg32.dll /link /DEF:"%ROOT%src\p2patch\p2patch.def" /DYNAMICBASE:NO /SAFESEH:NO ^
   user32.lib gdi32.lib kernel32.lib d3d11.lib dxgi.lib winmm.lib delayimp.lib /DELAYLOAD:d3d11.dll || (popd & exit /b 1)
del /q *.obj *.exp *.lib upscale_*.h 2>nul
popd
echo Built %OUT%\msimg32.dll
exit /b 0

:novs
echo Visual Studio C++ x86 tools not found.
exit /b 1
