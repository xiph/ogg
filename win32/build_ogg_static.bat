@echo off
echo ---+++--- Building Ogg (Static) ---+++---

set OLDPATH=%PATH%
set OLDINCLUDE=%INCLUDE%
set OLDLIB=%LIB%

call "c:\program files\microsoft visual studio\vc98\bin\vcvars32.bat"
echo Setting include paths for Ogg
set INCLUDE=%INCLUDE%;c:\src\ogg\include
echo Compiling...
msdev ogg_static.dsp /useenv /make "ogg_static - Win32 Release" /rebuild

set PATH=%OLDPATH%
set INCLUDE=%OLDINCLUDE%
set LIB=%OLDLIB%
