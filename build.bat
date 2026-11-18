@echo off
rem Builds the game and the server (Release). Run from the repo root.
rem   build.bat          game + server only (fast)
rem   build.bat tests    also every headless test program in tests\
cd /d "%~dp0"
cmake -B build -DVOXELRPG_TESTS=ON || goto :fail
if /i "%~1"=="tests" (
    cmake --build build --config Release || goto :fail
) else (
    cmake --build build --config Release --target VoxelRPG VoxelRPG-Server || goto :fail
)
echo.
echo Built: build\bin\Release\VoxelRPG.exe and VoxelRPG-Server.exe
if /i "%~1"=="tests" echo Tests: build\bin\Release\*_test.exe (run them from the repo root)
exit /b 0
:fail
echo.
echo BUILD FAILED - see the first error above.
exit /b 1
