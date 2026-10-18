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
SET "RECURSIVE_URL=https://github.com/arrowtype/recursive/releases"
CALL :AddDependency "Recursive-1.085" ^
  "!RECURSIVE_URL!/download/v1.085/ArrowType-Recursive-1.085.zip" ^
  "cbcbdf7a0e181d284a9235e09ed5f3873e527bc5dd1d977df71cdc1ff937da02" 1
SET "SOURCE_SANS_URL=https://github.com/adobe-fonts/source-sans/releases"
CALL :AddDependency "source-sans-pro-3.006R" ^
  "!SOURCE_SANS_URL!/download/3.006R/source-sans-pro-3.006R.zip" ^
  "b22c7a4c45050ed8ae1b5293a9fd3b886d6f5150eb1e2ded429b9a11ac1602c5" 1
CALL :AddFiles "RobotoMono-895ec69" "googlefonts/RobotoMono" ^
  "895ec691990d041dd727c7b5afa3ce56525d98e6" "fonts/ttf" 1
CALL :AddFile "RobotoMono-Bold.ttf" ^
  "3ecf35e5e87accc7578b605d1f5f0bc30d88b195d6807bec8a0c57f6aa95c4db"
CALL :AddFile "RobotoMono-BoldItalic.ttf" ^
  "a0f16567447311eaf42a35f6c50eb64b911694b42f1b01038e3b7e92c20f131d"
CALL :AddFile "RobotoMono-Italic.ttf" ^
  "4549325cd2d10938d37d63eba2aaca7c2e16e48322dc767576eab45e512b6ad2"
CALL :AddFile "RobotoMono-Light.ttf" ^
  "8045de2596189296c6ac8aaa76316a7b8289ee8b20a66d3d58122a0b8fc4391d"
CALL :AddFile "RobotoMono-LightItalic.ttf" ^
  "2f8dc95270e28464f26dde41aa515a25a2b6e81f0ba9a4168f3f73f51dbeefe3"
CALL :AddFile "RobotoMono-Medium.ttf" ^
  "4acbb2725a260eafff1bcaade3501204ca774e56346e259ee1b61d56996d2611"
CALL :AddFile "RobotoMono-MediumItalic.ttf" ^
  "e01593c1cf8034729deb4ebe412d19884907ca1156962169bca624ef139d827a"
CALL :AddFile "RobotoMono-Regular.ttf" ^
  "af0bff7599c3df3831755c16e39b3c496df74b8c8d8a1161b14dc8461be17cb4"
CALL :AddFile "RobotoMono-Thin.ttf" ^
  "77c06a7cca40839e7e98f08837bb2a9510690472ad156b32f663c05ceff13186"
CALL :AddFile "RobotoMono-ThinItalic.ttf" ^
  "78b05d06047962c6ba4cb6a8301acca3986ca7d51e4d34a67e4fd82e6e515617"
CALL :AddFiles "Work-Sans-2.010" "weiweihuanghuang/Work-Sans" ^
  "6634e7b2395e9dba4ac8611c4529968a9719d606" "fonts/static/TTF" 1
CALL :AddFile "WorkSans-Black.ttf" ^
  "5cb2a4c90dd9c4504ed002e608863346368c0ac7e3f6a71d6c03643c30a91ba4"
CALL :AddFile "WorkSans-BlackItalic.ttf" ^
  "1500e83d92f3117ed62a1720c111e1d10508251c5ea6cb14f1907364d337ff90"
CALL :AddFile "WorkSans-Bold.ttf" ^
  "e5c36b1191be27046270da352f231fc342ad4333cfb2967f393e00d18c274cdc"
CALL :AddFile "WorkSans-BoldItalic.ttf" ^
  "8f7b9e47221fab81026d48d1d746f3e8cbad4b045524c09645d052efd181ca19"
CALL :AddFile "WorkSans-ExtraBold.ttf" ^
  "d47d48e8290a4d1a90faed7812edad2a1418295a431859557d618ed9247f2aed"
CALL :AddFile "WorkSans-ExtraBoldItalic.ttf" ^
  "ec8d47247d300bf96282c3092be43086e5e0162482600e057199474fa0dde163"
CALL :AddFile "WorkSans-ExtraLight.ttf" ^
  "7939231e7d9abfa68030383bdb8ac9fc05b0cc7f50da45b76018778dd9a41be8"
CALL :AddFile "WorkSans-ExtraLightItalic.ttf" ^
  "c62370f8294f7f97c956aae8bb0c862c05d6dd88a780305978144697d152b83f"
CALL :AddFile "WorkSans-Italic.ttf" ^
  "8fcbc2499374a09594ff28ed488852191f3451ec2ef430d421ec327b3d6c15e8"
CALL :AddFile "WorkSans-Light.ttf" ^
  "16d4241f82605d7a104e49ace6b11c07c1ed4e5b70bbdc6cd88311a3152665eb"
CALL :AddFile "WorkSans-LightItalic.ttf" ^
  "212687f73b6f752d51a8ebc73b73dcfeb0c4a2b8af95b8bd0d408aa2ac25cd52"
CALL :AddFile "WorkSans-Medium.ttf" ^
  "3c5ae1ef9260a0c1cdf1f59841f28620c40826f6ca3bd4c71516997c29fec7dc"
CALL :AddFile "WorkSans-MediumItalic.ttf" ^
  "81877c8081f4d68a4932b5128438cd892df72501206c17a5f7437a4ad86b6dab"
CALL :AddFile "WorkSans-Regular.ttf" ^
  "22e7f1607ebc29d03be61d893ec47dde307847eaf60fbec260e286695001982a"
CALL :AddFile "WorkSans-SemiBold.ttf" ^
  "399b8be04df8e5c531cc1cf490d9d4d781299440e086d61097db5175e2c71321"
CALL :AddFile "WorkSans-SemiBoldItalic.ttf" ^
  "c5e1b356c227239cd3003e606af03ca7199194e67f83639e32406ac269d0ca48"
CALL :AddFile "WorkSans-Thin.ttf" ^
  "91d398dfc6367fe18e80e296d219e99d73507d6a0ac927d27188104a68678953"
CALL :AddFile "WorkSans-ThinItalic.ttf" ^
  "08ae234d96652a4befd4dea18538ea71a53211823932a4f554dc338174e9e285"
CALL :AddFiles "Raleway-6c67ab1" "impallari/Raleway" ^
  "6c67ab1f7aa65c442bd2745bb9d4ef1cd7bc01fa" "fonts/v3.000 Fontlab/TTF" 1
CALL :AddFile "Raleway-Black-Italic.ttf" ^
  "332be78c28e10f13eb395f50dd55fcc508c303d54948ce22f35d07706c072981"
CALL :AddFile "Raleway-Black.ttf" ^
  "f27f4ec663f6d59f4c14a324d45c520b27b034c94381977f61873008a094c874"
CALL :AddFile "Raleway-Bold-Italic.ttf" ^
  "88f61a661d8ccdc2189b9baf5a46f26a3889c3902e27dd4b07b1f5845172cdfe"
CALL :AddFile "Raleway-Bold.ttf" ^
  "4bbee5ac4b10b4fbe5fdb0bff6a75898d8800c62ad16311484e8680ef46b5701"
CALL :AddFile "Raleway-ExtraBold-Italic.ttf" ^
  "617165bc6f2d085b0dd94ee0ba82034cf17d2fa621dcf4fff70064e91f995566"
CALL :AddFile "Raleway-ExtraBold.ttf" ^
  "d8a96c8de333a8854254bbd9a6022df2bb434205e82c0987d944e1f74e2e6cf5"
CALL :AddFile "Raleway-ExtraLight-Italic.ttf" ^
  "58d3dc30e5b4b5c903fe0cad25f9245ac9026176e7455a356b3b7b8ba8fee534"
CALL :AddFile "Raleway-ExtraLight.ttf" ^
  "dbe8a009a21ed2e6ad0f7b7eaccb630c3e33708a4301ec0e36f2f562b9d1c11d"
CALL :AddFile "Raleway-Light-Italic.ttf" ^
  "a25be620f79edf1bdd37377cf94017b3d3d85223d8bd4deec9b33d385ba43535"
CALL :AddFile "Raleway-Light.ttf" ^
  "d11b6f8bbb1025a56e94141b4fe29ff6c8cd67c60a0adeb7165679e5fba70988"
CALL :AddFile "Raleway-Medium-Italic.ttf" ^
  "5cfee7494e1af0f1c4e97ee000c703836971ff78b95a325fe330f84e492ec336"
CALL :AddFile "Raleway-Medium.ttf" ^
  "84e15d32decbb3e051c169b4023d7debafa044587b8f12eeb3324f4ed2683f48"
CALL :AddFile "Raleway-Regular-Italic.ttf" ^
  "104679ffca1064f25c27f71c6e672b5d2d21ec1cbaf0da93b84fdd4b7c79926f"
CALL :AddFile "Raleway-Regular.ttf" ^
  "1ac5d5e41cd972d0700f41a03dd25a1315ae872b774a17150dbd835ba7cbb7d9"
CALL :AddFile "Raleway-SemiBold-Italic.ttf" ^
  "7d86dc1c7c5d0f52d8ce87648edefaa7c156ddae01cbfc6f53868db08f26baab"
CALL :AddFile "Raleway-SemiBold.ttf" ^
  "ce7712f51d2775f751087b9a13ccaee41ef6d7a822c69677e738f679f6f8eb2a"
CALL :AddFile "Raleway-Thin-Italic.ttf" ^
  "49b03e5c0baaefcb7ba3b10221d6e1096454c92072042872ddfe9adfb7550af4"
CALL :AddFile "Raleway-Thin.ttf" ^
  "62c89b72e490e3c7832f9c5ab4e8f554e57356fefd2da31f3e9d0a15b564823e"
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
CALL :AddFiles "ptserif-47d54ca" "google/fonts" ^
  "47d54ca1ebb14fd864695f1d39dc734f39017c33" "ofl/ptserif" 1
CALL :AddFile "PT_Serif-Web-Bold.ttf" ^
  "038ba7336bd7ea14f12ad155bed51a4345cac5153275d521dec3ba04021c526e"
CALL :AddFile "PT_Serif-Web-BoldItalic.ttf" ^
  "f003788ba08981eb0988b3557a6f224a53dab49c20e283e8b74d5af3c466f8be"
CALL :AddFile "PT_Serif-Web-Italic.ttf" ^
  "f57e95ff9dc85691a3b2e193f2028db36f6663939a46c0fc4f286d618b80b7ce"
CALL :AddFile "PT_Serif-Web-Regular.ttf" ^
  "a4951fade06ff8f09b7673aa81ffb65a8cd409e24d3289a6dc670bc4dda2557a"
CALL :AddFiles "ptserifcaption-47d54ca" "google/fonts" ^
  "47d54ca1ebb14fd864695f1d39dc734f39017c33" "ofl/ptserifcaption" 1
CALL :AddFile "PT_Serif-Caption-Web-Italic.ttf" ^
  "afe69233498676c418383b9defda8aace407eca0e86f4499d9ba4e9f494f9da5"
CALL :AddFile "PT_Serif-Caption-Web-Regular.ttf" ^
  "d17d47f44e840eee017f839b27dff3c94c7afe7743eff308d4bd11f7018b69af"
CALL :InstallDependencies || EXIT /B 1
EXIT /B 0
ENDLOCAL

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
