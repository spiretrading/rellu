@ECHO OFF
SETLOCAL EnableDelayedExpansion
FOR /F "delims==" %%V IN ('SET DEPENDENCIES[ 2^>NUL') DO (
  SET "%%V="
)
SET "NEXT_DEPENDENCY_INDEX=0"
SET "ROOT=%cd%"
SET "CACHE_DIRECTORY=!ROOT!\cache_files\rellu"
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
CALL :AddDependency "qtshadertools-6.12.0" ^
  "!QT_URL!/submodules/qtshadertools-everywhere-src-6.12.0.zip" ^
  "d3dbfd833769bb9f341e39352247c3910f8c77f5e64d34b10f65bb40d8da4ce6" 1 ^
  ":BuildQtShaderTools"
SET "CLIPPER_URL=https://github.com/AngusJohnson/Clipper2/archive/refs/tags"
CALL :AddDependency "Clipper2-2.0.1" ^
  "!CLIPPER_URL!/Clipper2_2.0.1.zip" ^
  "2fbfe548aa1427716510b572446bcec34c88990df7da510039cbdc2ee2406a23" 1 ^
  ":BuildClipper2"
SET "ROBOTO_URL=https://github.com/googlefonts/roboto-2/releases"
CALL :AddDependency "roboto-2.136" ^
  "!ROBOTO_URL!/download/v2.136/roboto-hinted.zip" ^
  "c4528791de55ade3d6c698738a70b457311e4dd296c5a3318aa729874067fa6a" 1
CALL :AddFiles "opensans-bd7e376" "googlefonts/opensans" ^
  "bd7e37632246368c60fdcbd374dbf9bad11969b6" "fonts/noto-set/ttf" 1
CALL :AddFile "OpenSans-Bold.ttf" ^
  "b9652382e4d6f16b5cc0b9e44809e520c8965f97aad2a61cce103ebe1be61ad5"
CALL :AddFile "OpenSans-BoldItalic.ttf" ^
  "6be38771f2ba835b1515bb6a3cce56900275760a1b4768ed075d049721d964f0"
CALL :AddFile "OpenSans-CondensedBold.ttf" ^
  "28ea9deff91ec78fe7d3c66a4ee702af156b9ad327b0827e6043f94772aab21c"
CALL :AddFile "OpenSans-CondensedBoldItalic.ttf" ^
  "9b28853d915567ce5d8cf13f1433c617c135bc4ab388f401123c25b9e7d12702"
CALL :AddFile "OpenSans-CondensedExtraBold.ttf" ^
  "eb07aed2b8c816e1bf765b526e3b6b4c3c1e8e8dcc2735cf6bb209bf2e935333"
CALL :AddFile "OpenSans-CondensedExtraBoldItalic.ttf" ^
  "72d66673d80a5b7ddf346b463c4e8f74541d305fb1b64607f0a21381ace8cd34"
CALL :AddFile "OpenSans-CondensedItalic.ttf" ^
  "0cbe24043f25200ef026c8636a7a9f3f6f53c4b0e29ddc536f3845c9c97d89f5"
CALL :AddFile "OpenSans-CondensedLight.ttf" ^
  "1bac1fb62720b1d388d676369bc3389b60cbcfce677396981a7c98a4cd4428fc"
CALL :AddFile "OpenSans-CondensedLightItalic.ttf" ^
  "4d3ba6580e6727b4d41aaef8c91bc020c0a41f4113bbd32869a5dbfcf24b9b40"
CALL :AddFile "OpenSans-CondensedRegular.ttf" ^
  "7fd8ec690d06b7504e1803163f4ba1e4eea08182af50deb8872b26597075ffee"
CALL :AddFile "OpenSans-CondensedSemiBold.ttf" ^
  "7013d0d09a3bcffbc96883d7ec1170577308eaf9fc1c6c44d23801d56e34e93e"
CALL :AddFile "OpenSans-CondensedSemiBoldItalic.ttf" ^
  "701d5076f8c949849a671adc536491287df62ee97bae1e091e547122ceeaab61"
CALL :AddFile "OpenSans-ExtraBold.ttf" ^
  "58131e8ded443333fdeda104384b1c637863636c0517f4af5891965f2f39e05c"
CALL :AddFile "OpenSans-ExtraBoldItalic.ttf" ^
  "c265dd757e3e40c40f55a5d377fe5a74ac4b15eda66db56bbbfd1be7df34eac1"
CALL :AddFile "OpenSans-Italic.ttf" ^
  "b9980136238347f81f9865bcee65d52ca432b7e05fd479af1484983988432def"
CALL :AddFile "OpenSans-Light.ttf" ^
  "493c5e9f31c425745da143dcc1b3513e9d36ea3c444d45dda6ca6f5eab86fff4"
CALL :AddFile "OpenSans-LightItalic.ttf" ^
  "f441a8b24bcab020f040ed741371205246a6375fa5531ad67debab3f9daaa33f"
CALL :AddFile "OpenSans-Regular.ttf" ^
  "ce57ff2cbd11a2ce997256c3ec5d7c9e8a8199141bd7aaceda77f37fbecd71b3"
CALL :AddFile "OpenSans-SemiBold.ttf" ^
  "ce59e775d558eefea4c8b01365acc14806cf7d83e18aff65ca1f9d3de4655d30"
CALL :AddFile "OpenSans-SemiBoldItalic.ttf" ^
  "b5c47cd18612a43a2059a5d854fd4c04e08b68bc95fa9a6d27287c7857a389a4"
CALL :InstallDependencies || EXIT /B 1
CALL :InstallHarfBuzzHeaders "qtbase-6.12.0" || EXIT /B 1
EXIT /B 0
ENDLOCAL

:InstallHarfBuzzHeaders
SET "QT_FOLDER=%~1"
SET "HARFBUZZ_DIRECTORY=!ROOT!\!QT_FOLDER!\install\include\harfbuzz"
SET "HARFBUZZ_MARKER=!CACHE_DIRECTORY!\harfbuzz-headers.build_complete"
SET "QT_BUILD_HASH="
SET /P QT_BUILD_HASH=<"!CACHE_DIRECTORY!\!QT_FOLDER!.build_complete"
IF NOT DEFINED QT_BUILD_HASH EXIT /B 1
SET "CACHED_HASH="
IF EXIST "!HARFBUZZ_MARKER!" (
  SET /P CACHED_HASH=<"!HARFBUZZ_MARKER!"
  IF EXIST "!HARFBUZZ_DIRECTORY!\" (
    IF "!CACHED_HASH!"=="!QT_BUILD_HASH!" EXIT /B 0
  )
  DEL /F /Q "!HARFBUZZ_MARKER!"
  IF EXIST "!HARFBUZZ_MARKER!" EXIT /B 1
)
IF EXIST "!HARFBUZZ_DIRECTORY!" (
  RD /S /Q "!HARFBUZZ_DIRECTORY!" || EXIT /B 1
)
MD "!HARFBUZZ_DIRECTORY!" || EXIT /B 1
COPY /Y "!ROOT!\!QT_FOLDER!\src\3rdparty\harfbuzz-ng\src\*.h" ^
  "!HARFBUZZ_DIRECTORY!" >NUL || EXIT /B 1
(ECHO !QT_BUILD_HASH!) >"!HARFBUZZ_MARKER!" || EXIT /B 1
EXIT /B 0

:BuildClipper2
FOR %%D IN (build install) DO (
  IF EXIST %%D (
    RD /S /Q %%D || EXIT /B 1
  )
)
cmake -S CPP -B build -DCLIPPER2_UTILS=OFF -DCLIPPER2_EXAMPLES=OFF ^
  -DCLIPPER2_TESTS=OFF -DCLIPPER2_USINGZ=OFF -DCMAKE_DEBUG_POSTFIX=d ^
  -DCMAKE_INSTALL_PREFIX="!ROOT!\!FOLDER!\install" || EXIT /B 1
FOR %%C IN (Debug Release) DO (
  cmake --build build --config %%C --parallel || EXIT /B 1
  cmake --install build --config %%C || EXIT /B 1
)
RD /S /Q build
EXIT /B !ERRORLEVEL!

:BuildQtShaderTools
IF EXIST build (
  RD /S /Q build || EXIT /B 1
)
MD build || EXIT /B 1
PUSHD build || EXIT /B 1
CALL "!ROOT!\qtbase-6.12.0\install\bin\qt-configure-module.bat" .. ^
  -cmake-generator "Visual Studio 18 2026" || (
  POPD
  EXIT /B 1
)
SETLOCAL
SET "CL=/MP"
cmake --build . --config Release --parallel || (ENDLOCAL & POPD & EXIT /B 1)
ENDLOCAL
cmake --install . --config Release || (POPD & EXIT /B 1)
POPD
RD /S /Q build
EXIT /B !ERRORLEVEL!

:BuildQt
DEL /F /Q "!CACHE_DIRECTORY!\qtshadertools-6.12.0.build_complete" 2>NUL
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

:AddFiles
SET "FILES_URL=https://raw.githubusercontent.com/%~2/%~3/%~4"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].NAME=%~1"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].URL=!FILES_URL!"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].REVISION=%~5"
SET "DEPENDENCIES[%NEXT_DEPENDENCY_INDEX%].FILES=0"
SET /A NEXT_DEPENDENCY_INDEX+=1
EXIT /B 0

:AddFile
SET /A "INDEX=NEXT_DEPENDENCY_INDEX-1"
SET "FILE_INDEX=!DEPENDENCIES[%INDEX%].FILES!"
SET "DEPENDENCIES[%INDEX%].FILES[%FILE_INDEX%].NAME=%~1"
SET "DEPENDENCIES[%INDEX%].FILES[%FILE_INDEX%].HASH=%~2"
SET /A FILE_INDEX+=1
SET "DEPENDENCIES[%INDEX%].FILES=!FILE_INDEX!"
EXIT /B 0

:InstallDependencies
SET "I=0"
:InstallDependenciesLoop
IF NOT DEFINED DEPENDENCIES[%I%].NAME EXIT /B 0
IF DEFINED DEPENDENCIES[%I%].FILES (
  CALL :DownloadFiles %I% || EXIT /B 1
  SET /A I+=1
  GOTO InstallDependenciesLoop
)
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
IF EXIST "!FOLDER!\.rellu_extract_complete" (
  SET /P CACHED_HASH=<"!FOLDER!\.rellu_extract_complete"
  IF "!CACHED_HASH!"=="!EXPECTED_HASH!" GOTO BuildDependency
  DEL /F /Q "!FOLDER!\.rellu_extract_complete"
  IF EXIST "!FOLDER!\.rellu_extract_complete" EXIT /B 1
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
(ECHO !EXPECTED_HASH!) >"!FOLDER!\.rellu_extract_complete" || EXIT /B 1
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

:DownloadFiles
SET "FOLDER=!DEPENDENCIES[%~1].NAME!"
SET "FILES_URL=!DEPENDENCIES[%~1].URL!"
SET "FILE_COUNT=!DEPENDENCIES[%~1].FILES!"
SET "BUILD_MARKER=!CACHE_DIRECTORY!\!FOLDER!.build_complete"
SET "MANIFEST=!CACHE_DIRECTORY!\!FOLDER!.manifest"
(ECHO !FILES_URL!) >"!MANIFEST!" || EXIT /B 1
SET "J=0"
:WriteManifestLoop
IF !J! GEQ !FILE_COUNT! GOTO WriteManifestDone
CALL :GetFile %~1 !J!
(ECHO !FILE_NAME! !FILE_HASH!) >>"!MANIFEST!" || EXIT /B 1
SET /A J+=1
GOTO WriteManifestLoop
:WriteManifestDone
CALL :HashFile "!MANIFEST!"
DEL /F /Q "!MANIFEST!"
SET "BUILD_HASH=!ACTUAL_HASH! windows-!DEPENDENCIES[%~1].REVISION!"
SET "ACTUAL_HASH="
SET "CACHED_HASH="
IF EXIST "!BUILD_MARKER!" (
  SET /P CACHED_HASH=<"!BUILD_MARKER!"
  IF EXIST "!FOLDER!\" (
    IF "!CACHED_HASH!"=="!BUILD_HASH!" EXIT /B 0
  )
  DEL /F /Q "!BUILD_MARKER!"
  IF EXIST "!BUILD_MARKER!" EXIT /B 1
)
IF NOT EXIST "!FOLDER!" (
  MD "!FOLDER!" || EXIT /B 1
)
SET "J=0"
:DownloadFilesLoop
IF !J! GEQ !FILE_COUNT! GOTO DownloadFilesDone
CALL :GetFile %~1 !J!
CALL :DownloadFile "!FILES_URL!/!FILE_NAME!" "!FOLDER!\!FILE_NAME!" ^
  "!FILE_HASH!" || EXIT /B 1
SET /A J+=1
GOTO DownloadFilesLoop
:DownloadFilesDone
(ECHO !BUILD_HASH!) >"!FOLDER!\.rellu_download_complete" || EXIT /B 1
(ECHO !BUILD_HASH!) >"!BUILD_MARKER!" || EXIT /B 1
EXIT /B 0

:GetFile
SET "FILE_NAME=!DEPENDENCIES[%~1].FILES[%~2].NAME!"
SET "FILE_HASH=!DEPENDENCIES[%~1].FILES[%~2].HASH!"
EXIT /B 0

:DownloadFile
SET "FILE_URL=%~1"
SET "FILE_URL=!FILE_URL: =%%20!"
IF EXIST "%~2" (
  CALL :HashFile "%~2"
  IF /I "!ACTUAL_HASH!"=="%~3" EXIT /B 0
  DEL /F /Q "%~2" || EXIT /B 1
)
curl -fsL -o "%~2.download" "!FILE_URL!" || (
  IF EXIST "%~2.download" DEL /F /Q "%~2.download"
  EXIT /B 1
)
CALL :HashFile "%~2.download"
IF /I NOT "!ACTUAL_HASH!"=="%~3" (
  ECHO Error: SHA256 mismatch for !FILE_URL!.
  ECHO   Expected: %~3
  ECHO   Actual:   !ACTUAL_HASH!
  DEL /F /Q "%~2.download"
  EXIT /B 1
)
MOVE /Y "%~2.download" "%~2" >NUL || EXIT /B 1
EXIT /B 0

:HashFile
SET "ACTUAL_HASH="
FOR /F "skip=1 tokens=*" %%H IN ('certutil -hashfile "%~1" SHA256') DO (
  IF NOT DEFINED ACTUAL_HASH SET "ACTUAL_HASH=%%H"
)
SET "ACTUAL_HASH=!ACTUAL_HASH: =!"
EXIT /B 0
