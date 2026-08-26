@echo off
call "D:\software\Microsoft\VisualStudio\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d F:\MXD_dev\BeiDou-ijl15\_gg_stub
cl /nologo /LD nmcogame_stub.cpp /Fe:nmcogame_stub.dll
