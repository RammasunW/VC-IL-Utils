@echo off
setlocal
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in ("%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /utf-8 /EHsc /O2 /MT /LD Main.cpp /link /MACHINE:X86 user32.lib comctl32.lib gdi32.lib /OUT:VC-IL-Utils.asi
pause
