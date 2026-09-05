@echo off
rem SPDX-FileCopyrightText: 2026 shadPS4 Emulator Project
rem SPDX-License-Identifier: GPL-2.0-or-later

setlocal
set "SHADPS4_BLOODBORNE_RE_TRACE=1"

if not exist "%~dp0shadPS4.exe" (
    echo shadPS4.exe was not found next to this launcher.
    echo Keep this file inside the extracted Windows build folder.
    pause
    exit /b 1
)

start "" "%~dp0shadPS4.exe"
endlocal
