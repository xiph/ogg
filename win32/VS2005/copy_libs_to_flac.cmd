@ECHO OFF
SET FLAC_DEBUG=..\..\..\flac\obj\debug\lib
SET FLAC_RELEASE=..\..\..\flac\obj\release\lib


COPY debug\libogg.lib %FLAC_DEBUG%\
COPY release\libogg.lib %FLAC_RELEASE%\

PAUSE
