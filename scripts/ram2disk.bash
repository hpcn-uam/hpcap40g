#!/bin/bash

disk="mnt"
odisk="/disco_capturas_0"

while [ true ]
do

	n=$(ls -1rtd /${disk}/*/* | wc -l )
	if [ $n -gt 2 ]
	then

	fich=$(ls -1rtd /${disk}/*/* | head -n 1 )
	dir=$(echo $fich | awk -v FS="/" '{print $3}')
	name=$(echo $fich | awk -v FS="/" '{print $4}')
	mkdir -p ${odisk}/$dir
	taskset -c 7 mv $fich ${odisk}/${dir}/$name &
	echo "[$(pidof mv)] taskset -c 7 mv $fich ${odisk}/${dir}/$name"

	fich=$(ls -1rtd /${disk}/*/* | head -n 2 | tail -n 1 )
	dir=$(echo $fich | awk -v FS="/" '{print $3}')
	name=$(echo $fich | awk -v FS="/" '{print $4}')
	mkdir -p ${odisk}/$dir
	taskset -c 8 mv $fich ${odisk}/${dir}/$name &
	echo "[$(pidof mv)] taskset -c 8 mv $fich ${odisk}/${dir}/$name"

	wait $(pidof mv)
	fi
done
