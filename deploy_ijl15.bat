@echo off
setlocal EnableExtensions
rem ============================================================
rem  Build Release ijl15.dll and overwrite BeiDou client DLL
rem  Target default: BeiDou-Client_1\ijl15.dll (see deploy_ijl15.ps1)
rem
rem  Usage:
rem    deploy_ijl15.bat              build + deploy (fail if locked)
rem    deploy_ijl15.bat /kill        kill BeiDou.exe then overwrite
rem    deploy_ijl15.bat /copyonly    copy existing out\Release only
rem    deploy_ijl15.bat /kill /copyonly
rem
rem  VS / MSBuild also auto-deploy via Post-Build:
rem    msbuild ezorsia.sln /p:Configuration=Release /p:Platform=x86
rem    msbuild ... /p:DeployKillClient=true
rem ============================================================

set "ROOT=%~dp0"
set "SLN=%ROOT%ezorsia.sln"
set "DLL=%ROOT%out\Release\ijl15.dll"
set "DLL_ALT=%ROOT%ezorsia\out\Release\ijl15.dll"
set "CONFIG=%ROOT%ezorsia\config.ini"
set "MSBUILD=D:\software\Microsoft\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe"

set "KILL="
set "COPYONLY="
for %%A in (%*) do (
  if /I "%%~A"=="/kill" set "KILL=-KillClient"
  if /I "%%~A"=="-kill" set "KILL=-KillClient"
  if /I "%%~A"=="/copyonly" set "COPYONLY=1"
  if /I "%%~A"=="-copyonly" set "COPYONLY=1"
)

if not defined COPYONLY (
  if not exist "%MSBUILD%" (
    echo [Deploy] ERROR: MSBuild not found: %MSBUILD%
    exit /b 1
  )
  echo [Deploy] MSBuild Release^|x86 ...
  "%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=x86 /m /v:minimal /p:DeploySkipPostBuild=true
  if errorlevel 1 (
    echo [Deploy] ERROR: build failed
    exit /b 1
  )
)

rem Prefer sln OutDir (out\Release). ezorsia\out alone was a stale-deploy trap.
if not exist "%DLL%" (
  if exist "%DLL_ALT%" (
    set "DLL=%DLL_ALT%"
    echo [Deploy] WARN: using fallback %DLL_ALT%
  ) else (
    echo [Deploy] ERROR: missing %ROOT%out\Release\ijl15.dll and ezorsia\out\Release\ijl15.dll
    exit /b 1
  )
) else (
  echo [Deploy] using %DLL%
)

rem ClientDir uses default inside deploy_ijl15.ps1 (BeiDou-Client_1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%deploy_ijl15.ps1" -DllPath "%DLL%" -ConfigPath "%CONFIG%" %KILL%
exit /b %ERRORLEVEL%