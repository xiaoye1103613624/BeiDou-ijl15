@echo off
call "D:\software\Microsoft\VisualStudio\Community\VC\Auxiliary\Build\vcvars32.bat"
cd /d E:\pro\BeiDou-ijl15_S9
"D:\software\Microsoft\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe" ezorsia.sln /t:Build /p:Configuration=Release /p:Platform=x86 /m:1 /p:MultiProcessorCompilation=false /v:normal > E:\pro\BeiDou-ijl15_S9\build_full.log 2>&1
echo EXIT=%ERRORLEVEL%> E:\pro\BeiDou-ijl15_S9\build_exit.txt
