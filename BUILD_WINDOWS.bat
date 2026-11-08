@echo off
setlocal
cd /d "%~dp0"
where gcc >NUL 2>NUL || (echo MinGW-w64 GCC was not found. Install MinGW-w64 and add gcc to PATH.& exit /b 1)
where windres >NUL 2>NUL || (echo windres was not found. Install the MinGW-w64 binutils package.& exit /b 1)
if not exist build mkdir build
windres gui\app.rc -O coff -o build\gui-res.o
if errorlevel 1 exit /b 1
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic main.c src\student_core.c -o StudentManagement.exe
if errorlevel 1 exit /b 1
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -I src -mwindows gui\win32_main.c src\student_core.c build\gui-res.o -o StudentManagementGUI.exe -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -lole32
if errorlevel 1 exit /b 1
echo Built StudentManagement.exe and StudentManagementGUI.exe.
echo Double-click START_GUI_WINDOWS.bat to launch the graphical application.
endlocal
