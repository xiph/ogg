REM @echo off

Echo LIB Windows Build NuGet

REM # XEON x64 Build Vars #
set _SCRIPT_DRIVE=%~d0
set _SCRIPT_FOLDER=%~dp0
set INITDIR=%_SCRIPT_FOLDER%
set SRC=%INITDIR%\..\..\
set BUILDTREE=%SRC%\build-win\
SET tbs_arch=x64
SET vcvar_arg=x86_amd64
SET cmake_platform="Visual Studio 15 2017 Win64"

REM # VC Vars #
SET VCVAR="%programfiles(x86)%\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist %VCVAR% call %VCVAR% %vcvar_arg%
SET VCVAR="%programfiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist %VCVAR% call %VCVAR% %vcvar_arg%

REM # Clean Build Tree #
rd /s /q %BUILDTREE%
mkdir %BUILDTREE%
cd %BUILDTREE%

:copy_files
set BINDIR=%SRC%\build-nuget\
rd /s /q %BINDIR%
mkdir %BINDIR%

:static_LIB
REM # LIB STATIC #
ECHO %cmake_platform% STATIC

rd /s /q %BUILDTREE%\ogg
mkdir %BUILDTREE%\ogg
cd %BUILDTREE%\ogg
cmake -G %cmake_platform% ^
-DBUILD_SHARED_LIBS:BOOL=OFF ^
-DCMAKE_CXX_FLAGS_RELEASE="/MD" ^
-DCMAKE_CXX_FLAGS_DEBUG="/MDd" ^
-DCMAKE_C_FLAGS_RELEASE="/MD" ^
-DCMAKE_C_FLAGS_DEBUG="/MDd" ^
-DCMAKE_INSTALL_PREFIX=%BINDIR% ^
-DCMAKE_BUILD_TYPE="Release" %SRC%
cmake --build . --config Release --target install

move %BINDIR%lib %BINDIR%lib_release
move %BINDIR%bin %BINDIR%bin_release

REM # Clean Build Tree #
rd /s /q %BUILDTREE%\ogg

REM # DEBUG #
rd /s /q %BUILDTREE%\ogg
mkdir %BUILDTREE%\ogg
cd %BUILDTREE%\ogg
cmake -G %cmake_platform% ^
-DBUILD_SHARED_LIBS:BOOL=OFF ^
-DCMAKE_CXX_FLAGS_RELEASE="/MD" ^
-DCMAKE_CXX_FLAGS_DEBUG="/MDd" ^
-DCMAKE_C_FLAGS_RELEASE="/MD" ^
-DCMAKE_C_FLAGS_DEBUG="/MDd" ^
-DCMAKE_INSTALL_PREFIX=%BINDIR% ^
-DCMAKE_BUILD_TYPE="DEBUG" %SRC%
cmake --build . --config DEBUG --target install

move %BINDIR%lib %BINDIR%lib_debug
move %BINDIR%bin %BINDIR%bin_debug

REM # TODO: ENABLE SHARED Build
GOTO:nuget_req
mkdir %BINDIR%\static\
move /Y %BINDIR%\lib %BINDIR%\static\

:shared_LIB
REM # LIB SHARED #
ECHO %cmake_platform% SHARED


:nuget_req
REM # make nuget packages from binaries #
copy %INITDIR%\ogg-msvc-%tbs_arch%.targets %BINDIR%\ogg-msvc-%tbs_arch%.targets
cd %BUILDTREE%
nuget pack %INITDIR%\ogg-msvc-%tbs_arch%.nuspec
cd %INITDIR%
REM --- exit ----
GOTO:eof
