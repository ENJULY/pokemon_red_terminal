@echo off
echo [1] START
pause
cd /d "%~dp0"
echo [2] DIR: %cd%
pause
set GXX=C:\MinGW\bin\g++.exe
echo [3] GXX: %GXX%
pause
if not exist %GXX% (
    echo ERROR: g++ not found
    pause
    exit /b 1
)
echo [4] g++ found OK
pause
if not exist build mkdir build
echo [5] building...
%GXX% -std=c++14 src/main.cpp src/engine/renderer.cpp src/engine/input.cpp src/engine/audio.cpp src/game/game.cpp src/game/battle.cpp src/game/overworld.cpp -o build/PokemonRed.exe -lwinmm -I src
echo [6] errorlevel: %errorlevel%
pause
if %errorlevel% neq 0 (
    echo BUILD FAILED
    pause
    exit /b 1
)
echo BUILD OK
pause
build\PokemonRed.exe
pause
