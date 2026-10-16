@echo off
rem Mysteries of the Sith: starts the game from this folder, as MoTS
cd /d "%~dp0"
start "" "%~dp0openjkdf2-64.exe" -motsCompat %*
