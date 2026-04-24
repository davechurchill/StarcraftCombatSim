@echo off
setlocal

if "%SFML_DIR%"=="" (
  set SFML_DIR=C:\dev\libraries\SFML-3.0.1
)

cmake -S . -B build -DSFML_DIR="%SFML_DIR%\lib\cmake\SFML"
if errorlevel 1 exit /b %errorlevel%

cmake --build build --config Release --target StarCraftCombatVisualizer --parallel 8
if errorlevel 1 exit /b %errorlevel%

endlocal
