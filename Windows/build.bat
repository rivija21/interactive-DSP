@echo off
rem Builds FilterLab.exe in this folder (the Windows counterpart of build.sh).
rem Needs CMake plus either MinGW-w64 (g++ and Ninja on PATH) or Visual Studio 2022+ with C++.
setlocal
cd /d "%~dp0"

where g++ >nul 2>nul
if %errorlevel%==0 (
  echo Compiling with MinGW-w64...
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_RC_COMPILER=windres || goto :fail
  cmake --build build || goto :fail
) else (
  echo Compiling with Visual Studio...
  cmake -S . -B build-msvc -A x64 || goto :fail
  cmake --build build-msvc --config Release || goto :fail
  if not exist build\bin mkdir build\bin
  copy /y build-msvc\bin\Release\FilterLab.exe build\bin\FilterLab.exe >nul
)

copy /y build\bin\FilterLab.exe FilterLab.exe >nul || goto :fail
echo Done: %cd%\FilterLab.exe
exit /b 0

:fail
echo Build failed.
exit /b 1
