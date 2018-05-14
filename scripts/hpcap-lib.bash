#!/bin/bash

#######################
# AUX FUNCTIONS USED BY OTHER SCRIPTS
#######################

base_driver_types="ixgbe i40e i40evf"

die() { echo "$@" 1>&2 ; exit 1; }

function negocia_iface()
{
	local basedriver=$(ethtool -i $1 | grep driver | awk '{print $2}')
	if [ $2 = "40000" ] && [ "$basedriver" != "i40e" ]; then
		return # No ethtool setttings required on Mellanox cards
	fi

	if ethtool -s $1 speed $2 duplex full autoneg on 2>/dev/null | grep "Cannot set new setting" &> /dev/null ; then
		echo "Warning: Seems we can't set the interface with autonegotiation. Trying with autoneg off."
		ethtool -s $1 speed $2 duplex full autoneg off
	fi
	ethtool -A $1 autoneg off rx off tx off
	ethtool -K $1 tso off gso off gro off lro off rx off sg on rxvlan off rxhash off #ufo off

	if [ "$basedriver" = "i40e" ]; then
		echo "Performing i40e interrupt moderation adjustments"
		ethtool -C $1 rx-usecs 0 tx-usecs 0 adaptive-rx off adaptive-tx off rx-usecs-high 0
		ip link set $1 down
		ip link set $1 up
	fi
}
function check_iface_link()
{
	aux=$( ethtool $1 | grep "Link detected" | awk '{print $3}' )
	if [ $aux = "yes" ]
	then
		echo "     -> Interface ${1}: link successfully configured"
	else
		echo "     -> Error while negociating $1 link"
	fi
}
function remove_module()
{
	# Use \<, \> to match the full word and not just part of the module name.
	if lsmod | grep "\<$1\>" &> /dev/null; then
		echo "     -> Deleting kernel module $1"
		rmmod $1
	fi
}

paramfile="params.cfg"

function ensure_paramfile_exists()
{
	if [ ! -e "$paramfile" ]; then
		echor "Configuration file $paramfile not found." 1>&2
		echo "Please generate it with scripts/gen-hpcap-config " 1>&2
		echo "or copy params.cfg.sample and modify it to your needs." 1>&2
		exit 1
	fi
}

function read_value_param()
{
	grep -v "#" $paramfile | grep "${1}=" | awk -v FS="[;,=]" '{print $2}'
}

function check_and_mount()
{
	aux=$(df -h | grep $1 | wc -l)
	if [ $aux = 0 ]
	then
		echo "     -> Mounting $2 on /$1"
		mount /dev/$2 /$1
	else
		echo "     -> /$1 already mounted"
	fi
	nr_req=$(read_value_param nr_req)
	echo "echo $nr_req > /sys/block/${2}/queue/nr_requests"
	echo $nr_req > /sys/block/${2}/queue/nr_requests
}
function repeat()
{
	aux=""
	for i in $(seq 1 $1)
	do
		if [ $i != 1 ]
		then
			aux="${aux},$2"
		else
			aux="$2"
		fi
	done
	echo $aux
}
function fill()
{
	num=$2
	ret=""
	arg=$1
	for i in $(seq 0 $(( $num - 1 )) )
	do
		leido=$(read_value_param "${arg}${i}")
		if [ $i != 0 ]
		then
			ret="${ret},$leido"
		else
			ret="$leido"
		fi
	done
	echo $ret
}
function fill_cores()
{
	num=$1
	ret=""
	for i in $(seq 0 $(( $num - 1 )) )
	do
		leido=$(read_value_param "core${i}")
		leido=$(get_real_core_index $leido)
		if [ $i != 0 ]
		then
			ret="${ret},$leido"
		else
			ret="$leido"
		fi
	done
	echo $ret
}
function fill_nodes()
{
	num=$1
	ret=""
	for i in $(seq 0 $(( $num - 1 )) )
	do
		leido=$(read_value_param "core${i}")
		leido=$(get_real_core_index $leido)
		if [ $i != 0 ]
		then
			ret="${ret},$(nodo_del_core $leido)"
		else
			ret="$(nodo_del_core $leido)"
		fi
	done
	echo $ret
}
function nodo_del_core()
{
	local core="$1"
	i=0

	ensure_necessary_commands

	numactl --hardware | grep cpus | awk -v FS="cpus: " '{print $2}' |
	( while read linea
		do
			for j in $linea ; do
				if [ "$core" = "$j" ];then
					echo "$i"
				fi
			done
			i=$(( $i + 1 ))
		done )
}

function get_real_core_index()
{
	local monitor_core=$1

	if [ $monitor_core -eq -1 ]; then
		local num_cores=$( cat /proc/cpuinfo | grep processor | wc -l )
		monitor_core=$(( $num_cores -1 ))
	fi

	echo $monitor_core
}

function set_irq_affinity()
{
	cat /proc/interrupts | grep $iface | awk '{split($1,irq,":");print irq[1];}' |
		while read irq
		do
			num=$(printf "%x\n"  $(( 1 << $core )) )
			echo $num > /proc/irq/${irq}/smp_affinity
		done
}

function get_active_cores()
{
	local iface_count=$(read_value_param nif)
	local nconsumers=$(read_value_param consumers)

	for i in $(seq 0 $((iface_count - 1)) ); do
		local iface_core="$(read_value_param "core${i}")"

		for c in $(seq 0 $((nconsumers - 1)) ); do
			echo $((iface_core + c))
		done
	done
}

function get_scaling_governor_of()
{
	local core="$1"

	cat /sys/devices/system/cpu/cpu${core}/cpufreq/scaling_governor
}

function set_scaling_governor_of()
{
	local core="$1"
	local scaling="$2"

	echo $scaling > /sys/devices/system/cpu/cpu${core}/cpufreq/scaling_governor
}

function get_governors_on_active_cores()
{
	( for core in $(get_active_cores); do
		get_scaling_governor_of $core
	done ) | sort | uniq
}

function kill_proc()
{
	max_retries=5
	retries=0
	procname=$1
	pid=$(pidof $procname)

	if [ $? -ne 0 ]; then
		return # Process does not exist
	fi

	if [ -z "$2" ]; then
		signal=SIGTERM
	else
		signal=$2
	fi

	while is_pid_running $pid &> /dev/null && [ $retries -lt $max_retries ]; do
		kill -s $signal $pid
		sleep 1
		retries=$((retries + 1))
	done

	kill -s SIGKILL $pid
}

# Gets a certain value from the /proc/meminfo file.
function get_meminfo_val()
{
	grep $1 /sys/devices/system/node/node*/meminfo | sort -n -k 2 | awk '{ print $4 }' | tr '\n' '|' | sed 's#|$##'
}

## Some variables and functions for formatting output
tbold=$(tput bold)
treset=$(tput sgr0)
tred=$(tput setaf 1)
tgreen=$(tput setaf 2)
tyellow=$(tput setaf 3)

function echob()
{
	local out="$@"
	[ -t 1 ] && out="${tbold}${out}${treset}"
	echo -e "${out}"
}

function echor()
{
	local out="$@"
	[ -t 1 ] && out="${tred}${out}${treset}"
	echo -e "${out}"
}

function echoy()
{
	local out="$@"
	[ -t 1 ] && out="${tyellow}${out}${treset}"
	echo -e "${out}"
}

function echog()
{
	local out="$@"
	[ -t 1 ] && out="${tgreen}${out}${treset}"
	echo -e "${out}"
}

# Output hugepage information
function hp_info()
{
	total_hp=$(get_meminfo_val HugePages_Total)
	free_hp=$(get_meminfo_val HugePages_Free)

	echo "Hugepage per node stats: $free_hp free. $total_hp total"
}

function get_hugepage_path()
{
	mount | grep "type hugetlbfs" | awk '{print $3}' | head -n 1
}

function has_link()
{
	iface=$1

	link_status=$(ethtool $iface 2>/dev/null | grep "Link detected" | awk '{print $3}')

	if [ "$link_status" = "yes" ]; then
		return 0
	else
		return 1
	fi
}

# Prints the arguments for the driver based on the given paramfile
function parse_driver_args()
{
	paramfile=$1
	local driver_type=$2

	local itr=956
	local nif=$(read_value_param nif)
	local nrxq=$(read_value_param nrxq)
	local ntxq=$(read_value_param ntxq)
	local ncons=$(read_value_param consumers)

	local args=""
	args+="RXQ=$(repeat $nif ${nrxq}) "
	args+="TXQ=$(repeat $nif ${ntxq}) "
	args+="Consumers=$(repeat $nif ${ncons}) "

	if [ "$driver_type" = "ixgbe" ]; then
		args+="VMDQ=$(repeat $nif 0) "
	fi

	if [ "$driver_type" = "ixgbe" ] || [ "$driver_type" = "vf" ]; then
		args+="InterruptThrottleRate=$(repeat $nif ${itr}) "
	fi

	args+="Core=$(fill_cores $nif) "
	args+="Caplen=$(fill caplen $nif) "
	args+="Mode=$(fill mode $nif | tr 3 2) " # Mode 3 is the same that mode 2 at the driver level.
	args+="Dup=$(fill dup $nif) "
	args+="Pages=$(fill pages $nif)"

	echo $args
}

function get_driver_type()
{
	local kofile="$1"

	if [[ "$kofile" = *"mlx.ko" ]]; then
		echo "mlnx"
	elif [[ "$kofile" = *"ivf.ko" ]]; then
		echo "i40evf"
	elif [[ "$kofile" = *"vf.ko" ]]; then
		echo "vf"
	elif [[ "$kofile" = *"i.ko" ]]; then
		echo "i40e"
	else
		echo "ixgbe"
	fi
}

function get_hpcap_major()
{
	first_hpcap_iface=$(read_value_param ifs | tr ' ' '\n' | grep hpcap | head -n 1)
	first_hpcap_idx=$(get_iface_index $first_hpcap_iface)
	first_hpcap_major=$(grep hpcap /proc/devices | head -n 1 | awk '{print $1}')

	echo $((first_hpcap_major - first_hpcap_idx))
}

function get_iface_index()
{
	echo $1 | awk -v FS="hpcap|xgb" '{print $2}'
}

# Get the best version of the binary: first try for
# the bin/release folder of the HPCAP source tree
# and then rely on the PATH.
function get_binary()
{
	if [ -e "bin/release/$1" ]; then
		echo "bin/release/$1"
	elif [ -e "../bin/release/$1" ]; then
		echo "../bin/release/$1"
	else
		echo "$1"
	fi
}

function ensure_iface_naming()
{
	local iface="$1"

	if ! ip link show $iface &> /dev/null; then
		local renamed="";

		if dmesg | grep "renamed network interface $iface" &> /dev/null; then
			renamed=$(dmesg | grep "renamed network interface $iface" | tail -n 1 | awk '{ print $NF }')
		elif dmesg | grep "hpcap_register_chardev: $iface has" &> /dev/null; then
			renamed=$(dmesg | grep "hpcap_register_chardev: $iface has" | tail -n 1 | sed -E 's/\[.*\]//g' | awk '{ print $1 }' | tr -d ':')
		elif dmesg | grep ": renamed from $iface" &> /dev/null; then
			renamed=$(dmesg | grep ": renamed from $iface" | tail -n 1 | sed -E 's/.* ([a-zA-Z0-9]+): renamed.*/\1/')
		fi

		if [ -z "$renamed" ]; then
			echoy "Warning: interface $iface does not exist but didn't found the rename log"
		else
			echo "Found udev rename $iface -> $renamed"

			if ip link set $renamed down && ip link set $renamed name $iface ; then
				echo "$renamed renamed back to $iface"
			else
				echor "Could not rename $renamed"
			fi
		fi
	fi
}

function ensure_no_taking_down_links()
{
	local driver="$1"

	# Sorry for the superlong pipe command. But it's cool, isn't it?
	local driver_ifaces_with_link=$(ip route | sed -E 's/.* dev (\w+).*/\1/' | sort | uniq \
		| grep -vE "lo|hpcap|xgb" | xargs -n 1 -I {} bash -c "ethtool -i {} 2>/dev/null | grep -E '^driver:[ \t]+${driver}' &>/dev/null && echo {}")

	if [ ! -z "$driver_ifaces_with_link" ]; then
		echoy "Warning: Unloading the driver $driver will take down the active interfaces $(echo $driver_ifaces_with_link | tr '\n' ',')"
		read -p "Are you sure you want to unload $driver? [y/N]" yesno

		if [[ $yesno =~ ^[Yy]$ ]]; then
			return 0
		else
			return 1
		fi
	fi

	return 0
}

# Set an HPCAP interface
function interface_up_hpcap()
{
	local dev="hpcap"

	local iface=$1
	local mode=$2
	paramfile=$3
	local retval=0

	ensure_paramfile_exists

	local major=$(get_hpcap_major)
	local iface_index=$(get_iface_index $iface)
	local core=$(get_real_core_index $(read_value_param "core${iface_index}"))
	local nrxq=$(read_value_param nrxq)
	local speed=$(read_value_param "vel${iface_index}")
	local dups=$(read_value_param "dup${iface_index}")

	ensure_iface_naming $iface

	if [ $mode -eq 2 ] || [ $mode -eq 3 ]; then
		for rxq in $(seq 0 $(( $nrxq -1 )) ) ; do
			local devname="${dev}_${iface_index}_${rxq}"
			rm -f /dev/${devname}
			mknod /dev/${devname} c $(( $major + $iface_index )) $rxq
			chmod 666 /dev/${devname}

			map_cmd=$(get_binary huge_map)

			if [ $mode -eq 3 ]; then
				huge_size=$(read_value_param "hugesize${iface_index}")
				if [ -z "$huge_size" ]; then
					echoy "Warning: $iface in mode 3 but without hugesize${iface_index} parameter set."
				fi

				if hugepage_mount ; then
					if ! $map_cmd $iface_index $rxq map $huge_size $(get_hugepage_path); then
						echor "ERROR: Can't map hugepage buffer"
						echo "(Command was '$map_cmd $iface_index $rxq map $huge_size')"
						retval=1
					fi

					# Set read/write permissions for everybody
					find $(get_hugepage_path) -name "hpcap*" | xargs chmod a+rw
				else
					echo "ERROR: Can't mount hugetlbfs"
					retval=1
				fi
			fi
		done
	fi

	ip link set ${iface} up
	ip link set ${iface} promisc on
	set_irq_affinity $iface $core
	negocia_iface ${iface} $speed

	if [ $dups == 1 ]; then
		ethtool -K $iface rxhash on
	fi

	local ip=$(read_value_param "ip${iface_index}")
	local netmask=$(read_value_param "mask${iface_index}")

	if [ $mode -eq 1 ]; then
		[ -z $ip ] || ifconfig $iface $ip
		[ -z $netmask ] || ifconfig $iface netmask $netmask
	fi

	if [ $retval -eq 1 ]; then
		return 1
	else
		return 0
	fi
}

function write_ixgbe_pci_devs()
{
	local pcifile="$1"

	# File with the PCI IDs of the supported devices (Vendor:Device)
	# Yup, this is pretty ugly, but works independently of where's this script
	# placed.
	cat << EOF > $pcifile
8086:10f8
8086:10f9
8086:10fb
8086:10fc
8086:1517
8086:151c
8086:1529
8086:152a
8086:154f
8086:1557
8086:1528
8086:1563
EOF
}

function write_i40e_pci_devs()
{
	local pcifile="$1"

	cat << EOF > $pcifile
8086:1584
EOF
}

function write_i40evf_pci_devs()
{
	local pcifile="$1"

	cat << EOF > $pcifile
8086:154c
EOF
}


# Returns the PCI devices on this system that are supported by HPCAP.
function find_hpcap_pci_devs()
{
	local driver_type="$1"
	local tmp_pcifile=$(mktemp)

	if [ "$driver_type" = "ixgbe" ]; then
		write_ixgbe_pci_devs $tmp_pcifile
	elif [ "$driver_type" = "i40e" ]; then
		write_i40e_pci_devs $tmp_pcifile
	elif [ "$driver_type" = "i40evf" ]; then
		write_i40evf_pci_devs $tmp_pcifile
	else
		echo "Unrecognized driver type $driver_type, cannot get PCI devs" > /dev/stderr
		exit 0
	fi

	lspci -mn | tr -d '"' | awk -vpciFile="$tmp_pcifile" '
BEGIN {
	while ((getline pciId < pciFile) > 0)
		pciIds[pciId] = 1
}
{
	pciId = $3 ":" $4

	if (pciId in pciIds)
		print $1
}'

	rm "$tmp_pcifile"
}

function listcontains()
{
	for word in $1; do
		[[ $word = $2 ]] && return 0
	done

	return 1
}

# Ensures that the necessary commands (numactl currently) are
# present in the system.
function ensure_necessary_commands()
{
	local requisites="numactl"

	for req in $(echo $requisites | tr ' ' '\n'); do
		if ! which $req &> /dev/null; then
			echo "ERROR: $req command not present in your system. Please install it before continuing."
			exit 1
		fi
	done
}

function get_ethtool_param()
{
	local iface="$1"
	local param="$2"

	ethtool -S $iface 2>/dev/null | (grep -E "^ *${param}:" || echo "0 0") | awk '{ print $2 }' | tail -n 1
}

function get_iface_losses()
{
	local iface="$1"

	local missed=$(get_ethtool_param $iface "rx_missed_errors")
	local dropped=$(get_ethtool_param $iface "rx_dropped")
	local dropped_port=$(get_ethtool_param $iface "port.rx_dropped")

	bc <<< "$missed + $dropped + $dropped_port"
}

function get_iface_rx()
{
	local iface="$1"

	get_ethtool_param $iface "rx_bytes"
}

function get_iface_rx_frames()
{
	local iface="$1"

	get_ethtool_param $iface "rx_packets"
}

# Syntax: test_is_param_in_bounds param_name min max
# Tests whether a given value in the paramfile is in the closed interval
# [min max].
function test_is_param_in_bounds()
{
	local name="$1"
	local min="$2"
	local max="$3"

	local value=$(read_value_param $name)

	if [ -z "$value" ]; then
		echor "Error: $name is empty"
		return 1
	elif ! [[ $value =~ ^-?[0-9]+$ ]]; then
		echor "Error: $name should be an integer (it is '$value' now)"
		return 1
	elif [ ! -z "$min" ] && [ $value -lt $min ]; then
		echor "Error: $name should not be less than $min (it is '$value' now)"
		return 1
	elif [ ! -z "$max" ] && [ $value -gt $max ]; then
		echor "Error: $name should not be greater than $max (it is '$value' now)"
		return 1
	else
		return 0
	fi
}

function is_paramfile_valid()
{
	local file="$1"
	local old_paramfile="$paramfile" # Dirty hack.
	local has_error=0

	paramfile="$file"

	local use_vf="$(read_value_param use_vf)"

	if [ ! -z "$use_vf" ]; then
		echoy "Warning: use_vf parameter is deprecated and does not have any effect."
		echoy "         Set driver_type value to the appropriate driver."
	fi

	test_is_param_in_bounds nrxq 1 || has_error=1
	test_is_param_in_bounds ntxq 1 || has_error=1

	local driver_type="$(read_value_param driver_type)"

	if [ ! -z "$driver_type" ]; then
		local regex=\\b$driver_type\\b  # We need a temp variable to use the regex. \\b matches word boundary

		if [[ ! $base_driver_types =~ $regex ]]; then
			echor "Error: Unrecognized driver_type = $driver_type. Available options: $base_driver_types"
			has_error=1
		fi
	fi

	local ifaces=$(read_value_param ifs)
	local iface_count=$(read_value_param nif)
	local iface_expected_idx=0

	for iface in $ifaces; do
		if ! [[ "$iface" =~ ^(hpcap|xgb)[0-9]+$ ]]; then
			echor "Error: invalid interface name in ifs parameter: $iface"
			has_error=1
		else
			local iface_idx=$(get_iface_index $iface)

			# Sometime in the future, this shouldn't be an issue.
			if [ $iface_idx -ne $iface_expected_idx ]; then
				echor "Error: unordered interfaces. Interface $iface should have index $iface_expected_idx"
				has_error=1
			elif [ $iface_idx -ge $iface_count ]; then
				echor "Error: too many interfaces ($iface). The defined number of interfaces (parameter nif) is $iface_count"
				has_error=1
			else
				iface_expected_idx=$((iface_expected_idx + 1))
			fi
		fi
	done

	local max_pages=262144
	local total_pages=0

	for i in $(seq 0 $((iface_count - 1))) ; do
		local mode=$(read_value_param "mode${i}")
		local pages=$(read_value_param "pages${i}")
		local speed=$(read_value_param "vel${i}")
		local hugesize=$(read_value_param "hugesize${i}")

		test_is_param_in_bounds "mode${i}" 1 3 || has_error=1
		test_is_param_in_bounds "core${i}" -1 || has_error=1
		test_is_param_in_bounds "dup${i}" 0 1 || has_error=1
		test_is_param_in_bounds "caplen${i}" 0 || has_error=1
		test_is_param_in_bounds "pages${i}" 0 $max_pages || has_error=1

		if [ -z "$speed" ]; then
			echor "Error: vel${i} is empty."
		elif ! [ $speed = "1000" ] && ! [ $speed = "10000" ] && ! [ $speed = "40000" ]; then
			echor "Error: Wrong speed vel${i} = $speed. Supported values are 1000, 10000 and 40000"
			has_error=1
		fi

		if [ $mode = "2" ]; then
			total_pages=$((total_pages + pages))

			if [ $total_pages -gt $max_pages ]; then
				echor "Error: You assigned more pages than available ($total_pages > $max_pages)"
				has_error=1
			fi
		elif [ $mode = "3" ]; then
			if [ -z "$hugesize" ]; then
				echor "Error: Interface hpcap${i} in mode 3 but parameter hugesize${i} not set."
				has_error=1
			elif ! [[ "$hugesize" =~ ^[0-9]+[KMGT]?$ ]]; then
				echor "Error: hugesize${i} is not a valid buffer size. Example acceptable values: 1G, 2048M, 31621K"
				has_error=1
			fi
		fi
	done

	test_is_param_in_bounds monitor_enabled 0 1 || has_error=1
	test_is_param_in_bounds monitor_core -1 || has_error=1
	test_is_param_in_bounds monitor_interval 1 || has_error=1

	paramfile="$old_paramfile"

	if [ $has_error -eq 1 ]; then
		return 1 # 1 means error in bash!
	else
		return 0
	fi
}

function is_monitor_running()
{
	local logdir="$1"
	local iface="$2"
	local logtarget="${logdir}/${iface}-monitor.log"
	local pidfile="${logdir}/${iface}-monitor.pid"

	if [ -e $pidfile ] && is_pid_running $(cat $pidfile) &> /dev/null; then
		return 0
	else
		return 1
	fi
}

function is_hpcap_iface()
{
	local iface="$1"

	if [[ "$iface" =~ ^hpcap[0-9]+$ ]]; then
		return 0
	else
		return 1
	fi
}

function is_pid_running()
{
	local pid="$1"

	ps -p "$pid" &> /dev/null
}

function get_iface_in_core()
{
	local core="$1"
	local iface_count="$(read_value_param nif)"

	for i in $(seq 0 $((iface_count - 1)) ); do
		local iface_core="$(read_value_param "core${i}")"
		local iface_mode="$(read_value_param "mode${i}")"

		if [ $iface_mode -gt 1 ] && [ "$iface_core" = "$core" ]; then
			echo "hpcap${i}"
			return 0
		fi
	done

	echo ""
	return 1
}

function get_free_core_in_node()
{
	local node="$1"
	local corelist="$(numactl --hardware | grep "node $node cpus: " | cut -d':' -f2)"
	local iface_count="$()"

	for core in $corelist; do
		if ! get_iface_in_core $core &> /dev/null ; then
			echo "$core"
		fi
	done

	echo "-1"
}

function get_core_for_receiver()
{
	local recv_port="$1"

	local iface_core=$(read_value_param "core${recv_port}")
	local iface_node=$(nodo_del_core "$iface_core")
	local num_nodes=$(numactl --hardware | grep "cpus: " | wc -l)
	local receiver_node=$(( iface_node % num_nodes ))

	get_real_core_index $(get_free_core_in_node $receiver_node)
}


### Generator functions

function ensure_link()
{
	local recv_iface="$1"

	log "Checking for link on $recv_iface..."

	local max_link_attempts=5 # 5 seconds max to establish a link
	local link_attempts=0

	while ! has_link $recv_iface ; do
		log "No link on $recv_iface"
		sleep 1
		link_attempts=$((link_attempts + 1))

		if [ $link_attempts -ge $max_link_attempts ]; then
			log "Max link attempts reached: $link_attempts"
			test_fail_die
		fi
	done

	log "Link on $recv_iface"
}

# Starts the generator
# Args:
# 	- Rate (Mbps)
# 	- Frame size (bytes)
# 	- Output file for the generator
function start_generator()
{
	local generator_cmd=""
	local rate_Mbps="$1"
	local framesize="$2"
	local generator_out=""

	if [ -z "$3" ]; then
		generator_out="$logfile"
	else
		generator_out="$3"
		log "Redirecting output to $generator_out"
	fi

	if [ "$generator_mode" = "moongen" ]; then
		log "Using MoonGen for traffic generation"
		scp scripts/fixed-rate.lua ${generator_ssh_host}:MoonGen &> $logfile

		local generation_rate_Gbps=$(($rate_Mbps / 1000))
		log "Sending ${generation_rate_Gbps}Gbps through port $generator_port"

		generator_cmd="cd MoonGen; ./build/MoonGen fixed-rate.lua ${generator_port} ${generation_rate_Gbps} ${framesize}"
	elif [ "$generator_mode" = "fpga" ]; then
		log "FPGA generator does not support rate limiting."
		log "Sending at wire rate through port $generator_port"

		generator_cmd="cd $fpga_path; bin/sendRandom -s $framesize -l 1"
	elif [ "$generator_mode" = "hpcn_latency" ]; then
		log "hpcn_latency does not support rate limiting."
		log "hpcn_latency does not support generator port election."

		nocrc_framesize=$((framesize - 4))
		generator_cmd="cd $hpcn_latency_path; ./scripts/interface0.40g.sh --bw --ethd $hpcn_latency_dstmac --pktLen ${nocrc_framesize}"
	else
		log "start_generator: generator mode $generator_mode"
		test_fail_die
	fi

	log "Generator command is $generator_cmd"

	ssh -t $generator_ssh_host "$generator_cmd" > $generator_out 2>&1 &
	ssh_pid="$!"

	sleep 1

	if ! is_pid_running $ssh_pid &> /dev/null ; then
		log "ssh remote command did not start correctly"
		test_fail_die
	fi
}

# Starts the generator with support for duplicate generation
# Args:
# 	- Rate (Mbps)
# 	- Frame size (bytes)
# 	- Duplicate frequency (A duplicate will appear, in average, every "duplicate frequency"
# 		number of packets).
# 	- Output file for the generator
function start_generator_with_duplicate()
{
	local generator_cmd=""
	local rate_Mbps="$1"
	local framesize="$2"
	local dupfreq="$3"
	local generator_out="$4"

	if [ -z "$generator_out" ]; then
		generator_out=$logfile
	fi

	if [ "$generator_mode" = "moongen" ]; then
		log "Using MoonGen for traffic generation"
		scp scripts/fixed-rate.lua ${generator_ssh_host}:MoonGen &> $logfile
		scp scripts/duplicate-frames.lua ${generator_ssh_host}:MoonGen &> $logfile

		local generation_rate_Gbps=$(($rate_Mbps / 1000))
		log "Sending ${generation_rate_Gbps}Gbps through port $generator_port"

		generator_cmd="cd MoonGen; ./build/MoonGen duplicate-frames.lua ${generator_port} ${generation_rate_Gbps} ${framesize} ${dupfreq}"
	else
		log "Generator $generator_mode does not support traffic generation with duplicates"
		test_fail_die
	fi

	log "Generator command is $generator_cmd"

	ssh -t $generator_ssh_host "$generator_cmd" > $generator_out 2>&1 &
	ssh_pid="$!"

	sleep 1

	if ! is_pid_running $ssh_pid &> /dev/null ; then
		log "ssh remote command did not start correctly"
		test_fail_die
	fi
}

function stop_generator()
{
	log "Stopping generator..."

	if [ "$generator_mode" = "moongen" ]; then
		ssh -t $generator_ssh_host "killall -INT MoonGen" &> /dev/null
		sleep 3
		ssh -t $generator_ssh_host "killall -TERM MoonGen" &> /dev/null
		sleep 1
		kill -TERM $ssh_pid >> $logfile 2>&1
	elif [ "$generator_mode" = "hpcn_latency" ]; then
		ssh -t $generator_ssh_host "killall -INT hpcn_latency"
		sleep 3
		ssh -t $generator_ssh_host "killall -TERM hpcn_latency" &> /dev/null
		sleep 1
		kill -TERM $ssh_pid >> $logfile 2>&1
	else
		kill -TERM $ssh_pid >> $logfile 2>&1
		sleep 3
		kill -INT $ssh_pid >> $logfile 2>&1
		sleep 1
	fi

	if is_pid_running $ssh_pid >> $logfile 2>&1 ; then
		log "Could not stop sender/receiver. Aborting."
		log "Beware: the generator and/or receiver could be in an incosistent state."
		test_fail_die
	fi
}

function has_dupremoval_enabled()
{
	local driver_file="$1"

	if [ -z "$driver_file" ]; then
		driver_file="../bin/release/hpcap.ko"
	fi

	modinfo -F version $driver_file 2>&1 | grep "dup-removal" &> /dev/null
}

function get_pci_addr_for_iface()
{
	local iface="$1"

	ethtool -i $iface | grep "bus-info" | awk '{ print $2 }'
}

function get_module_for_driver_type()
{
	local drivtype="$1"

	if [ "$drivtype" = "ixgbe" ]; then
		echo hpcap.ko
	elif [ "$drivtype" = "ixgbevf" ]; then
		echo hpcapvf.ko
	elif [ "$drivtype" = "i40e" ]; then
		echo hpcapi.ko
	elif [ "$drivtype" = "i40evf" ]; then
		echo hpcapivf.ko
	else
		echo "Unrecognized driver type $drivtype" > /dev/stderr
		exit 1
	fi
}
