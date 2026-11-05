@echo off
setlocal
set "VCLOCAL_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VCLOCAL_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCLOCAL_VSPATH=%%i"
if defined QIANCHENG_VS_PATH set "VCLOCAL_VSPATH=%QIANCHENG_VS_PATH%"
if not defined VCLOCAL_VSPATH exit /b 1
if "%~1"=="" goto default_toolset
call "%VCLOCAL_VSPATH%\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=%~1
goto ready
:default_toolset
call "%VCLOCAL_VSPATH%\VC\Auxiliary\Build\vcvars32.bat"
:ready
where cl >nul 2>nul
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0..\.."
if not exist Release\local-userfiles mkdir Release\local-userfiles
cl /nologo /utf-8 /EHsc /W4 /O2 /MT /LD tools\local-userfiles\Main.cpp /Fo:Release\local-userfiles\Main.obj /link /MACHINE:X86 user32.lib /OUT:Release\local-userfiles\zz_VC-LocalUserFiles.asi
if errorlevel 1 exit /b %errorlevel%
cl /nologo /utf-8 /EHsc /W4 /O2 /MT /LD tools\local-userfiles\EarlyLoader.cpp /Fo:Release\local-userfiles\EarlyLoader.obj /link /MACHINE:X86 user32.lib /DEF:tools\local-userfiles\dinput8.def /IMPLIB:Release\local-userfiles\dinput8.lib /OUT:Release\local-userfiles\dinput8.dll
if errorlevel 1 exit /b %errorlevel%
cl /nologo /utf-8 /EHsc /W4 /O2 /MT tools\local-userfiles\tests.cpp /Fo:Release\local-userfiles\tests.obj /Fe:Release\local-userfiles\tests.exe /link /MACHINE:X86 user32.lib
if errorlevel 1 exit /b %errorlevel%
Release\local-userfiles\tests.exe
if errorlevel 1 exit /b %errorlevel%
copy /y tools\local-userfiles\README.md Release\local-userfiles\README.md >nul
popd
