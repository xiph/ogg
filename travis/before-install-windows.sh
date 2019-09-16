#!/bin/bash

choco install buckaroo

if [ $BUCKAROO_USE_BAZEL ]
then

choco install bazel

else

choco install zip
choco install unzip
choco install buck

powershell -Command 'set-executionpolicy unrestricted'
powershell -Command 'Install-Module -Name PSCX -AllowClobber'
powershell -Command 'Install-Module -Name VSSetup -AllowClobber'
powershell -Command 'Import-VisualStudioVars 2017 amd64'

fi
