@echo off
cd /d "%~dp0app"
set VENV_DIR=venv

REM === Make logs dir
if not exist logs mkdir logs

REM === Set timestamped log file
for /f %%i in ('powershell -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set LOGFILE=logs\run_%%i.log

REM === Log start
> "%LOGFILE%" echo ===== %DATE% %TIME%: Start logging =====

REM === Check for venv
if not exist "%VENV_DIR%\Scripts\python.exe" (
    echo Creating venv...
    powershell -Command "& { python -m venv '%VENV_DIR%' *>> '%LOGFILE%' }"
)

REM === Install requirements.txt if present
if exist requirements.txt (
    echo Installing requirements...
    powershell -Command "& { & '%VENV_DIR%\Scripts\pip.exe' install --disable-pip-version-check -r 'requirements.txt' | Out-File -FilePath '%LOGFILE%' -Append -Encoding utf8 }"
)

REM === Run main.py
echo Launching main.py...
powershell -Command "& { & '%VENV_DIR%\Scripts\python.exe' 'main.py' *>> '%LOGFILE%' }"

REM === Log exit code
>> "%LOGFILE%" echo ===== %DATE% %TIME%: Process exited with errorlevel %ERRORLEVEL% =====
exit /b %ERRORLEVEL%
