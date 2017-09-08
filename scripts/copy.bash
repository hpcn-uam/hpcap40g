#!/bin/bash

source scripts/lib.bash

#######################
# PARAMS
#######################

dir=$(read_value_param basedir)
driver=$(read_value_param version)
ifindex=$1
iface="hpcap${ifindex}"
queue=$2
dev="hpcap_${ifindex}_${queue}"
disk=$(read_value_param "dir${ifindex}" )
nrxq=$(read_value_param nrxq)
core_base=$(read_value_param "core${ifindex}" )
#core_base=$(( $core_base + $nrxq + $queue ))
core_base=$(( $core_base + $(($((2 * $queue))+1)) ))
core_mask=$(( 1 << $core_base ))

#TAMANO DE BLOQUE DE TRANSFERENCIA
iblock=$( cat ${dir}/${driver}/include/hpcap.h | grep "#define HPCAP_IBS" | awk '{split($3,aux,"u");print aux[1]}' )
oblock=$( cat ${dir}/${driver}/include/hpcap.h | grep "#define HPCAP_OBS" | awk '{split($3,aux,"u");print aux[1]}' )
#CANTIDAD DE BLOQUES A TRANSFERIR
count=$( cat ${dir}/${driver}/include/hpcap.h | grep "#define HPCAP_COUNT" | awk '{split($3,aux,"u");print aux[1]}' )

logfile="${dir}/data/log_dd/${iface}_$(date +%Y_%m_%d__%H_%M)"

echo "Se copiaran " $count " bloques de " $iblock " bytes (se lanza en el core " $core_base " [ 0x" $core_mask " ]" >> ${logfile}.txt
echo "taskset -c ${core_base} dd if=/dev/${dev} ibs=${iblock} iflag=fullblock of=/${disk}/<hora>/<segundo>.raw obs=${oblock} oflag=direct count=$count" >> ${logfile}.txt

while [ 1 ]
do
	if [ -f "${dir}/reload_conf_${ifindex}" ]
	then
		echo "[$(date)] Reloadind configuration parameters..." >> ${logfile}.txt
		disk=$(read_value_param "dir${ifindex}" )
		rm "${dir}/reload_conf_${ifindex}"
	fi

	fecha=$(date +%s)
	nombre="${fecha}_${iface}_q${queue}"
	hora=$(( $(( $fecha / 1800 )) * 1800 ))
	mkdir -p ${disk}/${hora}

#	echo "[$(date)] taskset -c ${core_base} nice -n 20 dd if=/dev/${dev} bs=${iblock} iflag=fullblock of=${disk}/${hora}/${nombre}.raw oflag=direct count=$count" >> ${logfile}.txt
#	taskset -c ${core_base} nice -n 20 dd if=/dev/${dev} bs=${iblock} iflag=fullblock of=${disk}/${hora}/${nombre}.raw oflag=direct count=$count 2>> ${logfile}.txt
	taskset -c ${core_base} nice -n 20 dd if=/dev/${dev} bs=${iblock} iflag=fullblock of=/dev/null count=$count #2>> ${logfile}.txt

#	echo "[$(date)] taskset -c $(( ${core_base} + 1 )) nice -n 20 ${dir}/${driver}/samples/hpcapdd/hpcapdd $ifindex $queue ${disk}" >> hpcapdd_log.txt
#	taskset -c $(( ${core_base} + 1 )) nice -n 20 ${dir}/${driver}/samples/hpcapdd/hpcapdd $ifindex $queue ${disk} #>> hpcapdd_log.txt
#	taskset -c ${core_base} nice -n 20 ${dir}/${driver}/samples/hpcapdd/hpcapdd $ifindex $queue ${disk} #>> hpcapdd_log.txt
#	exit 0

done

