@echo off
echo Building SPL interpreter...
gcc -std=c99 -Wall -Wextra -o spl.exe spl.c
if %errorlevel% neq 0 (
    echo Build FAILED.
    exit /b 1
)
echo Build OK.
if "%~1"=="" (
    echo Usage: spl.bat script.spl
) else (
    spl.exe %1
)
