@echo off
rem Build everything. Usage: build.cmd [test]
rem   build.cmd       : engine (CMake/Ninja) + tools (dotnet)
rem   build.cmd test  : above + C++ tests, C# tests, WPF viewer selftest
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0build" || exit /b 1
rem vcvars sets Platform=x64, which breaks the dotnet solution configuration.
set Platform=
dotnet build "%~dp0tools\GunGameTools.slnx" || exit /b 1
if /i not "%1"=="test" exit /b 0
ctest --test-dir "%~dp0build" --output-on-failure || exit /b 1
dotnet test "%~dp0tools\GunGame.Interop.Tests" --no-build || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\selftest.ps1" || exit /b 1
