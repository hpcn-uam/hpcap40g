#!/bin/bash

source lib.bash

iface=$1
ncolas=$2
core=0

rmmod ixgbe
echo "modprobe ixgbe RSS=$ncolas"
modprobe ixgbe RSS=$ncolas
ifconfig $iface up promisc
set_irq_affinity $iface $core

