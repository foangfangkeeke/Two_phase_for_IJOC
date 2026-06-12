@echo off
setlocal

set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%
set BUILD_DIR=%SCRIPT_DIR%\out\build\x64
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
cmake.exe -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%

cmake.exe --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b %errorlevel%

endlocal
