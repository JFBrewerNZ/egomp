@echo off
rem Copies freshly built binaries over the deployed ones in this folder.
rem Run AFTER closing the game and the dedicated server (both lock their files).
copy /Y Release\EgoMP.exe .
if errorlevel 1 goto failed
copy /Y Release\EgoMP.dll .
if errorlevel 1 goto failed
copy /Y Release\EgoMPServer.exe .
if errorlevel 1 goto failed
echo Updated OK.
goto done
:failed
echo FAILED - is the game or server still running?
:done
pause
