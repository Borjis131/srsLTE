#!/bin/bash
echo "Script for setting the scaling_governor to performance in all cpus"

cpus=$(nproc)

for (( i=0; i<=$cpus-1; i++ ))
do  
	echo "Setting scaling_governor for cpu$i from $(cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor) to performance"
	echo "performance" > /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor
done

for (( i=0; i<=$cpus-1; i++ ))
do  
	echo "Checking scaling_governor for cpu$i, set to $(cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor)"
done

