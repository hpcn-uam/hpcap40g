#!/bin/bash

pushd ../samples/hpcapdd

n=$2
if=$1

for i in $(seq 0 $(( $n -1)) )
do
	echo "taskset -c $i ./hpcapdd $if $i null"
	taskset -c $i ./hpcapdd $if $i null &
done 
popd

tail -f ../data/$(date  +%Y-%V)/hpcap${if}
