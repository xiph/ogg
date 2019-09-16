#!/bin/bash

if [ $BUCKAROO_USE_BAZEL ]
then

wget https://github.com/bazelbuild/bazel/releases/download/0.29.1/bazel-0.29.1-installer-linux-x86_64.sh
chmod +x ./bazel-0.29.1-installer-linux-x86_64.sh
sudo ./bazel-0.29.1-installer-linux-x86_64.sh

else

sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-6 90
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-6 90

c++ --version
g++ --version
gcc --version

mkdir -p bin

wget -c https://github.com/njlr/buck-warp/releases/download/v0.2.0/buck-2019.01.10.01-linux -O bin/buck
chmod +x ./bin/buck
sudo cp ./bin/buck /usr/bin/buck
buck --version

wget -c https://github.com/LoopPerfect/buckaroo/releases/download/$BUCKAROO_VERSION/buckaroo-linux -O bin/buckaroo
chmod +x ./bin/buckaroo
sudo cp ./bin/buckaroo /usr/bin/buckaroo
buckaroo version

fi
