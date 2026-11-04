@echo off
setlocal
where cmake >nul 2>nul || (echo CMake nao encontrado no PATH & exit /b 1)

set GEN=%1
if "%GEN%"=="" set GEN=MinGW Makefiles

cmake -S "%~dp0." -B "%~dp0build" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build "%~dp0build" --config Release -j 8
if errorlevel 1 exit /b 1
echo.
echo Saida: %~dp0build\PWLauncher.exe
