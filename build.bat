@echo off
rem Use utf-8 code page.
chcp 65001

set exe_name=SleepyLid.exe

if "%~2"=="" (set subsystem=WINDOWS) else (set subsystem=%2)
if not %subsystem%==WINDOWS if not %subsystem%==CONSOLE (
    echo Unknown subsystem "%subsystem%"
    exit /b 1
)

if "%~1"=="" (set build=release) else (set build=%1)
if %build%==debug (
    set cl_flags=/Zi /Zi /MTd /Od
) else (
    if %build%==release (
        set cl_flags=/MT /O2
    ) else (
        echo Unknown build "%build%"
        exit /b 1
    )
)
set build_dir=build\\%build%
set cl_flags=%cl_flags% /EHsc /D %subsystem% /Fe:"%build_dir%\\%exe_name%" /Fo"%build_dir%\\" /Fd"%build_dir%\\"
set link_flags=/SUBSYSTEM:%subsystem%

if not exist %build_dir% mkdir %build_dir%

rem compile resources.
rc /fo %build_dir%\\res.res res.rc
if errorlevel 1 exit

cl /nologo /utf-8 %cl_flags% /D WINVER=0x0A00 /D _WIN32_WINNT=0x0A00  /D WIN32_LEAN_AND_MEAN *.cpp /link %link_flags% %build_dir%\\res.res User32.lib Shell32.lib Comctl32.lib Advapi32.lib PowrProf.lib