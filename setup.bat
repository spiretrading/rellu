@ECHO OFF
SETLOCAL EnableDelayedExpansion
FOR /F "delims==" %%V IN ('SET DEPENDENCIES[ 2^>NUL') DO (
  SET "%%V="
)
SET "NEXT_DEPENDENCY_INDEX=0"
SET "ROOT=%cd%"
SET "CACHE_DIRECTORY=!ROOT!\cache_files\gesso"
IF NOT EXIST "!CACHE_DIRECTORY!" (
  MD "!CACHE_DIRECTORY!" || EXIT /B 1
)
CALL :SetupVSEnvironment || EXIT /B 1
CALL :AddDependency "doctest-2.4.12" ^
  "https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.zip" ^
  "7a7afb5f70d0b749d49ddfcb8a454299a8fcd53e9db9c131abe99b456e88a1fe" 1
SET "QT_URL=https://download.qt.io/official_releases/qt/6.12/6.12.0"
CALL :AddDependency "qtbase-6.12.0" ^
  "!QT_URL!/submodules/qtbase-everywhere-src-6.12.0.zip" ^
  "0529901575c413e34ded504eeda177c4a6dfd0dff1e39ed52c811dc5204b43c7" 1 ^
  ":BuildQt"
CALL :InstallDependencies || EXIT /B 1
EXIT /B 0
ENDLOCAL

:BuildQt
FOR %%D IN (install debug_cmake) DO (
  IF EXIST %%D (
    RD /S /Q %%D || EXIT /B 1
  )
)
CALL :BuildQtConfiguration debug Debug || EXIT /B 1
ROBOCOPY install\lib\cmake debug_cmake *-debug.cmake /S >NUL
IF ERRORLEVEL 8 EXIT /B 1
CALL :BuildQtConfiguration release Release || EXIT /B 1
ROBOCOPY debug_cmake install\lib\cmake /S >NUL
IF ERRORLEVEL 8 EXIT /B 1
RD /S /Q debug_cmake || EXIT /B 1
EXIT /B 0

:BuildQtConfiguration
IF EXIST build (
  RD /S /Q build || EXIT /B 1
)
MD build || EXIT /B 1
PUSHD build || EXIT /B 1
CALL ..\configure.bat -prefix "!ROOT!\!FOLDER!\install" -static -%~1 ^
  -cmake-generator "Visual Studio 18 2026" -opensource -confirm-license ^
  -nomake examples -nomake tests -no-icu -qt-freetype -qt-harfbuzz ^
  -qt-libjpeg -qt-libpng -qt-pcre -qt-zlib || (
  POPD
  EXIT /B 1
)
SETLOCAL
SET "CL=/MP"
cmake --build . --config %~2 --parallel || (ENDLOCAL & POPD & EXIT /B 1)
ENDLOCAL
cmake --install . --config %~2 || (POPD & EXIT /B 1)
POPD
RD /S /Q build
EXIT /B !ERRORLEVEL!

:SetupVSEnvironment
SET "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
SET "VS_FOUND="
FOR /F "usebackq delims=" %%i IN (` ^
    "!VSWHERE!" -prerelease -latest -products * ^
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
      -property installationPath`) DO (
  IF EXIST "%%i\Common7\Tools\vsdevcmd.bat" (
    CALL "%%i\Common7\Tools\vsdevcmd.bat" -no_logo -arch=x64 -host_arch=x64 || (
      EXIT /B 1
    )
    SET "VS_FOUND=1"
  )
)
IF NOT DEFINED VS_FOUND (
  ECHO Error: Visual Studio C++ build tools were not found.
  EXIT /B 1
)
EXIT /B 0

:AddDependency
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].NAME=%~1"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].URL=%~2"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].HASH=%~3"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].REVISION=%~4"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].BUILD=%~5"
SET /A NEXT_DEPENDENCY_INDEX+=1
EXIT /B 0

:InstallDependencies
SET "I=0"
:InstallDependenciesLoop
IF NOT DEFINED DEPENDENCIES[%I%].NAME EXIT /B 0
CALL :DownloadAndExtract "!DEPENDENCIES[%I%].NAME!" "!DEPENDENCIES[%I%].URL!" ^
  "!DEPENDENCIES[%I%].HASH!" "!DEPENDENCIES[%I%].REVISION!" ^
  "!DEPENDENCIES[%I%].BUILD!" || EXIT /B 1
SET /A I+=1
GOTO InstallDependenciesLoop

:DownloadAndExtract
SET "FOLDER=%~1"
SET "BUILD_MARKER=!CACHE_DIRECTORY!\!FOLDER!.build_complete"
SET "URL=%~2"
SET "EXPECTED_HASH=%~3"
SET "BUILD_HASH=!EXPECTED_HASH! windows-%~4"
SET "BUILD_LABEL=%~5"
SET "ACTUAL_HASH="
FOR /F "tokens=* delims=/" %%A IN ("!URL!") DO (
  SET "ARCHIVE=%%~nxA"
)
SET "CACHED_HASH="
IF EXIST "!BUILD_MARKER!" (
  SET /P CACHED_HASH=<"!BUILD_MARKER!"
  IF EXIST "!FOLDER!\" (
    IF "!CACHED_HASH!"=="!BUILD_HASH!" EXIT /B 0
  )
  DEL /F /Q "!BUILD_MARKER!"
  IF EXIST "!BUILD_MARKER!" EXIT /B 1
)
IF EXIST "!FOLDER!\.gesso_extract_complete" (
  SET /P CACHED_HASH=<"!FOLDER!\.gesso_extract_complete"
  IF "!CACHED_HASH!"=="!EXPECTED_HASH!" GOTO BuildDependency
  DEL /F /Q "!FOLDER!\.gesso_extract_complete"
  IF EXIST "!FOLDER!\.gesso_extract_complete" EXIT /B 1
)
IF NOT EXIST "!ARCHIVE!" (
  curl -fsL -o "!ARCHIVE!" "!URL!" || (
    IF EXIST "!ARCHIVE!" DEL /F /Q "!ARCHIVE!"
    EXIT /B 1
  )
)
FOR /F "skip=1 tokens=*" %%H IN ('certutil -hashfile "!ARCHIVE!" SHA256') DO (
  IF NOT DEFINED ACTUAL_HASH SET "ACTUAL_HASH=%%H"
)
SET "ACTUAL_HASH=!ACTUAL_HASH: =!"
IF /I NOT "!ACTUAL_HASH!"=="!EXPECTED_HASH!" (
  ECHO Error: SHA256 mismatch for !ARCHIVE!.
  ECHO   Expected: !EXPECTED_HASH!
  ECHO   Actual:   !ACTUAL_HASH!
  DEL /F /Q "!ARCHIVE!"
  SET "ACTUAL_HASH="
  EXIT /B 1
)
SET "ACTUAL_HASH="
IF NOT EXIST "!FOLDER!" (
  MD "!FOLDER!" || EXIT /B 1
)
cmake -DARCHIVE:FILEPATH="!ARCHIVE!" -DDESTINATION:PATH="!FOLDER!" ^
  -DSTRIP_COMPONENTS=1 -P "%~dp0Config\extract.cmake" || EXIT /B 1
(ECHO !EXPECTED_HASH!) >"!FOLDER!\.gesso_extract_complete" || EXIT /B 1
:BuildDependency
IF DEFINED BUILD_LABEL (
  PUSHD "!FOLDER!" || EXIT /B 1
  CALL !BUILD_LABEL!
  SET "BUILD_RESULT=!ERRORLEVEL!"
  POPD
  IF NOT "!BUILD_RESULT!"=="0" EXIT /B !BUILD_RESULT!
)
(ECHO !BUILD_HASH!) >"!BUILD_MARKER!" || EXIT /B 1
IF EXIST "!ARCHIVE!" DEL /F /Q "!ARCHIVE!"
EXIT /B 0
