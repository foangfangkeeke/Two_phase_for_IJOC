@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%

cd /d "%~dp0"
cmake.exe -S . -B out/build/x64-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCOPT_HOME="C:/Program Files/copt80"
if errorlevel 1 exit /b %errorlevel%

cmake.exe --build out/build/x64-ninja --config Release
exit /b %errorlevel%
