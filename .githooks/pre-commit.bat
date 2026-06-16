@echo off
setlocal EnableExtensions

rem Pre-commit hook for d2bsng - checks code formatting

rem Get repo root
for /f "delims=" %%I in ('git rev-parse --show-toplevel') do set "REPO_ROOT=%%I"

rem Check if any C++ files are staged
git diff --cached --name-only --diff-filter=ACM | findstr /i "\.cpp$ \.h$" >nul 2>&1
if errorlevel 1 exit /b 0

echo Checking code formatting...

rem Run the formatting check via build.ps1
pushd "%REPO_ROOT%"
powershell -NoProfile -ExecutionPolicy Bypass -File "build.ps1" check-format >nul 2>&1
if errorlevel 1 (
    echo.
    echo ERROR: Some files need formatting.
    echo.
    echo Run 'powershell -File build.ps1 format' to fix formatting, then stage the changes.
    popd
    exit /b 1
)
popd

echo All staged files properly formatted.
exit /b 0
