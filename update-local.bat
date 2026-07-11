@echo off
rem Copies freshly built binaries over the deployed ones in this folder.
rem Run AFTER closing the game and the dedicated server (both lock their files).
copy /Y ReleasegoMP.exe . && copy /Y ReleasegoMP.dll . && copy /Y ReleasegoMPServer.exe .
if errorlevel 1 (echo FAILED - is the game or server still running?) else echo Updated OK.
pause
