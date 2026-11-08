@echo off
setlocal
cd /d "%~dp0"

if not exist "StudentManagement.exe" (
  echo StudentManagement.exe was not found in this folder.
  echo Extract all files from the ZIP first, then run this file again.
  pause
  exit /b 1
)

echo Starting Student Management System...
echo Keep this window open while using the program.
echo.
StudentManagement.exe

echo.
echo Program closed.
pause
endlocal
