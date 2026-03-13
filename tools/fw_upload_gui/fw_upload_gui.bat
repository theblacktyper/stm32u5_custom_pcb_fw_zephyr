@echo off
REM Launch Firmware Upload GUI without a console window
REM Tries project venv first, then system Python

if exist "%~dp0..\.venv\Scripts\pythonw.exe" (
    start "" "%~dp0..\.venv\Scripts\pythonw.exe" "%~dp0fw_upload_gui.py" %*
) else if exist "%~dp0.venv\Scripts\pythonw.exe" (
    start "" "%~dp0.venv\Scripts\pythonw.exe" "%~dp0fw_upload_gui.py" %*
) else (
    start "" pythonw "%~dp0fw_upload_gui.py" %*
)
