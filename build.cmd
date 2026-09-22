@echo off
rem  build.cmd -- one-click N64 Doom ROM build.
rem
rem  1. put your IWAD (DOOM2.WAD, DOOM.WAD, DOOMU.WAD, DOOM1.WAD,
rem     PLUTONIA.WAD or TNT.WAD) in  input\
rem  2. run this file
rem  3. the ROM appears in  output\<PREFIX>.z64
rem
rem  Needs fluidsynth and ffmpeg on PATH the first time (to render the
rem  IWAD's music) - see README.md for where to get them. Extra options
rem  are passed straight through, e.g.:
rem     build.cmd --force-music
setlocal
cd /d "%~dp0"

set "PY=python"
where python >nul 2>&1 || set "PY=py -3"

%PY% tools\build_rom.py %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo Build FAILED ^(exit %RC%^).
) else (
    echo Done. Check output\ for your ROM.
)
pause
exit /b %RC%
