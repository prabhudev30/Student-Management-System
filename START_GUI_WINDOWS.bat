@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0StudentManagementGUI.exe" (
  echo StudentManagementGUI.exe was not found.
  echo Extract the complete Windows ZIP into a writable folder and try again.
  pause
  exit /b 1
)
start "Student Management Studio" "%~dp0StudentManagementGUI.exe"
endlocal
