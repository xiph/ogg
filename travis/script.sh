#!/bin/bash

buckaroo install

if [ $BUCKAROO_USE_BAZEL ]
then

bazel build //:ogg

else

buck build -c ui.superconsole=DISABLED //:ogg

fi
