#!/bin/bash

sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-6 90
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-6 90

c++ --version
g++ --version
gcc --version

mkdir -p bin

wget -c https://github.com/njlr/buck-warp/releases/download/v0.2.0/buck-2019.01.10.01-linux -O bin/buck
chmod +x ./bin/buck
./bin/buck --version

wget -c https://github.com/LoopPerfect/buckaroo/releases/download/$BUCKAROO_VERSION/buckaroo-linux -O bin/buckaroo
chmod +x ./bin/buckaroo
./bin/buckaroo version
