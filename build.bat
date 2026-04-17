@echo off
cd /d "%~dp0"

echo ============================================
echo  PokemonRed - Build Script
echo ============================================

:: 컴파일러 탐색 순서: mingw64(GCC 15) 우선
set GXX=
if exist "C:\mingw64\bin\g++.exe"  set GXX=C:\mingw64\bin\g++.exe
if not defined GXX if exist "C:\msys64\mingw64\bin\g++.exe" set GXX=C:\msys64\mingw64\bin\g++.exe
if not defined GXX if exist "C:\msys2\mingw64\bin\g++.exe"  set GXX=C:\msys2\mingw64\bin\g++.exe

:: 구형 MinGW(GCC 6.x)는 C++17 inline 변수 미지원 → 빌드 불가
:: C:\MinGW 는 의도적으로 사용하지 않습니다.

if not defined GXX (
    echo.
    echo [ERROR] C++17을 지원하는 g++를 찾지 못했습니다.
    echo  - MinGW-w64 설치 필요: https://winlibs.com 에서 GCC 14+ Win64 다운로드
    echo  - 압축 해제 후 C:\mingw64 에 놓으면 됩니다.
    echo.
    pause
    exit /b 1
)

echo [INFO] 컴파일러: %GXX%
for /f "tokens=*" %%v in ('"%GXX%" --version 2^>^&1 ^| findstr /i "g++"') do echo [INFO] 버전: %%v

if not exist build mkdir build

echo.
echo Building...
"%GXX%" -std=c++17 ^
  src/main.cpp ^
  src/engine/renderer.cpp ^
  src/engine/input.cpp ^
  src/engine/audio.cpp ^
  src/game/game.cpp ^
  src/game/battle.cpp ^
  src/game/overworld.cpp ^
  -o build/PokemonRed.exe ^
  -lwinmm -I src -mconsole -DUNICODE -D_UNICODE ^
  -static -static-libgcc -static-libstdc++

if %errorlevel% neq 0 (
    echo.
    echo [FAILED] 빌드 실패
    pause
    exit /b 1
)

echo.
echo [OK] 빌드 성공 - 실행 중...
build\PokemonRed.exe
