@echo off
cd /d "C:\Users\김지훈\Desktop\pockmonred"
C:\mingw64\bin\g++.exe -std=c++17 src/game/overworld.cpp -c -I src -DUNICODE -D_UNICODE 2>build\err.txt
echo ExitCode=%ERRORLEVEL%
type build\err.txt
