@echo off
rem Builds the game, the server and the tests (Release). Run from the repo root.
cd /d "%~dp0"
cmake -B build -DVOXELRPG_TESTS=ON || goto :fail
cmake --build build --config Release || goto :fail
echo.
echo Built: build\bin\Release\VoxelRPG.exe and VoxelRPG-Server.exe
exit /b 0
:fail
echo.
echo BUILD FAILED - see the first error above.
exit /b 1
