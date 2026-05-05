@echo off
cd /d "%~dp0"
setlocal

:: ====== MinGW 경로 ======
set MGW=C:\mingw64
set GXX=%MGW%\bin\g++.exe
set MGW_LIBEXEC=%MGW%\libexec\gcc\x86_64-w64-mingw32\15.1.0\
set MGW_TLIB=%MGW%\x86_64-w64-mingw32\lib\

:: 깨진 C:\MinGW (구 6.3.0) 우선 차단 — 새 mingw64를 PATH 맨 앞으로
set PATH=%MGW%\bin;%PATH%

if not exist "%GXX%" (
    echo [ERROR] %GXX% not found
    pause
    exit /b 1
)

echo ============================================
echo  PokemonRed - Build
echo ============================================
echo Compiler: %GXX%
"%GXX%" --version | findstr /i "g++"

:: 실행 중인 게임 종료 (exe 잠금 방지)
taskkill /F /IM PokemonRed.exe 2>nul

if not exist build mkdir build

echo.
echo Compiling...
"%GXX%" -std=c++17 -B"%MGW_LIBEXEC%" -B"%MGW_TLIB%" src\main.cpp src\engine\renderer.cpp src\engine\input.cpp src\engine\audio.cpp src\game\game.cpp src\game\battle.cpp src\game\overworld.cpp -o build\PokemonRed.exe -lwinmm -I src -mconsole -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++
set BUILDRC=%errorlevel%

echo.
echo Compiler exit code: %BUILDRC%

if %BUILDRC% neq 0 (
    echo.
    echo [FAILED] Build failed
    pause
    exit /b 1
)

if not exist build\PokemonRed.exe (
    echo [FAILED] exe not produced
    pause
    exit /b 1
)

:: DLL 백업 복사 (정적 링크 실패 시 대비)
if exist "%MGW%\bin\libwinpthread-1.dll" copy /Y "%MGW%\bin\libwinpthread-1.dll" build\ >nul
if exist "%MGW%\bin\libgcc_s_seh-1.dll"  copy /Y "%MGW%\bin\libgcc_s_seh-1.dll"  build\ >nul
if exist "%MGW%\bin\libstdc++-6.dll"     copy /Y "%MGW%\bin\libstdc++-6.dll"     build\ >nul

echo.
echo [OK] Build succeeded
echo Running...
build\PokemonRed.exe
echo.
echo Game exit code: %errorlevel%
pause
