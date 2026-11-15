@echo off
setlocal
cd /d "%~dp0.."
call "D:\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
if not exist dist\CrashlandsTrainer-v1.3.1 mkdir dist\CrashlandsTrainer-v1.3.1
cl /nologo /O2 /MT /W0 /c /Fo:build\ vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde64.c
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /LD /Fo:build\trainer.obj src\native\trainer.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /link /OUT:dist\CrashlandsTrainer-v1.3.1\CrashlandsTrainer.dll /INCREMENTAL:NO
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /Fo:build\native-tests.obj tests\native-tests.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /link /OUT:build\native-tests.exe /INCREMENTAL:NO
if errorlevel 1 exit /b 1
build\native-tests.exe
if errorlevel 1 exit /b 1
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /platform:x64 /optimize+ /out:dist\CrashlandsTrainer-v1.3.1\CrashlandsTrainer.exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll src\Trainer.cs
if errorlevel 1 exit /b 1
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:exe /main:LauncherTests /platform:x64 /optimize+ /out:build\launcher-tests.exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll src\Trainer.cs tests\launcher-tests.cs
if errorlevel 1 exit /b 1
build\launcher-tests.exe
if errorlevel 1 exit /b 1
exit /b 0
