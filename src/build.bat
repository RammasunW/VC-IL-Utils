@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined QIANCHENG_VS_PATH set "VSPATH=%QIANCHENG_VS_PATH%"
if not defined VSPATH exit /b 1
rem Optional first argument selects a toolset, for example: build.bat 14.44
if "%~1"=="" goto default_toolset
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=%~1
goto toolset_ready
:default_toolset
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat"
:toolset_ready
where cl >nul 2>nul
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0.."
if not exist Release\qiancheng\EN mkdir Release\qiancheng\EN
if not exist Release\qiancheng\CN mkdir Release\qiancheng\CN
cl /nologo /utf-8 /EHsc /W4 /O2 /MT /LD src\Main_EN.cpp /Fo:Release\qiancheng\EN\Main.obj /link /MACHINE:X86 user32.lib comctl32.lib gdi32.lib /OUT:Release\qiancheng\EN\VC-IL-Utils.asi
if errorlevel 1 exit /b %errorlevel%
cl /nologo /source-charset:.936 /execution-charset:.936 /EHsc /W4 /O2 /MT /LD src\Main_CN.cpp /Fo:Release\qiancheng\CN\Main.obj /link /MACHINE:X86 user32.lib comctl32.lib gdi32.lib /OUT:Release\qiancheng\CN\VC-IL-Utils.asi
if errorlevel 1 exit /b %errorlevel%
copy /y EN\ILUtilsKeys.ini Release\qiancheng\EN\ILUtilsKeys.ini >nul
copy /y CN\ILUtilsKeys.ini Release\qiancheng\CN\ILUtilsKeys.ini >nul
popd
