@echo off
chcp 65001 >nul 2>&1
cd /d "%~dp0"
setlocal EnableDelayedExpansion

REM ============================================================
REM  Pokemon Red - Terminal Edition Build Script
REM  - Auto-detects g++ from PATH or common MinGW install locations
REM  - Builds and launches the game in one step
REM ============================================================

REM ====== Locate g++ ======
REM 1) Try modern MinGW-w64 install locations FIRST (PATH may have an old GCC)
REM    C++17 inline variables require g++ 7.0+
set "GXX="
for %%P in (
    "C:\mingw64\bin\g++.exe"
    "C:\msys64\ucrt64\bin\g++.exe"
    "C:\msys64\mingw64\bin\g++.exe"
    "C:\Program Files\mingw64\bin\g++.exe"
    "C:\Program Files (x86)\mingw64\bin\g++.exe"
    "%LOCALAPPDATA%\Programs\mingw64\bin\g++.exe"
) do (
    if exist %%~P (
        set "GXX=%%~P"
        REM Prepend g++ directory to PATH so runtime DLLs are found
        set "PATH=%%~dpP;!PATH!"
        goto :found
    )
)

REM 2) Fall back to PATH
where g++ >nul 2>&1
if %errorlevel% equ 0 (
    set "GXX=g++"
    goto :found
)

echo [ERROR] g++.exe not found.
echo.
echo Please install MinGW-w64:
echo   - https://www.msys2.org/    (MSYS2 - recommended)
echo   - https://winlibs.com/      (Portable)
echo.
echo After install, either:
echo   1) Add the bin directory to PATH environment variable
echo      (e.g. C:\mingw64\bin)
echo   2) Or extract to C:\mingw64
echo.
pause
exit /b 1

:found
echo ============================================
echo  PokemonRed - Build
echo ============================================
echo Compiler: %GXX%
"%GXX%" --version | findstr /i "g++"

REM Kill any running game instance to release the exe file lock
taskkill /F /IM PokemonRed.exe >nul 2>&1

if not exist build mkdir build

echo.
echo Compiling...
"%GXX%" -std=c++17 ^
    src\main.cpp ^
    src\engine\renderer.cpp src\engine\input.cpp src\engine\audio.cpp ^
    src\game\game.cpp src\game\battle.cpp src\game\overworld.cpp ^
    -o build\PokemonRed.exe ^
    -lwinmm -I src ^
    -mconsole -DUNICODE -D_UNICODE ^
    -static -static-libgcc -static-libstdc++

set BUILDRC=%errorlevel%

if %BUILDRC% neq 0 (
    echo.
    echo [FAILED] Build failed, exit code %BUILDRC%
    pause
    exit /b 1
)

if not exist build\PokemonRed.exe (
    echo [FAILED] PokemonRed.exe was not produced
    pause
    exit /b 1
)

REM Copy runtime DLLs as fallback (in case -static linking fails)
for %%D in (libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll) do (
    for /f "delims=" %%F in ('where %%D 2^>nul') do (
        if not exist "build\%%D" copy /Y "%%F" build\ >nul 2>&1
    )
)

echo.
echo [OK] Build succeeded
echo ============================================
echo  Launching game...
echo ============================================
echo.

REM Launch the game. Window closes automatically when game exits.
build\PokemonRed.exe
