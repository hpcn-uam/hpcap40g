#!/bin/bash

function elige_disco()
{
	candidatos="sda sdb"
	found=0
	for disco in $candidatos
	do
		n=$(ls -1 /dev/${disco}* | wc -l)
		if [ $n -eq 1 ]
		then
			echo $disco
			found=1
		fi
	done
	if [ $found -eq 0 ]
	then
		echo "none"
	fi
}

function lista_discos()
{
	nd=$1
	lista=""
	first=1
	for i in $(seq 0 $(( $nd - 1 )) )
	do
		if [ $first -eq 1 ]
		then
			first=0
		else
			lista="${lista},"
		fi
		#El numero depende del "enclosure ID" (cambia con la controladora)
		#para verlo: <comando_megacli> -PDList -aALL
		lista="${lista}12:${i}"
		#lista="${lista}23:${i}"
	done
	echo $lista
}


ndiscos=12
strips=1024
#strips=256
dcache=1
cachepol="WB"
fs="xfs"

mountpoint="/almacenamiento"

cmd="/opt/MegaRAID/CmdTool2/CmdTool264"
#cmd="megacli"

umount $mountpoint

###################################################################################
#
# CREATE RAID0 ARRAY
#
###################################################################################
device="none"
while [ $device == "none" ]
do
	# https://supportforums.cisco.com/docs/DOC-16309
	# http://linux.alanstudio.hk/megacli_command.htm

	#Borrar RAID
	${cmd} -CfgLdDel -Lall -a0 > /dev/null
	
	#Crear RAID0
	discos=$(lista_discos $ndiscos)
	if [ $cachepol == "Direct" ]
	then
		echo "${cmd} -CfgLdAdd -r0[$discos] Direct -strpsz${strips} -a0"
		${cmd} -CfgLdAdd -r0[$discos] Direct -strpsz${strips} -a0
	else
		echo "${cmd} -CfgLdAdd -r0[$discos] Cached -strpsz${strips} -a0"
		${cmd} -CfgLdAdd -r0[$discos] Cached -strpsz${strips} -a0
	fi
	
	if [ $cachepol == "WB" ]
	then
		echo "${cmd} -LDSetProp ForcedWB -Immediate -L0 -a0"
		${cmd} -LDSetProp ForcedWB -Immediate -L0 -a0
	else
		echo "${cmd} -LDSetProp WT -L0 -a0"
		${cmd} -LDSetProp WT -L0 -a0
	fi
	
	if [ $dcache -eq 0 ]
	then
		echo "${cmd} -LDSetProp -DisDskCache -L0 -a0"
		${cmd} -LDSetProp -DisDskCache -L0 -a0
	else
		echo "${cmd} -LDSetProp -EnDskCache -L0 -a0"
		${cmd} -LDSetProp -EnDskCache -L0 -a0
	fi
	
	#Listado de Volúmenes Lógicos
	echo "${cmd} -LDInfo -Lall -a0"
	${cmd} -LDInfo -Lall -a0
	
	#Listado de Volúmenes Físicos
	#megacli -PDList -aALL
				
	sleep 1
	device=$(elige_disco)

	echo "Se utilizara el dispositivo $device"
done

echo "Se utilizara el dispositivo $device"


###################################################################################
#
# CREATE FILESYSTEM
#
# http://erikugel.wordpress.com/2011/04/14/the-quest-for-the-fastest-linux-filesystem/
#
###################################################################################
mount=1
err=0
stripsz=$(( $strips * 1024 )) #pasar de KB a B
chunk=$(( 1024 * 1024 * $ndiscos ))
if [ $fs == "ext4" ]
then
	stride=$(( $stripsz / 4096 ))
	stripe=$(( $stride * $ndiscos ))
	echo "mkfs.ext4 -F -b 4096 -E stride=${stride},stripe-width=${stripe} /dev/${device}"
	mkfs.ext4 -F -b 4096 -E stride=${stride},stripe-width=${stripe} /dev/${device}
elif [ $fs == "xfs" ]
then
	sunit=$(( $stripsz / 512 ))
	swidth=$(( $sunit * $ndiscos ))
	echo "mkfs.xfs -f -b size=4096 -d sunit=${sunit},swidth=${swidth} /dev/${device}"
	mkfs.xfs -f -b size=4096 -d sunit=${sunit},swidth=${swidth} /dev/${device}
elif [ $fs == "jfs" ]
then
	echo "mkfs.jfs -q /dev/${device}"
	mkfs.jfs -q /dev/${device}
elif [ $fs == "none" ]
then
	mount=0
else
	echo "Sistema de ficheros incorrecto"
	echo "Sistema de ficheros incorrecto" >> $ofile
	mount=0
	err=1
fi


###################################################################################
#
# MOUNT VOLUME
#
###################################################################################
if [ $mount -eq 1 ]
then
	echo "mount -t $fs /dev/${device} ${mountpoint}"
	mount -t $fs /dev/${device} ${mountpoint}
	#mount -t $fs /dev/${device} ${mountpoint} -o noatime,nodiratime,data=writeback,stripe=16,barrier=0,errors=remount-ro

	sleep 1
	df -h	
	n=$(df -h | grep $device | wc -l)
	if [ $n -ne 1 ]
	then
		err=1
	fi
fi
		
if [ $err -eq 0 ]
then
	echo ""
	echo "[SUCCESS]"
	exit 0
else
	echo ""
	echo "[ERROR]"
	exit -1
fi

