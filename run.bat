@echo off
rem Builds openday (Release) if any source changed since the last build, then starts it.
rem Extra arguments are passed to openday, e.g.  run.bat --no-intro --mute
rem
rem   OPENDAY_GAME_DIR  game installation (default: the GOG install path)
rem   OPENDAY_DATA_DIR  converted SQLite data (default: data\ in this folder;
rem                     created with tools\mdb\mdb2sqlite.py when missing)
setlocal
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "EXE=%BUILD%\app\Release\openday.exe"
set "STAMP=%BUILD%\openday.stamp"

if not defined OPENDAY_GAME_DIR set "OPENDAY_GAME_DIR=C:\Program Files (x86)\GOG Galaxy\Games\Fallen Haven Liberation Day"
if not defined OPENDAY_DATA_DIR set "OPENDAY_DATA_DIR=%ROOT%\data"

if not exist "%OPENDAY_GAME_DIR%\" (
    echo Game directory not found: "%OPENDAY_GAME_DIR%"
    echo Set OPENDAY_GAME_DIR to your installation.
    exit /b 1
)

rem --- Configure (first run only) ---
if not exist "%BUILD%\CMakeCache.txt" (
    echo Configuring...
    cmake -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 18 2026" -A x64 || exit /b 1
)

rem --- Build only when a source file changed since the last successful build ---
set "STALE=1"
if exist "%EXE%" if exist "%STAMP%" (
    powershell -NoProfile -Command ^
        "$stamp = (Get-Item -LiteralPath $env:STAMP).LastWriteTimeUtc;" ^
        "$files = @(Get-ChildItem -LiteralPath (Join-Path $env:ROOT 'CMakeLists.txt'));" ^
        "foreach ($d in 'app','engine','game','cmake') { $files += Get-ChildItem -LiteralPath (Join-Path $env:ROOT $d) -Recurse -File }" ^
        "if ($files | Where-Object { $_.LastWriteTimeUtc -gt $stamp } | Select-Object -First 1) { exit 1 } else { exit 0 }"
    if not errorlevel 1 set "STALE=0"
)
if "%STALE%"=="1" (
    echo Building...
    cmake --build "%BUILD%" --config Release --target openday || exit /b 1
    type nul > "%STAMP%"
) else (
    echo Up to date, skipping build.
)

rem --- Convert the game databases (first run only) ---
if not exist "%OPENDAY_DATA_DIR%\GLOBALS\" (
    echo Converting game databases to "%OPENDAY_DATA_DIR%"
    python "%ROOT%\tools\mdb\mdb2sqlite.py" --game-dir "%OPENDAY_GAME_DIR%" "%OPENDAY_DATA_DIR%" || exit /b 1
)

"%EXE%" --game-dir "%OPENDAY_GAME_DIR%" --data "%OPENDAY_DATA_DIR%" %*
