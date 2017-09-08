#!/bin/bash

# scheduler por defecto: noop anticipatory deadline
# nr_requests por defecto: 128

# sunit = 256k / 512 = 512   
# swidth = # disk (raid 0) * sunit = 6 * 512 = 3072
#mkfs.xfs -f -d sunit=512,swidth=3072 -l version=2,sunit=512 /dev/sda
#mkfs.xfs -f -d sunit=512,swidth=3072 -l logdev=/dev/ram0 /dev/sda

#echo cfq > /sys/block/sda/queue/scheduler
#echo 128 > /sys/block/sda/queue/nr_requests

disco="/disco_capturas_0"
device="sdb"
umount $disco

k=1024
m=$(( ${k} * ${k} )) #1 mega
stripsize=$(( $m / 1 )) #512
ndisks=12

sunit=$(( $stripsize / 512 ))
swidth=$(( $ndisks * $sunit ))

echo "strip="$stripsize
echo "sunit="$sunit "swidth="$swidth

# El log a un ramdisk
mkfs.xfs -f -d sunit=${sunit},swidth=${swidth} -l logdev=/dev/ram0 /dev/${device}
mount -t xfs -o defaults,sunit=${sunit},swidth=${swidth},logdev=/dev/ram0,nobarrier,noatime,nodiratime /dev/${device} $disco

# El log "normal"
#mkfs.xfs -f -d sunit=${sunit},swidth=${swidth} /dev/${device}
#mount -t xfs /dev/${device} $disco
#mkfs.xfs -f /dev/${device}
#mount -t xfs /dev/${device} $disco

# sunit = 1M / 512 = 2048 
# swidth = # disk (raid 0) * sunit = 8 * 2048 = 1638
#mkfs.xfs -f -d sunit=2048,swidth=16384 -l logdev=/dev/ram0 /dev/sda
#mount -t xfs -o defaults,sunit=2048,swidth=16384,logdev=/dev/ram0,nobarrier,noatime,nodiratime /dev/sda /disco_capturas

# sunit = 256k / 512 = 512   
# swidth = # disk (raid 0) * sunit = 8 * 512 = 4096
#mkfs.xfs -f -d sunit=512,swidth=4096 -l version=2,sunit=512 /dev/sda
#mount -t xfs -o defaults,sunit=512,swidth=4096,nobarrier,noatime,nodiratime /dev/sda /disco_capturas

#mkfs.xfs -f -d sunit=512,swidth=4096 -l logdev=/dev/ram0 /dev/sda
#mount -t xfs -o defaults,sunit=512,swidth=4096,logdev=/dev/ram0,nobarrier,noatime,nodiratime /dev/sda /disco_capturas

# sunit = 8k / 512 = 16
# swidth = # disk (RAID 0) * sunit = 8 * 16 = 128
#mkfs.xfs -f -d sunit=16,swidth=128 -l logdev=/dev/ram0 /dev/sda
#mount -t xfs -o defaults,sunit=16,swidth=128,logdev=/dev/ram0,nobarrier,noatime,nodiratime /dev/sda /disco_capturas
