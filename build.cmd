@echo off
rem Build everything. Usage: build.cmd [test]
rem   build.cmd       : engine (CMake + MSBuild) + tools (dotnet/MSBuild)
rem   build.cmd test  : above + C++ tests, C# tests, editor UI input tests, WPF viewer selftest
rem Needs cmake and a Visual Studio or Build Tools install with the C++ workload. No vcvars/hardcoded path:
rem CMake finds the newest installed Visual Studio itself and builds with its MSBuild.
rem If the build dir was created with another generator, delete build\ first.
cmake -S "%~dp0." -B "%~dp0build" -A x64 || exit /b 1
cmake --build "%~dp0build" --config Release || exit /b 1
dotnet build "%~dp0tools\GunGameTools.slnx" || exit /b 1
if /i not "%1"=="test" exit /b 0
ctest --test-dir "%~dp0build" -C Release --output-on-failure || exit /b 1
dotnet test "%~dp0tools\GunGame.Interop.Tests" --no-build || exit /b 1
dotnet test "%~dp0tools\GunGameEditor.UiTests" --no-build || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\selftest.ps1" || exit /b 1
