@echo off
setlocal
set "ROOT=E:\project\BeiDou-ijl15"
set "VCVARS=D:\software\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
set "MSBUILD=D:\software\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%VCVARS%" (
  echo [ERR] vcvars32 not found: %VCVARS%
  exit /b 1
)
if not exist "%MSBUILD%" (
  echo [ERR] MSBuild not found: %MSBUILD%
  exit /b 1
)
call "%VCVARS%"
cd /d "%ROOT%"
"%MSBUILD%" ezorsia.sln /t:Build /p:Configuration=Release /p:Platform=x86 /m:1 /p:MultiProcessorCompilation=false /v:minimal
set "EC=%ERRORLEVEL%"
echo EXIT=%EC%> "%ROOT%\build_exit.txt"
if %EC% neq 0 (
  echo [ERR] build failed EXIT=%EC%
  exit /b %EC%
)
echo [OK] built + PostBuild deployed to BeiDou-Client_S9
exit /b 0
