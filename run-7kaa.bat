@echo off
rem Launch the locally built game with MSYS2's MinGW runtime DLLs on PATH.
cd /d "%~dp0"
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "SKDATA=%~dp0data"
rem Log what the AI kingdoms decide and why (see AiWarState::log in src/OAI_WAR.cpp)
set "SKAI_LOG=%~dp0ai-log.txt"
start "" "%~dp0src\7kaa.exe"
