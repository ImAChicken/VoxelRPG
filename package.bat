@echo off
rem Builds everything, then makes the downloads in dist\:
rem   VoxelRPG-Client.zip  (give this to friends)
rem   VoxelRPG-Server.zip  (for whoever hosts)
cd /d "%~dp0"
call build.bat || exit /b 1
cmake -P tools\package.cmake || exit /b 1
echo.
echo The zips are in the dist folder.
