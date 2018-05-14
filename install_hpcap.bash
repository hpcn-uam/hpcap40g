#!/bin/bash

cd "$(dirname ${BASH_SOURCE[0]})"

export PATH=./scripts:${PATH}:/sbin:./bin/release

if ! source hpcap-lib.bash; then
	echo "Shell library (hpcap-lib.bash) not found in your \$PATH nor in the scripts directory"
	echo "Your HPCAP installation is probably corrupted."
	exit 1
fi

ensure_paramfile_exists

if ! is_paramfile_valid $paramfile ; then
	echor "Invalid parameters. Exiting"
	exit 1
fi

#######################
# CLEANING
#######################

# Close conflicting applications
echo "[ Killing apps ... ]"
kill_proc irqbalance
kill_proc hpcapdd SIGINT
kill_proc hpcapdd_p5g SIGINT
kill_proc hpcap-monitor

# Remove driver
echo "[ Removing modules ... ]"

modules_to_remove_ixgbe="ps_ixgbe ixgbe hpcap ixgbevf hpcap ixgbevf hpcapvf igb_uio"
modules_to_remove_mlnx="mlx4_en mlx4_ib mlx4_core mlx5_ib mlx5_core ib_ipoib ib_ucm ib_cm ib_umad ib_uverbs ib_sa ib_mad iw_cm ib_core ib_addr hpcapmlx"
modules_to_remove_i40e="i40e hpcapi i40evf hpcapivf"
modules_to_remove_i40evf="hpcapi i40evf hpcapivf"


#######################
# INSTALLING
#######################

if [ "$1" == "-d" ]; then
	echo "     -> Enabling debug modules"
	debug=1
	shift
else
	debug=0
fi


if [ -z "$1" ]; then
	driver_type=$(read_value_param driver_type)
	kofile=""

	if [ ! -z "$driver_type" ]; then
		module=$(get_module_for_driver_type $driver_type)

		if [ ! -z "$module" ]; then
			kofile="bin/release/$module"
		fi
	fi

	if [ -z "$kofile" ]; then
		kofile=bin/release/hpcap.ko
		driver_type=ixgbe
	fi
else
	kofile=$1
	driver_type="$(get_driver_type $kofile)"
fi

if [ $debug -eq 1 ]; then
	kofile=$(echo $kofile | sed 's/release/debug/g')
fi

if [ "$driver_type" = "mlnx" ]; then
	modules_to_remove="$modules_to_remove_mlnx"
elif [ "$driver_type" = "i40e" ]; then
	modules_to_remove="$modules_to_remove_i40e"
elif [ "$driver_type" = "i40evf" ]; then
	modules_to_remove="$modules_to_remove_i40evf"
else
	modules_to_remove="$modules_to_remove_ixgbe"
fi

for mod in $(echo $modules_to_remove | tr ' ' '\n') ; do
	ensure_no_taking_down_links $mod && remove_module $mod
done

args=$(parse_driver_args $paramfile $driver_type)

cmd="insmod $kofile $args"

echo "     -> CMD = \"$cmd\""

# This driver sometimes needs to be loaded before HPCAP.
# Do it automatically and don't say anything if it fails.
modprobe vxlan &> /dev/null

make -j $(nproc || echo 4) $kofile || die "Error building driver."
$cmd || die "Error when install the driver module."

echo "[ Compiling auxilar binaries... ]"
make -j $(nproc || echo 4) huge_map || die "Couldn't build huge_map utility."

powersave_governors="no"

for core in $(get_active_cores); do
	# get_iface_in_core only detects the first core of each interface.
	# Assume they are sequential, so only change the value "iface" when
	# we find another interface's core.
	if [ ! -z "$(get_iface_in_core $core)" ]; then
		iface="$(get_iface_in_core $core)"
	fi

	governor=$(get_scaling_governor_of $core)

	if [ "$governor" == "powersave" ]; then
		echoy "Warning: core ${core} (${iface}) has powersave governor"
		powersave_governors="y"
	fi
done

if [ "$powersave_governors" == "y" ]; then
	echoy "Use 'scripts/scaling-governors performance' to configure correctly the governors for those cores"
fi

#######################
# CREATING DEVICES AND SETTINGS
#######################

ret=0

ifs=$(read_value_param ifs)
for iface in $ifs
do
	mode=$(read_value_param "mode$(get_iface_index $iface)")

	echo "[ Setting up $iface in mode $mode... ]"
	if ! interface_up_hpcap $iface $mode $paramfile; then
		echor "Error setting up $iface. Will try to continue."
		ret=1
	fi
done

echo ""
echo "[ New char devices in /dev ]"
ls -l /dev | grep "hpcap"
echo ""


#######################
# COMPILE LIB
######################
echo "[ Compiling HPCAP library... ]"
make libs

######################
# LAUNCH MONITORING SCRIPT (if enabled)
######################

monitor_enabled=$(read_value_param monitor_enabled)

if [ "$monitor_enabled" = "1" ]; then
	launch-hpcap-monitors
fi

if [ $ret -ne 0 ]; then
	echor "Installed with errors. Please check the log."
fi

exit $ret
