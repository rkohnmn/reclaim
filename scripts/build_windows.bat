@echo off
setlocal enabledelayedexpansion

echo === Building Reclaim v1.1.0 for Windows (x64 Release) ===

if not exist build\win64 mkdir build\win64

where cl >nul 2>&1
if %errorlevel%==0 (
    echo [Toolchain] Found MSVC cl.exe
    rc /nologo /fo build\win64\reclaim.res src\win32\reclaim.rc
    if errorlevel 1 exit /b 1

    cl /nologo /std:c++20 /O2 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
       /Iinclude ^
       src\core\*.cpp src\win32\win_main.cpp src\win32\main_window.cpp ^
       build\win64\reclaim.res ^
       /Fe:build\win64\Reclaim.exe ^
       /link /SUBSYSTEM:WINDOWS gdiplus.lib dwmapi.lib comctl32.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib user32.lib gdi32.lib
    if errorlevel 1 exit /b 1

    cl /nologo /std:c++20 /O2 /EHsc /W4 /utf-8 /Iinclude ^
       src\core\*.cpp src\cli\main_cli.cpp /Fe:build\win64\reclaim-cli.exe /link advapi32.lib
    if errorlevel 1 exit /b 1

    cl /nologo /std:c++20 /O2 /EHsc /W4 /utf-8 /Iinclude ^
       src\core\*.cpp tests\test_suite.cpp /Fe:build\win64\reclaim_tests.exe /link advapi32.lib
    if errorlevel 1 exit /b 1

    build\win64\reclaim_tests.exe
    echo [SUCCESS] Built build\win64\Reclaim.exe and build\win64\reclaim-cli.exe
    exit /b 0
)

where g++ >nul 2>&1
if %errorlevel%==0 (
    echo [Toolchain] Found MinGW-w64 g++
    windres src\win32\reclaim.rc -O coff -o build\win64\reclaim.res
    if errorlevel 1 exit /b 1

    g++ -std=c++20 -O2 -Wall -Wextra -municode -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN ^
        -Iinclude src\core\*.cpp src\win32\win_main.cpp src\win32\main_window.cpp build\win64\reclaim.res ^
        -o build\win64\Reclaim.exe -lgdiplus -ldwmapi -lcomctl32 -lshell32 -lole32 -lcomdlg32 -ladvapi32 -static-libgcc -static-libstdc++
    if errorlevel 1 exit /b 1

    g++ -std=c++20 -O2 -Wall -Wextra -Iinclude src\core\*.cpp src\cli\main_cli.cpp ^
        -o build\win64\reclaim-cli.exe -ladvapi32 -static-libgcc -static-libstdc++
    if errorlevel 1 exit /b 1

    echo [SUCCESS] Built build\win64\Reclaim.exe and build\win64\reclaim-cli.exe
    exit /b 0
)

echo [ERROR] Neither MSVC (cl.exe) nor MinGW-w64 (g++) was found in PATH.
exit /b 1
