@echo off
cd /d "%~dp0"
title Helios
echo Helios запускается...
echo Папка: %CD%
echo.
helios.exe --hardware %*
set CODE=%ERRORLEVEL%
echo.
echo Код выхода: %CODE%
if not "%CODE%"=="0" (
  echo Смотри helios.log в этой папке.
  pause
)
