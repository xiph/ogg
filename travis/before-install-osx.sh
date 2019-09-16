#!/bin/bash

if [ $BUCKAROO_USE_BAZEL ]
then

wget https://github.com/bazelbuild/bazel/releases/download/0.29.1/bazel-0.29.1-installer-darwin-x86_64.sh
chmod +x ./bazel-0.29.1-installer-darwin-x86_64.sh
sudo ./bazel-0.29.1-installer-darwin-x86_64.sh

else

c++ --version
g++ --version
gcc --version

mkdir -p bin

wget -c https://github.com/njlr/buck-warp/releases/download/v0.2.0/buck-2019.01.10.01-osx -O bin/buck
chmod +x ./bin/buck
sudo cp ./bin/buck /usr/local/bin/buck
buck --version

wget -c https://github.com/LoopPerfect/buckaroo/releases/download/$BUCKAROO_VERSION/buckaroo-macos -O bin/buckaroo
chmod +x ./bin/buckaroo
sudo cp ./bin/buckaroo /usr/local/bin/buckaroo
buckaroo version

fi
