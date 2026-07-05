@echo off
setlocal

set "SRC=%~dp0"
set "DST=%CD%"

if not exist "%DST%\CMakeLists.txt" (
  echo [ERROR] This command must be run from the D3D12Helper repository root.
  echo [ERROR] CMakeLists.txt was not found in: %DST%
  exit /b 1
)

copy /Y "%SRC%.gitignore" "%DST%\.gitignore" >nul

if errorlevel 1 (
  echo [ERROR] Failed to copy .gitignore.
  exit /b 1
)

echo [OK] .gitignore added/updated for D3D12Helper.
echo You can now run: git add .
endlocal
