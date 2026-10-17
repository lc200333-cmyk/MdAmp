@echo off
setlocal
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "VSLANG=1033"
call "%~1" >nul
if errorlevel 1 exit /b 1
chcp 65001 >nul
if errorlevel 1 exit /b 1
"%~2" -S "%~dp0.." -B "%~dp0..\build\%~4" -G Ninja "-DCMAKE_MAKE_PROGRAM=%~3" -DCMAKE_BUILD_TYPE=%~4 -DBUILD_TESTING=ON
if errorlevel 1 exit /b 1
"%~2" --build "%~dp0..\build\%~4" --parallel
if errorlevel 1 exit /b 1
"%~dp2ctest.exe" --test-dir "%~dp0..\build\%~4" --output-on-failure
if errorlevel 1 exit /b 1
echo Executable: "%~dp0..\build\%~4\bin\MdAmp.exe"
exit /b 0
