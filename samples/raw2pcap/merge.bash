#!/bin/bash

base="/home/telefonica/BT/captura5diasUralita/capturas_RAW"
curr=$(pwd)
version="hpcap_ixgbe-3.7.17_buffer"
aux=""
rm file.txt
ls -rt1 $base |
	while read dir
	do
		ls -rt1 ${base}/${dir}/*.pcap |
			while read pcapfile
			do
				echo $pcapfile >> file.txt
			done			
	done

mergecap -w /disco_capturas_0/salidaUralita5D.pcap `awk '{a[NR]=$1}END{for (i in a) printf "%s ",a[i]}' file.txt`

