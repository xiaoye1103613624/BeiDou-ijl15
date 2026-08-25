@echo off
setlocal EnableExtensions
set "LIVE=F:\MXD_dev\BeiDou-Client\Data\UI\UIWindow.img"
set "BAK=F:\MXD_dev\BeiDou-Client\Data\UI\UIWindow.img.bak"
if not exist "%BAK%" (
  echo Missing merged bak: %BAK%
  echo Run merge_tabbuy4.py first.
  exit /b 1
)
copy /Y "%BAK%" "%LIVE%" >nul
if errorlevel 1 (
  echo Failed to write live UIWindow.img. Close BeiDou.exe and retry.
  exit /b 1
)
echo OK: TabBuy/4 merged into %LIVE%
exit /b 0
