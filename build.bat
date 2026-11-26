@echo off
setlocal
cd /d "%~dp0"

where cl >nul 2>nul && goto msvc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto mingw
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto mingw
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || goto mingw

:msvc
rc /nologo /fo app.res app.rc || exit /b 1
cl /nologo /std:c++17 /O2 /utf-8 /EHsc /MT /DNDEBUG calc.cpp app.res /Fe:Calculator.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib d2d1.lib dwrite.lib dwmapi.lib || exit /b 1
del /q calc.obj app.res 2>nul
goto done

:mingw
where g++ >nul 2>nul || goto missing
windres app.rc -O coff -o app.res || exit /b 1
g++ -std=c++17 -O2 -s -static -mwindows -ffunction-sections -fdata-sections -Wl,--gc-sections calc.cpp app.res -o Calculator.exe -ld2d1 -ldwrite -ldwmapi || exit /b 1
del /q app.res 2>nul
goto done

:missing
echo C++ compiler not found. Install Visual Studio Build Tools (Desktop development with C++) or MinGW-w64.
exit /b 1

:done
echo Built: %~dp0Calculator.exe
