#!/bin/bash

nt=`which nosetests`
if [ $nt == "" ]; then
    echo nosetests not installed
    exit 1
fi

for t in $EMC2_HOME/nosetests/*.py
do
    nosetests  $t
done
