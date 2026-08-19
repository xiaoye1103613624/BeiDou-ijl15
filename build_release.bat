@echo off
setlocal EnableExtensions
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSB=%%I"
)
if not defined MSB (
  echo MSBUILD_NOT_FOUND
  exit /b 9
)
echo Using %MSB%
cd /d F:\MXD_dev\BeiDou-ijl15
"%MSB%" ezorsia\ezorsia.vcxproj /p:Configuration=Release /p:Platform=Win32 /m:1 /v:minimal
exit /b %ERRORLEVEL%
